#include <QApplication>
#include <QWidget>
#include <QScrollArea>
#include <QPainter>
#include <QImage>
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include <QPaintEvent>
#include <QTemporaryFile>
#include <QPdfDocument>
#include <QPdfView>
#include <QVBoxLayout>
#include <QProcess>
#include <QProcessEnvironment>
#include <vector>

#include "wlxplugin.h"

#define LOK_USE_UNSTABLE_API
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.h>

#define _detectstring "EXT=\"ODT\" | EXT=\"DOC\" | EXT=\"DOCX\" | EXT=\"ODS\" | EXT=\"XLS\" | EXT=\"XLSX\" | EXT=\"ODP\" | EXT=\"PPT\" | EXT=\"PPTX\""

// --- Config Utilities ---

QString getConfigValue(const QString& key, const QString& defaultValue = "") {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/doublecmd";
    QDir().mkpath(configDir);
    QSettings settings(configDir + "/officeview.conf", QSettings::IniFormat);
    return settings.value("Settings/" + key, defaultValue).toString();
}

void setConfigValue(const QString& key, const QString& value) {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/doublecmd";
    QSettings settings(configDir + "/officeview.conf", QSettings::IniFormat);
    settings.setValue("Settings/" + key, value);
}

// --- LibreOfficeKit Backend ---

static LibreOfficeKit* pOffice = nullptr;
static const int TWIPS_PER_PIXEL = 15;

struct PartInfo {
    int index;
    long width_twips;
    long height_twips;
    int pixel_y_offset;
    int pixel_width;
    int pixel_height;
};

class LOKWidget : public QWidget {
public:
    LOKWidget(LibreOfficeKitDocument* doc, QTemporaryFile* sourceFile, QWidget* parent = nullptr) : QWidget(parent), pDoc(doc), m_sourceFile(sourceFile) {
        if (pDoc) {
            int docType = pDoc->pClass->getDocumentType(pDoc);
            if (docType == LOK_DOCTYPE_PRESENTATION) {
                pDoc->pClass->initializeForRendering(pDoc, "{}");
            }
            
            int numParts = pDoc->pClass->getParts(pDoc);
            if (numParts <= 0) numParts = 1;
            
            for (int i = 0; i < numParts; ++i) {
                pDoc->pClass->setPart(pDoc, i);
                long w = 0, h = 0;
                pDoc->pClass->getDocumentSize(pDoc, &w, &h);
                
                PartInfo info;
                info.index = i;
                info.width_twips = w;
                info.height_twips = h;
                info.pixel_width = w / TWIPS_PER_PIXEL;
                info.pixel_height = h / TWIPS_PER_PIXEL;
                info.pixel_y_offset = m_totalHeight;
                
                m_totalHeight += info.pixel_height + 20; // 20px gap
                if (info.pixel_width > m_maxWidth) m_maxWidth = info.pixel_width;
                
                m_parts.push_back(info);
            }
            setFixedSize(m_maxWidth, m_totalHeight);
            
            // Reset to 0 just in case
            pDoc->pClass->setPart(pDoc, 0);
        }
    }
    
    ~LOKWidget() {
        if (pDoc) pDoc->pClass->destroy(pDoc);
        if (m_sourceFile) delete m_sourceFile;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (!pDoc) return;
        
        QRect rect = event->rect();
        QPainter painter(this);
        painter.fillRect(rect, Qt::lightGray);
        
        for (const auto& part : m_parts) {
            QRect partRect(0, part.pixel_y_offset, part.pixel_width, part.pixel_height);
            if (rect.intersects(partRect)) {
                QRect intersect = rect.intersected(partRect);
                
                painter.fillRect(intersect, Qt::white);
                
                int localX = intersect.x();
                int localY = intersect.y() - part.pixel_y_offset;
                
                int tilePosX = localX * TWIPS_PER_PIXEL;
                int tilePosY = localY * TWIPS_PER_PIXEL;
                int tileWidth = intersect.width() * TWIPS_PER_PIXEL;
                int tileHeight = intersect.height() * TWIPS_PER_PIXEL;
                
                QByteArray buffer;
                int stride = intersect.width() * 4;
                buffer.resize(intersect.height() * stride);
                buffer.fill((char)255);
                
                pDoc->pClass->setPart(pDoc, part.index);
                pDoc->pClass->paintTile(pDoc, (unsigned char*)buffer.data(), intersect.width(), intersect.height(), tilePosX, tilePosY, tileWidth, tileHeight);
                
                QImage image((const uchar*)buffer.constData(), intersect.width(), intersect.height(), stride, QImage::Format_ARGB32);
                painter.drawImage(intersect.topLeft(), image);
            }
        }
    }

private:
    LibreOfficeKitDocument* pDoc;
    QTemporaryFile* m_sourceFile;
    std::vector<PartInfo> m_parts;
    int m_totalHeight = 0;
    int m_maxWidth = 0;
};

