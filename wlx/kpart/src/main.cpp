#include "wlxplugin.h"
#include "kpartwidget.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QWidget>
#include <KPluginMetaData>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSet>
#include <QStringList>
#include <cstdio>

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
{
    (void)ShowFlags;

    if (!QCoreApplication::instance()) {
        return nullptr;
    }

    QWidget *parent = static_cast<QWidget*>(ParentWin);
    KPartWidget *view = new KPartWidget(parent);
    
    if (view->loadFile(QString::fromUtf8(FileToLoad))) {
        view->show();
        return static_cast<HWND>(view);
    } else {
        delete view;
        return nullptr;
    }
}

HWND DCPCALL ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags)
{
    (void)ShowFlags;

    if (!QCoreApplication::instance()) {
        return nullptr;
    }

    QString fileName = QString::fromUtf16(reinterpret_cast<const char16_t*>(FileToLoad));
    
    QWidget *parent = static_cast<QWidget*>(ParentWin);
    KPartWidget *view = new KPartWidget(parent);

    if (view->loadFile(fileName)) {
        view->show();
        return static_cast<HWND>(view);
    } else {
        delete view;
        return nullptr;
    }
}

int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags)
{
    (void)ParentWin;
    (void)ShowFlags;
    
    KPartWidget *view = static_cast<KPartWidget*>(PluginWin);
    if (!view) return LISTPLUGIN_ERROR;

    if (view->loadFile(QString::fromUtf8(FileToLoad))) {
        return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
}

int DCPCALL ListLoadNextW(HWND ParentWin, HWND PluginWin, WCHAR* FileToLoad, int ShowFlags)
{
    (void)ParentWin;
    (void)ShowFlags;
    
    KPartWidget *view = static_cast<KPartWidget*>(PluginWin);
    if (!view) return LISTPLUGIN_ERROR;

    QString fileName = QString::fromUtf16(reinterpret_cast<const char16_t*>(FileToLoad));
    if (view->loadFile(fileName)) {
        return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    KPartWidget *view = static_cast<KPartWidget*>(ListWin);
    if (view) {
        delete view;
    }
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
    // Need a QCoreApplication to use QMimeDatabase and KPluginMetaData
    int argc = 1;
    char* argv[] = { (char*)"doublecmd", nullptr };
    QCoreApplication *app = QCoreApplication::instance();
    bool appCreated = false;
    if (!app) {
        app = new QCoreApplication(argc, argv);
        appCreated = true;
    }

    QSet<QString> extensions;
    QMimeDatabase mimeDb;
    
    // Find all KParts installed on the system
    QVector<KPluginMetaData> parts = KPluginMetaData::findPlugins(QStringLiteral("kf6/parts"));
    for (const KPluginMetaData &part : parts) {
        QStringList mimeTypes = part.mimeTypes();
        for (const QString &mimeName : mimeTypes) {
            QMimeType mimeType = mimeDb.mimeTypeForName(mimeName);
            if (mimeType.isValid()) {
                QString ext = mimeType.preferredSuffix().toUpper();
                if (!ext.isEmpty()) {
                    extensions.insert(ext);
                }
            }
        }
    }

    if (extensions.isEmpty()) {
        // Fallback in case finding plugins failed or zero parts are installed
        snprintf(DetectString, maxlen, "EXT=\"TXT\"");
    } else {
        QStringList extList = extensions.values();
        // Sort for deterministic output
        extList.sort();
        
        QString result;
        for (const QString &ext : extList) {
            if (!result.isEmpty()) {
                result += QLatin1String(" | ");
            }
            result += QStringLiteral("EXT=\"%1\"").arg(ext);
        }
        
        QByteArray utf8 = result.toUtf8();
        qstrncpy(DetectString, utf8.constData(), maxlen);
    }

    if (appCreated) {
        delete app;
    }
}

int DCPCALL ListSearchDialog(HWND ListWin, int FindNext)
{
    (void)ListWin;
    (void)FindNext;
    return LISTPLUGIN_OK;
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    if (Command == 5 /* lc_focus */) {
        KPartWidget *view = static_cast<KPartWidget*>(ListWin);
        if (view) {
            view->setActive(Parameter != 0);
            return LISTPLUGIN_OK;
        }
    }
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
    (void)dps;
    
    // Set application metadata once during plugin global initialization
    // This helps KDE Frameworks associate jobs with the application.
    if (QCoreApplication::instance()) {
        if (QCoreApplication::applicationName().isEmpty()) {
            QCoreApplication::setApplicationName(QStringLiteral("doublecmd"));
        }
        if (QGuiApplication::desktopFileName().isEmpty()) {
            QGuiApplication::setDesktopFileName(QStringLiteral("doublecmd"));
        }
    }
}

} // extern "C"
