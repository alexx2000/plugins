# officeview

A Double Commander WLX lister plugin that previews Microsoft Office and
OpenDocument files (Word/Excel/PowerPoint and their legacy/macro-enabled
variants, plus ODT/ODS/ODP) directly in Double Commander's Quick View panel
and file lister, without opening a separate editor.

## What it does

- Converts Microsoft-format documents (legacy binary, OOXML, and
  macro-enabled OOXML) to PDF via EuroOffice's or OnlyOffice's headless
  `x2t` converter, then renders that PDF with MuPDF -- including real
  click-and-drag text selection with highlighting, and copy (Ctrl+C,
  right-click, or Double Commander's own Copy command).
- Renders OpenDocument files (ODT/ODS/ODP) directly via LibreOfficeKit
  (LOK), LibreOffice's own embeddable rendering component -- no PDF
  round-trip for this format family.
- Spreadsheets (XLSX/XLSM via the PDF path, ODS via LOK) get a sheet-tab
  bar for switching between sheets. A sheet that needs more than one page
  is paginated properly, not squeezed onto a single page.
- Zoom in/out (`Ctrl +`/`Ctrl -`/`Ctrl 0`, or `Ctrl+scroll wheel`) on both
  rendering paths.
- Per-extension file size limits, so a very large document is skipped
  instead of stalling the preview panel.

Supported extensions (also what the plugin reports to Double Commander via
its detect string, i.e. which files it offers to preview): `DOC`, `DOCX`,
`DOCM`, `XLS`, `XLSX`, `XLSM`, `PPT`, `PPTX`, `PPTM`, `ODT`, `ODS`, `ODP`.
Template variants (`DOT`, `DOTX`, `DOTM`, `XLT`, `XLTX`, `XLTM`, `POT`,
`POTX`, `POTM`, `OTT`, `OTS`, `OTP`) are intentionally not included.

## Finding an office suite, and what happens if more than one is installed

On first use, the plugin looks for:

- **EuroOffice or OnlyOffice** (for the Microsoft-format/PDF-rendering
  path): checks `/opt/euro-office/desktopeditors/converter/x2t` and
  `/opt/onlyoffice/desktopeditors/converter/x2t`. If **both** are present,
  **EuroOffice is preferred**.
- **LibreOffice** (for the ODF/LOK path, and as a fallback for Microsoft
  formats if neither EuroOffice nor OnlyOffice is found, or if PDF
  conversion fails for a specific file): checks the `LO_PATH` environment
  variable first, then `/usr/lib/libreoffice/program`,
  `/usr/lib64/libreoffice/program`, and `/opt/libreoffice/program`, in
  that order.

Whatever is found is written to `officeview.conf` (see below) the first
time the plugin runs, so this detection only happens once -- after that,
the plugin just reads the config. Delete the relevant config values (or
the whole file) to force it to re-detect.

**ODF (ODT/ODS/ODP) always uses LibreOffice**, regardless of what's
detected for the other formats. This is deliberate, not a fallback:
LibreOffice's ODF rendering fidelity is meaningfully better than x2t's for
this format family.

## Configuration

Config file: `~/.config/doublecmd/officeview.conf` (INI format, created/
migrated automatically the first time the plugin loads a file).

```ini
[Paths]
LibreOfficePath=/usr/lib/libreoffice/program
EuroOfficePath=/opt/euro-office/desktopeditors
OnlyOfficePath=

[Engines]
EngineForOOXML=EuroOffice
EngineForODF=LibreOffice
EngineForLegacyMS=EuroOffice

; Size limit in bytes. Files larger than this are not opened at
; all -- the plugin doesn't attempt to process them. Set a value
; to 0 to effectively disable the plugin for that extension.
[FileSizeLimits]
DOC=3145728
DOCX=3145728
DOCM=3145728
XLS=3145728
XLSX=3145728
XLSM=3145728
PPT=3145728
PPTX=3145728
PPTM=3145728
ODT=3145728
ODS=3145728
ODP=3145728
```

### `[Paths]`

Where each office suite's install lives. Set these manually if
auto-detection picked the wrong install (e.g. a non-standard LibreOffice
location), or to point at a suite that isn't in one of the default search
paths. `LO_PATH` (environment variable) always takes priority over
`LibreOfficePath` if both are set.

### `[Engines]`

Which engine each format family uses:

- `EngineForOOXML` -- for DOCX/XLSX/PPTX/DOCM/XLSM/PPTM. Valid values:
  `EuroOffice`, `OnlyOffice`, `LibreOffice`.
- `EngineForODF` -- for ODT/ODS/ODP. In practice this should stay
  `LibreOffice` (see above), but the setting exists if you want to
  experiment with routing ODF through x2t instead.
- `EngineForLegacyMS` -- for DOC/XLS/PPT. Same valid values as
  `EngineForOOXML`.

Edit these directly to force a specific engine per format family instead
of relying on auto-detection.

### `[FileSizeLimits]`

One entry per extension, in **bytes**. A file larger than its extension's
limit is not opened or processed at all -- the plugin declines up front,
before any conversion work happens, and shows a short message explaining
why instead of attempting to render it. Set an extension's value to `0`
to disable previewing for that extension entirely.

## Known limitations

- **Text selection on the LibreOffice/ODF path is whole-document/
  whole-part, not click-and-drag.** The Microsoft-format/MuPDF path
  supports real click-and-drag text selection with highlighting (MuPDF's
  structured-text API exposes per-glyph positions). LibreOfficeKit's
  copy API doesn't give the plugin that same level of control, so
  ODT/ODS/ODP copy (Ctrl+C, right-click, or Double Commander's Copy
  command) always copies the entire current page/sheet's text, not just
  a selected portion.
- **Legacy `.xls` files don't get a sheet-tab bar.** Sheet-tab extraction
  works by reading the sheet list out of the file's own XML (xlsx/xlsm
  are zip archives with XML inside; ODS the same). Legacy `.xls` is the
  old BIFF binary format, not a zip/XML file, so this extraction can't
  work on it without a real binary format parser. `.xls` files still
  render correctly -- multi-page pagination works fine -- just without a
  way to jump between sheets; you only see whichever sheet the file
  itself opens to.