QString findLibreOfficePath() {
    QByteArray envPath = qgetenv("LO_PATH");
    if (!envPath.isEmpty()) {
        QFileInfo fi(envPath);
        if (fi.exists() && fi.isDir()) return QString(envPath);
    }
    
    QString confPath = getConfigValue("LibreOfficePath");
    if (!confPath.isEmpty()) {
        QFileInfo fi(confPath);
        if (fi.exists() && fi.isDir()) return confPath;
    }
    
    QStringList fallbacks = {
        "/usr/lib/libreoffice/program",
        "/usr/lib64/libreoffice/program",
        "/opt/libreoffice/program"
    };
    for (const QString& fb : fallbacks) {
        QFileInfo fi(fb);
        if (fi.exists() && fi.isDir()) {
            setConfigValue("LibreOfficePath", fb);
            return fb;
        }
    }
    return QString();
}

// --- X2T Backend (Euro-Office / OnlyOffice) ---

class X2TWrapper {
public:
    bool isLoaded = false;
    QString loadedEngine = "";
    QString x2tBin = "";
    QString libPath = "";

    X2TWrapper(const QString& preferredEngine) {
        QStringList searchPaths;
        if (preferredEngine == "EuroOffice") {
            searchPaths << "/opt/euro-office/desktopeditors";
            searchPaths << "/opt/onlyoffice/desktopeditors";
        } else {
            searchPaths << "/opt/onlyoffice/desktopeditors";
            searchPaths << "/opt/euro-office/desktopeditors";
        }

        for (const QString& basePath : searchPaths) {
            QString bin = basePath + "/converter/x2t";
            if (QFileInfo::exists(bin)) {
                x2tBin = bin;
                libPath = basePath;
                isLoaded = true;
                loadedEngine = basePath.contains("euro") ? "EuroOffice" : "OnlyOffice";
                break;
            }
        }
    }

    bool convertToPdf(const QString& inputPath, const QString& outputPath) {
        if (!isLoaded) return false;
        
        QTemporaryFile configXml;
        configXml.setFileTemplate(QDir::tempPath() + "/x2t_config_XXXXXX.xml");
        if (!configXml.open()) return false;
        
        QString xml = QString("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                              "<TaskQueueDataConvert xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n"
                              "  <m_sFileFrom>%1</m_sFileFrom>\n"
                              "  <m_sFileTo>%2</m_sFileTo>\n"
                              "  <m_nFormatTo>513</m_nFormatTo>\n"
                              "  <m_bIsNoBase64>true</m_bIsNoBase64>\n"
                              "</TaskQueueDataConvert>").arg(inputPath, outputPath);
                              
        configXml.write(xml.toUtf8());
        configXml.flush();
        
        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LD_LIBRARY_PATH", libPath);
        proc.setProcessEnvironment(env);
        
        proc.start(x2tBin, QStringList() << configXml.fileName());
        if (proc.waitForFinished(10000)) {
            if (proc.exitCode() != 0) {
                printf("[OfficeView] x2t conversion failed with exit code %d\n", proc.exitCode());
                fflush(stdout);
            } else if (!QFileInfo::exists(outputPath) || QFileInfo(outputPath).size() == 0) {
                printf("[OfficeView] x2t conversion failed: output PDF is empty or missing\n");
                fflush(stdout);
            } else {
                return true;
            }
        } else {
            printf("[OfficeView] x2t conversion timed out or crashed\n");
            fflush(stdout);
        }
        return false;
    }
};

class PdfViewerWidget : public QWidget {
public:
    PdfViewerWidget(QPdfDocument* pdfDoc, QTemporaryFile* sourceFile, QTemporaryFile* tempPdf, QWidget* parent = nullptr) 
      : QWidget(parent), m_sourceFile(sourceFile), m_tempPdf(tempPdf) {
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        
        pdfDoc->setParent(this);
        QPdfView* pdfView = new QPdfView(this);
        pdfView->setDocument(pdfDoc);
        pdfView->setPageMode(QPdfView::PageMode::MultiPage);
        layout->addWidget(pdfView);
    }
    
    ~PdfViewerWidget() {
        if (m_sourceFile) delete m_sourceFile;
        if (m_tempPdf) delete m_tempPdf;
    }
    
private:
    QTemporaryFile* m_sourceFile;
    QTemporaryFile* m_tempPdf;
};

// --- Plugin Entry Points ---

