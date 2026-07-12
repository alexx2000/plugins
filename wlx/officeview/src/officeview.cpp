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

class LOKWidget : public QWidget {
public:
    LOKWidget(LibreOfficeKitDocument* doc, QTemporaryFile* sourceFile, QWidget* parent = nullptr) : QWidget(parent), pDoc(doc), m_sourceFile(sourceFile) {
        long width_twips = 0, height_twips = 0;
        if (pDoc) {
            if (pDoc->pClass->getDocumentType(pDoc) == LOK_DOCTYPE_PRESENTATION) {
                pDoc->pClass->initializeForRendering(pDoc, "");
                pDoc->pClass->setPart(pDoc, 0);
            }
            pDoc->pClass->getDocumentSize(pDoc, &width_twips, &height_twips);
            setFixedSize(width_twips / TWIPS_PER_PIXEL, height_twips / TWIPS_PER_PIXEL);
        }
    }
    
    ~LOKWidget() {
        if (pDoc) pDoc->pClass->destroy(pDoc);
        if (m_sourceFile) delete m_sourceFile;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (!pDoc) return;
        
        QPainter painter(this);
        painter.fillRect(event->rect(), Qt::white);
        
        QRect rect = event->rect();
        int canvasWidth = rect.width();
        int canvasHeight = rect.height();
        int tilePosX = rect.x() * TWIPS_PER_PIXEL;
        int tilePosY = rect.y() * TWIPS_PER_PIXEL;
        int tileWidth = canvasWidth * TWIPS_PER_PIXEL;
        int tileHeight = canvasHeight * TWIPS_PER_PIXEL;
        int stride = canvasWidth * 4;
        
        QByteArray buffer;
        buffer.resize(canvasHeight * stride);
        buffer.fill((char)255);
        
        pDoc->pClass->paintTile(pDoc, (unsigned char*)buffer.data(), canvasWidth, canvasHeight, tilePosX, tilePosY, tileWidth, tileHeight);
        QImage image((const uchar*)buffer.constData(), canvasWidth, canvasHeight, stride, QImage::Format_ARGB32);
        painter.drawImage(rect.topLeft(), image);
    }

private:
    LibreOfficeKitDocument* pDoc;
    QTemporaryFile* m_sourceFile;
};

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
            return (proc.exitCode() == 0 || QFileInfo::exists(outputPath));
        }
        return false;
    }
};

class PdfViewerWidget : public QWidget {
public:
    PdfViewerWidget(QTemporaryFile* sourceFile, const QString& engine, QWidget* parent = nullptr) : QWidget(parent), m_sourceFile(sourceFile) {
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        
        tempPdf = new QTemporaryFile(this);
        tempPdf->setFileTemplate(QDir::tempPath() + "/officeview_XXXXXX.pdf");
        if (tempPdf->open()) {
            QString outPath = tempPdf->fileName();
            tempPdf->close(); // Close so x2t can write to it
            
            X2TWrapper wrapper(engine);
            if (wrapper.convertToPdf(sourceFile->fileName(), outPath)) {
                QPdfDocument* pdfDoc = new QPdfDocument(this);
                pdfDoc->load(outPath);
                
                QPdfView* pdfView = new QPdfView(this);
                pdfView->setDocument(pdfDoc);
                pdfView->setPageMode(QPdfView::PageMode::MultiPage);
                layout->addWidget(pdfView);
            }
        }
    }
    
    ~PdfViewerWidget() {
        if (m_sourceFile) delete m_sourceFile;
    }
    
private:
    QTemporaryFile* tempPdf;
    QTemporaryFile* m_sourceFile;
};

// --- Plugin Entry Points ---

extern "C" {
    HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
        QString filePath = QString::fromUtf8(FileToLoad);
        QString ext = QFileInfo(filePath).suffix().toLower();
        
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
        // Note: QTemporaryFile auto-removes on destruction. We pass ownership to the widgets.
        
        bool isOOXML = (ext == "docx" || ext == "xlsx" || ext == "pptx");
        bool isODF = (ext == "odt" || ext == "ods" || ext == "odp");
        
        QString enginePrefOOXML = getConfigValue("EngineForOOXML", "Auto");
        QString enginePrefODF = getConfigValue("EngineForODF", "Auto");
        
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
                PdfViewerWidget* pdfWidget = new PdfViewerWidget(tempSource, wrapper.loadedEngine, (QWidget*)ParentWin);
                pdfWidget->show();
                return (HWND)pdfWidget;
            }
            if (enginePrefOOXML == "Auto" || enginePrefODF == "Auto") {
                selectedEngine = "LibreOffice";
            }
        }
        
        // LibreOffice engine
        if (selectedEngine == "LibreOffice") {
            if (!pOffice) {
                QString loPath = findLibreOfficePath();
                if (!loPath.isEmpty()) {
                    pOffice = lok_init(loPath.toUtf8().constData());
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
