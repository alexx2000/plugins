#include "wlxplugin.h"
#include "kpartwidget.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QWidget>
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
    snprintf(DetectString, maxlen,
        // Okular: documents & ebooks
        "EXT=\"PDF\" | EXT=\"EPUB\" | EXT=\"MOBI\" | EXT=\"DJVU\" | EXT=\"DJV\" | "
        "EXT=\"XPS\" | EXT=\"OXPS\" | EXT=\"PS\" | EXT=\"EPS\" | "
        "EXT=\"CBR\" | EXT=\"CBZ\" | EXT=\"CB7\" | EXT=\"CBT\" | "
        "EXT=\"ODT\" | EXT=\"ODS\" | EXT=\"ODP\" | EXT=\"ODG\" | "
        "EXT=\"DOCX\" | EXT=\"XLSX\" | EXT=\"PPTX\" | EXT=\"DOC\" | EXT=\"PPT\" | "
        "EXT=\"TIFF\" | EXT=\"TIF\" | EXT=\"CHM\" | "
        // Gwenview: images
        "EXT=\"PNG\" | EXT=\"JPG\" | EXT=\"JPEG\" | EXT=\"GIF\" | EXT=\"BMP\" | "
        "EXT=\"WEBP\" | EXT=\"SVG\" | EXT=\"SVGZ\" | EXT=\"ICO\" | "
        "EXT=\"XPM\" | EXT=\"PBM\" | EXT=\"PGM\" | EXT=\"PPM\" | EXT=\"PNM\" | "
        "EXT=\"AVIF\" | EXT=\"JXL\" | EXT=\"HEIF\" | EXT=\"HEIC\" | "
        // KFontView: fonts
        "EXT=\"TTF\" | EXT=\"OTF\" | EXT=\"TTC\" | EXT=\"WOFF\" | EXT=\"WOFF2\" | "
        "EXT=\"PFA\" | EXT=\"PFB\" | EXT=\"PCF\" | EXT=\"BDF\" | "
        // Ark: archives
        "EXT=\"ZIP\" | EXT=\"TAR\" | EXT=\"GZ\" | EXT=\"BZ2\" | EXT=\"XZ\" | "
        "EXT=\"ZSTD\" | EXT=\"ZST\" | EXT=\"LZ\" | EXT=\"LZMA\" | "
        "EXT=\"7Z\" | EXT=\"RAR\" | EXT=\"ARJ\" | EXT=\"CAB\" | "
        "EXT=\"RPM\" | EXT=\"DEB\" | EXT=\"ISO\" | EXT=\"CPIO\" | "
        "EXT=\"TGZ\" | EXT=\"TBZ2\" | EXT=\"TXZ\" | EXT=\"TAR.GZ\" | "
        // Kate: source code & text
        "EXT=\"TXT\" | EXT=\"MD\" | EXT=\"RST\" | EXT=\"LOG\" | EXT=\"CONF\" | "
        "EXT=\"CFG\" | EXT=\"INI\" | EXT=\"YAML\" | EXT=\"YML\" | EXT=\"TOML\" | "
        "EXT=\"JSON\" | EXT=\"XML\" | EXT=\"CSV\" | EXT=\"TSV\" | "
        "EXT=\"C\" | EXT=\"H\" | EXT=\"CPP\" | EXT=\"HPP\" | EXT=\"CXX\" | "
        "EXT=\"CC\" | EXT=\"HH\" | EXT=\"CS\" | "
        "EXT=\"JAVA\" | EXT=\"KT\" | EXT=\"SCALA\" | EXT=\"GROOVY\" | "
        "EXT=\"PY\" | EXT=\"RB\" | EXT=\"PL\" | EXT=\"PM\" | EXT=\"LUA\" | "
        "EXT=\"JS\" | EXT=\"TS\" | EXT=\"JSX\" | EXT=\"TSX\" | EXT=\"VUE\" | "
        "EXT=\"HTML\" | EXT=\"HTM\" | EXT=\"CSS\" | EXT=\"SCSS\" | EXT=\"SASS\" | EXT=\"LESS\" | "
        "EXT=\"PHP\" | EXT=\"GO\" | EXT=\"RS\" | EXT=\"SWIFT\" | EXT=\"D\" | "
        "EXT=\"SH\" | EXT=\"BASH\" | EXT=\"ZSH\" | EXT=\"FISH\" | "
        "EXT=\"SQL\" | EXT=\"R\" | EXT=\"M\" | EXT=\"TEX\" | EXT=\"BIB\" | "
        "EXT=\"DIFF\" | EXT=\"PATCH\" | EXT=\"CMAKE\" | EXT=\"MAKEFILE\" | "
        "EXT=\"PAS\" | EXT=\"PP\" | EXT=\"LPR\" | EXT=\"LPI\" | EXT=\"LPS\"");
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