extern "C" {
    HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
        QString filePath = QString::fromUtf8(FileToLoad);
        QString ext = QFileInfo(filePath).suffix().toLower();
        
        // Populate missing config options with defaults automatically
        QString enginePrefOOXML = getConfigValue("EngineForOOXML", "");
        if (enginePrefOOXML.isEmpty()) {
            X2TWrapper checkEngines("EuroOffice");
            if (checkEngines.isLoaded) {
                enginePrefOOXML = checkEngines.loadedEngine;
                setConfigValue(checkEngines.loadedEngine + "Path", checkEngines.libPath);
            } else {
                enginePrefOOXML = "LibreOffice";
            }
            setConfigValue("EngineForOOXML", enginePrefOOXML);
        }
        
        QString enginePrefODF = getConfigValue("EngineForODF", "");
        if (enginePrefODF.isEmpty()) {
            enginePrefODF = "LibreOffice";
            setConfigValue("EngineForODF", enginePrefODF);
        }
        
        // Copy to temp file to prevent doublecmd .lock file focus stealing loop
        QTemporaryFile* tempSource = new QTemporaryFile();
        tempSource->setFileTemplate(QDir::tempPath() + "/officeview_src_XXXXXX." + ext);
        if (!tempSource->open()) {
            delete tempSource;
            return nullptr;
        }
        
        QFile srcFile(filePath);
        if (srcFile.open(QIODevice::ReadOnly)) {
            tempSource->write(srcFile.readAll());
            srcFile.close();
        }
        tempSource->close(); // Close so external engines can read it
        
        bool isOOXML = (ext == "docx" || ext == "xlsx" || ext == "pptx");
        bool isODF = (ext == "odt" || ext == "ods" || ext == "odp");
        
        QString selectedEngine = "LibreOffice";
        
        if (isOOXML) {
            if (enginePrefOOXML == "Auto") selectedEngine = "EuroOffice";
            else selectedEngine = enginePrefOOXML;
        } else if (isODF) {
            if (enginePrefODF == "Auto") selectedEngine = "LibreOffice";
            else selectedEngine = enginePrefODF;
        }
        
        // Attempt x2t engines
        if (selectedEngine == "EuroOffice" || selectedEngine == "OnlyOffice" || selectedEngine == "Auto") {
            X2TWrapper wrapper(selectedEngine);
            if (wrapper.isLoaded) {
                printf("[OfficeView] Attempting to render %s with %s (x2t)...\n", filePath.toUtf8().constData(), wrapper.loadedEngine.toUtf8().constData());
                fflush(stdout);
                
                QTemporaryFile* tempPdf = new QTemporaryFile();
                tempPdf->setFileTemplate(QDir::tempPath() + "/officeview_XXXXXX.pdf");
                if (tempPdf->open()) {
                    QString outPath = tempPdf->fileName();
                    tempPdf->close();
                    
                    if (wrapper.convertToPdf(tempSource->fileName(), outPath)) {
                        QPdfDocument* pdfDoc = new QPdfDocument();
                        if (pdfDoc->load(outPath) == QPdfDocument::Error::None) {
                            printf("[OfficeView] Successfully rendered %s with %s (x2t)\n", filePath.toUtf8().constData(), wrapper.loadedEngine.toUtf8().constData());
                            fflush(stdout);
                            PdfViewerWidget* pdfWidget = new PdfViewerWidget(pdfDoc, tempSource, tempPdf, (QWidget*)ParentWin);
                            pdfWidget->show();
                            return (HWND)pdfWidget;
                        }
                        delete pdfDoc;
                    }
                }
                delete tempPdf;
                
                // Fallback to LO if x2t conversion failed
                printf("[OfficeView] Falling back to LibreOfficeKit for %s\n", filePath.toUtf8().constData());
                fflush(stdout);
                selectedEngine = "LibreOffice";
            } else if (enginePrefOOXML == "Auto" || enginePrefODF == "Auto") {
                selectedEngine = "LibreOffice";
            }
        }
        
        // LibreOffice engine
        if (selectedEngine == "LibreOffice") {
            printf("[OfficeView] Rendering %s with LibreOfficeKit\n", filePath.toUtf8().constData());
            fflush(stdout);
            
            if (!pOffice) {
                QString loPath = findLibreOfficePath();
                if (!loPath.isEmpty()) {
                    pOffice = lok_init_2(loPath.toUtf8().constData(), "file:///tmp/lok_profile_officeview");
                }
            }
            
            if (pOffice) {
                LibreOfficeKitDocument* pDoc = pOffice->pClass->documentLoad(pOffice, tempSource->fileName().toUtf8().constData());
                if (pDoc) {
                    QScrollArea* scrollArea = new QScrollArea((QWidget*)ParentWin);
                    LOKWidget* widget = new LOKWidget(pDoc, tempSource, scrollArea);
                    scrollArea->setWidget(widget);
                    scrollArea->setWidgetResizable(false);
                    scrollArea->show();
                    return (HWND)scrollArea;
                }
            }
        }
        
        delete tempSource;
        return nullptr;
    }

    void DCPCALL ListCloseWindow(HWND ListWin) {
        QWidget* widget = (QWidget*)ListWin;
        delete widget;
    }

    void DCPCALL ListGetDetectString(char* DetectString, int maxlen) {
        strncpy(DetectString, _detectstring, maxlen);
    }

    int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
        return LISTPLUGIN_ERROR;
    }

    int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter) {
        return LISTPLUGIN_ERROR;
    }
}
