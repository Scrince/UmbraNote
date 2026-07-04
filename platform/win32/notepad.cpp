#include "notepad.h"
#include "resource.h"

#include <zeronote/crypto.h>
#include <zeronote/pdf_export.h>
#include <zeronote/text_codec.h>

#include <algorithm>
#include <vector>

#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")

AppState g_app;

enum class PasswordDialogMode { Open, Save };

struct PasswordDialogParams {
    PasswordDialogMode mode = PasswordDialogMode::Open;
    bool require_keyfile = false;
    bool paranoid_kdf = true;
    std::wstring password;
    std::wstring keyfilePath;
    std::vector<uint8_t> keyfile;
};

namespace {

constexpr int kStatusHeight = 24;
constexpr int kDefaultEditorPointSize = 12;
constexpr COLORREF kEditorBgColor = RGB(255, 255, 255);
constexpr COLORREF kChromeBorderColor = RGB(225, 225, 225);

int PointsToLogFontHeight(HWND hwnd, int pointSize) {
    const HDC hdc = GetDC(hwnd);
    const int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);
    return -MulDiv(pointSize, logPixelsY, 72);
}

void InitDefaultEditorLogFont(HWND hwnd) {
    g_app.lf = LOGFONTW{};
    g_app.lf.lfHeight = PointsToLogFontHeight(hwnd, kDefaultEditorPointSize);
    g_app.lf.lfWeight = FW_NORMAL;
    g_app.lf.lfCharSet = DEFAULT_CHARSET;
    g_app.lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(g_app.lf.lfFaceName, L"Consolas");
}

std::wstring GetFileName(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring GetWindowTextString(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return L"";
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(len);
    return text;
}

void SetWindowTextString(HWND hwnd, const std::wstring& text) {
    SetWindowTextW(hwnd, text.c_str());
}

std::wstring GetEditText() {
    const int len = GetWindowTextLengthW(g_app.hwndEdit);
    if (len <= 0) return L"";
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(g_app.hwndEdit, text.data(), len + 1);
    text.resize(len);
    return text;
}

void SetEditText(const std::wstring& text) {
    SetWindowTextW(g_app.hwndEdit, text.c_str());
}

std::wstring FoldCase(const std::wstring& text) {
    if (text.empty()) return text;
    std::wstring folded(text.size(), L'\0');
    const int len = LCMapStringEx(
        LOCALE_NAME_USER_DEFAULT,
        LCMAP_LINGUISTIC_CASING | LCMAP_LOWERCASE,
        text.c_str(), static_cast<int>(text.size()),
        &folded[0], static_cast<int>(folded.size()),
        nullptr, nullptr, 0);
    if (len > 0) {
        folded.resize(static_cast<size_t>(len));
        return folded;
    }
    std::wstring out = text;
    for (wchar_t& ch : out) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return out;
}

int FindText(const std::wstring& haystack, const std::wstring& needle, bool matchCase, int start) {
    if (needle.empty()) return -1;
    if (matchCase) {
        const size_t pos = haystack.find(needle, static_cast<size_t>(start));
        return pos == std::wstring::npos ? -1 : static_cast<int>(pos);
    }
    const std::wstring foldedHay = FoldCase(haystack);
    const std::wstring foldedNeedle = FoldCase(needle);
    const size_t pos = foldedHay.find(foldedNeedle, static_cast<size_t>(start));
    return pos == std::wstring::npos ? -1 : static_cast<int>(pos);
}

void ApplyModernChrome(HWND hwnd) {
    BOOL useDark = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    COLORREF border = kChromeBorderColor;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const int roundCorners = 2;
    DwmSetWindowAttribute(hwnd, 33, &roundCorners, sizeof(roundCorners));
}

HWND CreateEditorWindow(HWND parent) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                    ES_NOHIDESEL | ES_WANTRETURN;
    if (!g_app.wordWrap) {
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    }

    HWND hwndEdit = CreateWindowExW(
        0, L"EDIT", L"", style,
        0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_EDIT),
        g_app.hInstance, nullptr);
    SendMessageW(hwndEdit, EM_LIMITTEXT, 0, 0);
    SendMessageW(hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
    if (g_app.hFont) {
        SendMessageW(hwndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
    }
    return hwndEdit;
}

BOOL CALLBACK SetChildFontProc(HWND child, LPARAM lParam) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
    return TRUE;
}

void ApplyUiFontToDialog(HWND hwnd) {
    if (!g_app.hUiFont) return;
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hUiFont), TRUE);
    EnumChildWindows(hwnd, SetChildFontProc, reinterpret_cast<LPARAM>(g_app.hUiFont));
}

void SelectRange(int start, int length) {
    SendMessageW(g_app.hwndEdit, EM_SETSEL, start, start + length);
    SendMessageW(g_app.hwndEdit, EM_SCROLLCARET, 0, 0);
    SetFocus(g_app.hwndEdit);
}

zeronote::crypto::EncryptionOptions BuildEncryptionOptions(const PasswordDialogParams& params) {
    zeronote::crypto::EncryptionOptions options;
    options.keyfile = params.keyfile;
    options.paranoid_kdf = params.paranoid_kdf;
    return options;
}

bool SaveEncryptedFile(const std::wstring& path, const std::wstring& text,
                       const PasswordDialogParams& auth, std::wstring& error) {
    std::vector<uint8_t> encrypted;
    std::string err;
    if (!zeronote::crypto::EncryptText(zeronote::WideToUtf8(text),
                                       zeronote::WideToUtf8(auth.password),
                                       encrypted, err,
                                       BuildEncryptionOptions(auth))) {
        error = zeronote::Utf8ToWide(err);
        return false;
    }
    return zeronote::WriteFileBytes(path, encrypted);
}

bool PromptPassword(HWND hwnd, PasswordDialogParams& params) {
    const INT_PTR result = DialogBoxParamW(
        g_app.hInstance, MAKEINTRESOURCEW(IDD_PASSWORD), hwnd, PasswordDlgProc,
        reinterpret_cast<LPARAM>(&params));
    return result == IDOK && !params.password.empty();
}

bool LoadKeyfileBytes(const std::wstring& path, std::vector<uint8_t>& bytes, std::wstring& error) {
    if (!zeronote::ReadFileBytes(path, bytes)) {
        error = L"Cannot read the selected keyfile.";
        return false;
    }
    if (bytes.size() < 32) {
        error = L"Keyfile must be at least 32 bytes.";
        return false;
    }
    return true;
}

void CheckMenuItemById(HWND hwnd, UINT id, bool checked) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) return;
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

void EnableMenuItemById(HWND hwnd, UINT id, bool enabled) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) return;
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

}

void ClearEncryptionSession() {
    g_app.fileEncrypted = false;
    g_app.encryptedRequiresKeyfile = false;
    g_app.encryptedParanoidKdf = true;
}

void UpdateTitle(HWND hwnd) {
    const std::wstring name = g_app.filePath.empty() ? L"Untitled" : GetFileName(g_app.filePath);
    const std::wstring encrypted = g_app.fileEncrypted ? L" [Encrypted]" : L"";
    const std::wstring title = (g_app.modified ? L"*" : L"") + name + encrypted + L" - UmbraNote";
    SetWindowTextW(hwnd, title.c_str());
}

void UpdateStatusBar() {
    if (!g_app.hwndStatus || g_app.wordWrap) return;

    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(g_app.hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

    const std::wstring text = GetEditText();
    const std::wstring before = text.substr(0, start);
    const int line = static_cast<int>(std::count(before.begin(), before.end(), L'\n')) + 1;
    const size_t lastNl = before.find_last_of(L'\n');
    const int col = lastNl == std::wstring::npos
                        ? static_cast<int>(before.size()) + 1
                        : static_cast<int>(before.size() - lastNl);

    wchar_t status[64];
    swprintf_s(status, L"Ln %d, Col %d", line, col);
    SendMessageW(g_app.hwndStatus, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(status));
}

void ResizeControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int statusHeight = g_app.wordWrap ? 0 : kStatusHeight;
    if (g_app.hwndStatus) {
        ShowWindow(g_app.hwndStatus, g_app.wordWrap ? SW_HIDE : SW_SHOW);
        if (!g_app.wordWrap) {
            SetWindowPos(g_app.hwndStatus, nullptr, 0, rc.bottom - statusHeight,
                         rc.right, statusHeight, SWP_NOZORDER);
        }
    }

    if (g_app.hwndEdit) {
        SetWindowPos(g_app.hwndEdit, nullptr, 0, 0, rc.right,
                     rc.bottom - statusHeight, SWP_NOZORDER);
    }
}

void ApplyFont() {
    if (g_app.hFont) {
        DeleteObject(g_app.hFont);
        g_app.hFont = nullptr;
    }
    g_app.hFont = CreateFontIndirectW(&g_app.lf);
    SendMessageW(g_app.hwndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hFont), TRUE);
}

void ApplyWordWrap(HWND hwnd) {
    if (!g_app.hwndEdit) return;

    const bool wasModified = g_app.modified;
    const std::wstring text = GetEditText();
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(g_app.hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

    DestroyWindow(g_app.hwndEdit);
    g_app.hwndEdit = nullptr;

    g_app.hwndEdit = CreateEditorWindow(hwnd);
    ApplyFont();
    SetEditText(text);
    SelectRange(static_cast<int>(start), static_cast<int>(end - start));
    SetModified(wasModified);
    ResizeControls(hwnd);
    CheckMenuItemById(hwnd, IDM_FORMAT_WORDWRAP, g_app.wordWrap);
    UpdateMenuState(hwnd);
    UpdateStatusBar();
}

void SetModified(bool modified) {
    g_app.modified = modified;
    if (g_app.hwndEdit) {
        SendMessageW(g_app.hwndEdit, EM_SETMODIFY, modified ? TRUE : FALSE, 0);
    }
    if (g_app.hwndMain) {
        UpdateTitle(g_app.hwndMain);
    }
}

void SyncModifiedFromEdit() {
    if (!g_app.hwndEdit) return;
    const bool modified = SendMessageW(g_app.hwndEdit, EM_GETMODIFY, 0, 0) != 0;
    if (modified != g_app.modified) {
        g_app.modified = modified;
        if (g_app.hwndMain) {
            UpdateTitle(g_app.hwndMain);
        }
    }
}

void UpdateMenuState(HWND hwnd) {
    DWORD selStart = 0;
    DWORD selEnd = 0;
    SendMessageW(g_app.hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    const bool hasSelection = selStart != selEnd;
    const bool canUndo = SendMessageW(g_app.hwndEdit, EM_CANUNDO, 0, 0) != 0;
    const bool hasFind = !g_app.findText.empty();

    EnableMenuItemById(hwnd, IDM_EDIT_UNDO, canUndo);
    EnableMenuItemById(hwnd, IDM_EDIT_CUT, hasSelection);
    EnableMenuItemById(hwnd, IDM_EDIT_COPY, hasSelection);
    EnableMenuItemById(hwnd, IDM_EDIT_DELETE, hasSelection);
    EnableMenuItemById(hwnd, IDM_EDIT_FINDNEXT, hasFind);
    EnableMenuItemById(hwnd, IDM_EDIT_GOTO, !g_app.wordWrap);
}

bool ConfirmSaveIfNeeded(HWND hwnd) {
    if (!g_app.modified) return true;

    const std::wstring name = g_app.filePath.empty() ? L"Untitled" : GetFileName(g_app.filePath);
    const std::wstring msg = L"Do you want to save changes to " + name + L"?";
    const int result = MessageBoxW(hwnd, msg.c_str(), L"UmbraNote",
                                   MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) return false;
    if (result == IDYES) return DoFileSave(hwnd);
    return true;
}

void DoFileNew(HWND hwnd) {
    if (!ConfirmSaveIfNeeded(hwnd)) return;
    SetEditText(L"");
    g_app.filePath.clear();
    ClearEncryptionSession();
    SetModified(false);
    UpdateTitle(hwnd);
    UpdateStatusBar();
    SetFocus(g_app.hwndEdit);
}

void DoFileOpen(HWND hwnd) {
    if (!ConfirmSaveIfNeeded(hwnd)) return;

    wchar_t fileName[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"All Supported (*.txt;*.zro)\0*.txt;*.zro\0"
        L"Text Documents (*.txt)\0*.txt\0"
        L"Encrypted Notes (*.zro)\0*.zro\0"
        L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";

    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<uint8_t> bytes;
    if (!zeronote::ReadFileBytes(fileName, bytes)) {
        MessageBoxW(hwnd, L"Cannot open the file.", L"UmbraNote", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring content;
    ClearEncryptionSession();

    if (zeronote::crypto::IsEncryptedFile(bytes)) {
        zeronote::crypto::EncryptedFileInfo info;
        zeronote::crypto::GetEncryptedFileInfo(bytes, info);

        PasswordDialogParams auth;
        auth.mode = PasswordDialogMode::Open;
        auth.require_keyfile = info.requires_keyfile;
        auth.paranoid_kdf = info.paranoid_kdf;
        if (!PromptPassword(hwnd, auth)) return;

        std::string plainUtf8;
        std::string error;
        if (!zeronote::crypto::DecryptText(bytes, zeronote::WideToUtf8(auth.password),
                                           plainUtf8, error,
                                           BuildEncryptionOptions(auth))) {
            MessageBoxW(hwnd, zeronote::Utf8ToWide(error).c_str(),
                        L"UmbraNote", MB_OK | MB_ICONERROR);
            return;
        }

        content = zeronote::Utf8ToWide(plainUtf8);
        g_app.fileEncrypted = true;
        g_app.encryptedRequiresKeyfile = info.requires_keyfile;
        g_app.encryptedParanoidKdf = info.paranoid_kdf;
    } else {
        std::string plainUtf8;
        if (!zeronote::DecodeTextFromBytes(bytes, plainUtf8)) {
            MessageBoxW(hwnd, L"Cannot open the file.", L"UmbraNote", MB_OK | MB_ICONERROR);
            return;
        }
        content = zeronote::Utf8ToWide(plainUtf8);
    }

    SetEditText(content);
    g_app.filePath = fileName;
    SetModified(false);
    UpdateTitle(hwnd);
    UpdateStatusBar();
    SetFocus(g_app.hwndEdit);
}

bool DoFileSave(HWND hwnd) {
    if (g_app.filePath.empty()) {
        return g_app.fileEncrypted ? DoFileSaveEncryptedAs(hwnd) : DoFileSaveAs(hwnd);
    }

    if (g_app.fileEncrypted) {
        PasswordDialogParams auth;
        auth.mode = PasswordDialogMode::Open;
        auth.require_keyfile = g_app.encryptedRequiresKeyfile;
        auth.paranoid_kdf = g_app.encryptedParanoidKdf;
        if (!PromptPassword(hwnd, auth)) {
            return false;
        }

        std::wstring error;
        if (!SaveEncryptedFile(g_app.filePath, GetEditText(), auth, error)) {
            MessageBoxW(hwnd, error.empty() ? L"Cannot save the encrypted file." : error.c_str(),
                        L"UmbraNote", MB_OK | MB_ICONERROR);
            return false;
        }

        SetModified(false);
        return true;
    }

    if (!zeronote::SaveTextFileUtf8(g_app.filePath, zeronote::WideToUtf8(GetEditText()))) {
        MessageBoxW(hwnd, L"Cannot save the file.", L"UmbraNote", MB_OK | MB_ICONERROR);
        return false;
    }
    SetModified(false);
    return true;
}

bool DoFileSaveAs(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = L"";
    if (!g_app.filePath.empty()) {
        wcscpy_s(fileName, g_app.filePath.c_str());
    } else {
        wcscpy_s(fileName, L"Untitled.txt");
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&ofn)) return false;

    if (!zeronote::SaveTextFileUtf8(fileName, zeronote::WideToUtf8(GetEditText()))) {
        MessageBoxW(hwnd, L"Cannot save the file.", L"UmbraNote", MB_OK | MB_ICONERROR);
        return false;
    }

    g_app.filePath = fileName;
    ClearEncryptionSession();
    SetModified(false);
    UpdateTitle(hwnd);
    return true;
}

bool DoFileSaveEncryptedAs(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = L"";
    if (!g_app.filePath.empty()) {
        wcscpy_s(fileName, g_app.filePath.c_str());
    } else {
        wcscpy_s(fileName, L"Untitled.zro");
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"Encrypted Notes (*.zro)\0*.zro\0"
        L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"zro";

    if (!GetSaveFileNameW(&ofn)) return false;

    PasswordDialogParams auth;
    auth.mode = PasswordDialogMode::Save;
    auth.paranoid_kdf = true;
    if (!PromptPassword(hwnd, auth)) return false;

    std::wstring error;
    if (!SaveEncryptedFile(fileName, GetEditText(), auth, error)) {
        MessageBoxW(hwnd, error.empty() ? L"Cannot save the encrypted file." : error.c_str(),
                    L"UmbraNote", MB_OK | MB_ICONERROR);
        return false;
    }

    g_app.filePath = fileName;
    g_app.fileEncrypted = true;
    g_app.encryptedRequiresKeyfile = !auth.keyfile.empty();
    g_app.encryptedParanoidKdf = auth.paranoid_kdf;
    SetModified(false);
    UpdateTitle(hwnd);
    return true;
}

void DoFileExportPdf(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = L"Untitled.pdf";
    if (!g_app.filePath.empty()) {
        const std::wstring base = g_app.filePath;
        const size_t dot = base.find_last_of(L'.');
        const std::wstring stem = dot == std::wstring::npos ? base : base.substr(0, dot);
        wcscpy_s(fileName, (stem + L".pdf").c_str());
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"pdf";

    if (!GetSaveFileNameW(&ofn)) return;

    std::string error;
    if (!zeronote::pdf::ExportTextToPdf(zeronote::WideToUtf8(GetEditText()),
                                        zeronote::WideToUtf8(fileName), error)) {
        MessageBoxW(hwnd,
                    error.empty() ? L"Cannot export the PDF file."
                                  : zeronote::Utf8ToWide(error).c_str(),
                    L"UmbraNote", MB_OK | MB_ICONERROR);
    }
}

void DoFilePrint(HWND hwnd) {
    MessageBoxW(hwnd,
                L"Printing is not fully implemented in this version.\n"
                L"Export to PDF or use another application to print.",
                L"UmbraNote", MB_OK | MB_ICONINFORMATION);
}

void DoFileExit(HWND hwnd) {
    if (ConfirmSaveIfNeeded(hwnd)) {
        DestroyWindow(hwnd);
    }
}

void DoEditUndo() {
    SendMessageW(g_app.hwndEdit, EM_UNDO, 0, 0);
    SyncModifiedFromEdit();
    UpdateMenuState(g_app.hwndMain);
}

void DoEditCut() {
    SendMessageW(g_app.hwndEdit, WM_CUT, 0, 0);
    SetModified(true);
    UpdateStatusBar();
}

void DoEditCopy() {
    SendMessageW(g_app.hwndEdit, WM_COPY, 0, 0);
}

void DoEditPaste() {
    SendMessageW(g_app.hwndEdit, WM_PASTE, 0, 0);
    SetModified(true);
    UpdateStatusBar();
}

void DoEditDelete() {
    SendMessageW(g_app.hwndEdit, WM_CLEAR, 0, 0);
    SetModified(true);
    UpdateStatusBar();
}

void DoEditFind(HWND hwnd) {
    DialogBoxW(g_app.hInstance, MAKEINTRESOURCEW(IDD_FIND), hwnd, FindDlgProc);
}

void DoEditFindNext(HWND hwnd) {
    if (g_app.findText.empty()) {
        DoEditFind(hwnd);
        return;
    }

    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(g_app.hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

    const std::wstring text = GetEditText();
    int searchFrom = static_cast<int>(end);
    if (start == end && searchFrom < static_cast<int>(text.size())) {
        ++searchFrom;
    }
    int idx = FindText(text, g_app.findText, g_app.findMatchCase, searchFrom);
    if (idx < 0) {
        idx = FindText(text, g_app.findText, g_app.findMatchCase, 0);
    }

    if (idx < 0) {
        MessageBoxW(hwnd, (L"Cannot find \"" + g_app.findText + L"\"").c_str(),
                    L"UmbraNote", MB_OK | MB_ICONINFORMATION);
        return;
    }

    SelectRange(idx, static_cast<int>(g_app.findText.size()));
    UpdateStatusBar();
}

void DoEditReplace(HWND hwnd) {
    DialogBoxW(g_app.hInstance, MAKEINTRESOURCEW(IDD_REPLACE), hwnd, ReplaceDlgProc);
}

void DoEditGoTo(HWND hwnd) {
    if (g_app.wordWrap) return;
    DialogBoxW(g_app.hInstance, MAKEINTRESOURCEW(IDD_GOTO), hwnd, GoToDlgProc);
}

void DoEditSelectAll() {
    SendMessageW(g_app.hwndEdit, EM_SETSEL, 0, -1);
    SetFocus(g_app.hwndEdit);
}

void DoFormatWordWrap(HWND hwnd) {
    g_app.wordWrap = !g_app.wordWrap;
    ApplyWordWrap(hwnd);
}

void DoFormatFont(HWND hwnd) {
    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd;
    cf.lpLogFont = &g_app.lf;
    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS;
    cf.rgbColors = RGB(0, 0, 0);

    if (ChooseFontW(&cf)) {
        ApplyFont();
    }
}

INT_PTR CALLBACK FindDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            ApplyUiFontToDialog(hwnd);
            SetWindowTextString(GetDlgItem(hwnd, IDC_FINDWHAT), g_app.findText);
            CheckDlgButton(hwnd, IDC_MATCHCASE, g_app.findMatchCase ? BST_CHECKED : BST_UNCHECKED);
            SendDlgItemMessageW(hwnd, IDC_FINDWHAT, EM_SETSEL, 0, -1);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    g_app.findText = GetWindowTextString(GetDlgItem(hwnd, IDC_FINDWHAT));
                    g_app.findMatchCase = IsDlgButtonChecked(hwnd, IDC_MATCHCASE) == BST_CHECKED;
                    UpdateMenuState(g_app.hwndMain);

                    const std::wstring text = GetEditText();
                    const int idx = FindText(text, g_app.findText, g_app.findMatchCase, 0);
                    if (idx < 0) {
                        MessageBoxW(hwnd, (L"Cannot find \"" + g_app.findText + L"\"").c_str(),
                                    L"UmbraNote", MB_OK | MB_ICONINFORMATION);
                        return TRUE;
                    }
                    SelectRange(idx, static_cast<int>(g_app.findText.size()));
                    UpdateStatusBar();
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK ReplaceDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            ApplyUiFontToDialog(hwnd);
            SetWindowTextString(GetDlgItem(hwnd, IDC_REPLACEWHAT), g_app.findText);
            CheckDlgButton(hwnd, IDC_MATCHCASE, g_app.findMatchCase ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    const std::wstring search = GetWindowTextString(GetDlgItem(hwnd, IDC_REPLACEWHAT));
                    const std::wstring replace = GetWindowTextString(GetDlgItem(hwnd, IDC_REPLACEWITH));
                    const bool matchCase = IsDlgButtonChecked(hwnd, IDC_MATCHCASE) == BST_CHECKED;
                    g_app.findText = search;
                    g_app.findMatchCase = matchCase;
                    UpdateMenuState(g_app.hwndMain);

                    DWORD start = 0;
                    DWORD end = 0;
                    SendMessageW(g_app.hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
                    std::wstring text = GetEditText();
                    int idx = FindText(text, search, matchCase, static_cast<int>(end));
                    if (idx < 0) {
                        idx = FindText(text, search, matchCase, 0);
                    }
                    if (idx < 0) {
                        MessageBoxW(hwnd, (L"Cannot find \"" + search + L"\"").c_str(),
                                    L"UmbraNote", MB_OK | MB_ICONINFORMATION);
                        return TRUE;
                    }
                    text.replace(static_cast<size_t>(idx), search.size(), replace);
                    SetEditText(text);
                    SelectRange(idx, static_cast<int>(replace.size()));
                    SetModified(true);
                    UpdateStatusBar();
                    return TRUE;
                }
                case 1001: {
                    const std::wstring search = GetWindowTextString(GetDlgItem(hwnd, IDC_REPLACEWHAT));
                    const std::wstring replace = GetWindowTextString(GetDlgItem(hwnd, IDC_REPLACEWITH));
                    const bool matchCase = IsDlgButtonChecked(hwnd, IDC_MATCHCASE) == BST_CHECKED;
                    if (search.empty()) return TRUE;

                    g_app.findText = search;
                    g_app.findMatchCase = matchCase;
                    UpdateMenuState(g_app.hwndMain);

                    std::wstring text = GetEditText();
                    int count = 0;
                    int pos = 0;
                    while (true) {
                        const int idx = FindText(text, search, matchCase, pos);
                        if (idx < 0) break;
                        text.replace(static_cast<size_t>(idx), search.size(), replace);
                        pos = idx + static_cast<int>(replace.size());
                        ++count;
                    }

                    if (count == 0) {
                        MessageBoxW(hwnd, (L"Cannot find \"" + search + L"\"").c_str(),
                                    L"UmbraNote", MB_OK | MB_ICONINFORMATION);
                    } else {
                        SetEditText(text);
                        SetModified(true);
                        wchar_t info[64];
                        swprintf_s(info, L"Replaced %d occurrence(s).", count);
                        MessageBoxW(hwnd, info, L"UmbraNote", MB_OK | MB_ICONINFORMATION);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK GoToDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            ApplyUiFontToDialog(hwnd);
            DWORD start = 0;
            DWORD end = 0;
            SendMessageW(g_app.hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
            const std::wstring text = GetEditText();
            const std::wstring before = text.substr(0, start);
            const int line = static_cast<int>(std::count(before.begin(), before.end(), L'\n')) + 1;
            SetDlgItemInt(hwnd, IDC_GOTO_LINE, line, FALSE);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    const int lineNum = GetDlgItemInt(hwnd, IDC_GOTO_LINE, nullptr, FALSE);
                    const std::wstring text = GetEditText();
                    int currentLine = 1;
                    int pos = 0;
                    for (size_t i = 0; i < text.size(); ++i) {
                        if (currentLine == lineNum) {
                            SelectRange(pos, 0);
                            EndDialog(hwnd, IDOK);
                            return TRUE;
                        }
                        if (text[i] == L'\n') {
                            ++currentLine;
                            pos = static_cast<int>(i + 1);
                        }
                    }
                    if (currentLine == lineNum) {
                        SelectRange(pos, 0);
                        EndDialog(hwnd, IDOK);
                        return TRUE;
                    }
                    MessageBoxW(hwnd, L"Line number out of range.", L"UmbraNote",
                                MB_OK | MB_ICONINFORMATION);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK PasswordDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* params = reinterpret_cast<PasswordDialogParams*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(params));
            if (params && params->mode == PasswordDialogMode::Open) {
                SetWindowTextW(hwnd, L"Unlock Protected Note");
                ShowWindow(GetDlgItem(hwnd, IDC_PASSWORD_CONFIRM_LABEL), SW_HIDE);
                ShowWindow(GetDlgItem(hwnd, IDC_PASSWORD_CONFIRM), SW_HIDE);
            }
            if (params) {
                CheckDlgButton(hwnd, IDC_PARANOID_KDF,
                               params->paranoid_kdf ? BST_CHECKED : BST_UNCHECKED);
                if (params->mode == PasswordDialogMode::Open) {
                    EnableWindow(GetDlgItem(hwnd, IDC_PARANOID_KDF), FALSE);
                }
                if (!params->require_keyfile) {
                    SetWindowTextW(GetDlgItem(hwnd, IDC_KEYFILE_LABEL), L"Keyfile (optional):");
                }
            }
            ApplyUiFontToDialog(hwnd);
            SendDlgItemMessageW(hwnd, IDC_PASSWORD, EM_SETSEL, 0, -1);
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_KEYFILE_BROWSE: {
                    wchar_t fileName[MAX_PATH] = L"";
                    OPENFILENAMEW ofn{};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Keyfiles (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
                    if (GetOpenFileNameW(&ofn)) {
                        SetWindowTextW(GetDlgItem(hwnd, IDC_KEYFILE_PATH), fileName);
                    }
                    return TRUE;
                }
                case IDOK: {
                    auto* params = reinterpret_cast<PasswordDialogParams*>(
                        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                    const std::wstring password = GetWindowTextString(GetDlgItem(hwnd, IDC_PASSWORD));
                    if (password.empty()) {
                        MessageBoxW(hwnd, L"Password cannot be empty.", L"UmbraNote",
                                    MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    if (params && params->mode == PasswordDialogMode::Save) {
                        const std::wstring confirm =
                            GetWindowTextString(GetDlgItem(hwnd, IDC_PASSWORD_CONFIRM));
                        if (password != confirm) {
                            MessageBoxW(hwnd, L"Passwords do not match.", L"UmbraNote",
                                        MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }
                    }

                    const std::wstring keyfilePath =
                        GetWindowTextString(GetDlgItem(hwnd, IDC_KEYFILE_PATH));
                    const bool paranoid =
                        IsDlgButtonChecked(hwnd, IDC_PARANOID_KDF) == BST_CHECKED;
                    std::vector<uint8_t> keyfile;
                    if (!keyfilePath.empty()) {
                        std::wstring keyfileError;
                        if (!LoadKeyfileBytes(keyfilePath, keyfile, keyfileError)) {
                            MessageBoxW(hwnd, keyfileError.c_str(), L"UmbraNote",
                                        MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }
                    }
                    if (paranoid && keyfile.empty()) {
                        MessageBoxW(hwnd,
                                    L"High-security mode requires a keyfile in addition to the password.",
                                    L"UmbraNote", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    if (params && params->require_keyfile && keyfile.empty()) {
                        MessageBoxW(hwnd, L"This file requires the original keyfile.",
                                    L"UmbraNote", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    std::string passwordError;
                    if (!zeronote::crypto::ValidateEncryptionPassword(
                            zeronote::WideToUtf8(password), paranoid, passwordError) &&
                        params && params->mode == PasswordDialogMode::Save) {
                        MessageBoxW(hwnd, zeronote::Utf8ToWide(passwordError).c_str(),
                                    L"UmbraNote", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    if (params) {
                        params->password = password;
                        params->keyfile = std::move(keyfile);
                        params->keyfilePath = keyfilePath;
                        params->paranoid_kdf = paranoid;
                    }
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwndMain = hwnd;

            INITCOMMONCONTROLSEX icc{};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icc);

            g_app.hEditBrush = CreateSolidBrush(kEditorBgColor);
            g_app.hUiFont = CreateFontW(
                -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            g_app.hwndStatus = CreateWindowExW(
                0, STATUSCLASSNAMEW, nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUS),
                g_app.hInstance, nullptr);
            if (g_app.hUiFont) {
                SendMessageW(g_app.hwndStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.hUiFont), TRUE);
            }

            InitDefaultEditorLogFont(hwnd);
            g_app.hwndEdit = CreateEditorWindow(hwnd);
            ApplyFont();
            ApplyModernChrome(hwnd);

            CheckMenuItemById(hwnd, IDM_FORMAT_WORDWRAP, g_app.wordWrap);
            ResizeControls(hwnd);
            UpdateTitle(hwnd);
            UpdateMenuState(hwnd);
            UpdateStatusBar();
            return 0;
        }

        case WM_SIZE:
            ResizeControls(hwnd);
            return 0;

        case WM_SETFOCUS:
            SetFocus(g_app.hwndEdit);
            return 0;

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, kEditorBgColor);
            SetTextColor(hdc, RGB(0, 0, 0));
            return reinterpret_cast<LRESULT>(g_app.hEditBrush);
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_3DFACE));
        }

        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_CHANGE &&
                reinterpret_cast<HWND>(lParam) == g_app.hwndEdit) {
                SyncModifiedFromEdit();
                UpdateStatusBar();
                return 0;
            }
            if (HIWORD(wParam) == EN_UPDATE &&
                reinterpret_cast<HWND>(lParam) == g_app.hwndEdit) {
                UpdateMenuState(hwnd);
                return 0;
            }

            switch (LOWORD(wParam)) {
                case IDM_FILE_NEW: DoFileNew(hwnd); break;
                case IDM_FILE_OPEN: DoFileOpen(hwnd); break;
                case IDM_FILE_SAVE: DoFileSave(hwnd); break;
                case IDM_FILE_SAVEAS: DoFileSaveAs(hwnd); break;
                case IDM_FILE_SAVEENCRYPTED: DoFileSaveEncryptedAs(hwnd); break;
                case IDM_FILE_EXPORTPDF: DoFileExportPdf(hwnd); break;
                case IDM_FILE_PRINT: DoFilePrint(hwnd); break;
                case IDM_FILE_EXIT: DoFileExit(hwnd); break;
                case IDM_EDIT_UNDO: DoEditUndo(); break;
                case IDM_EDIT_CUT: DoEditCut(); break;
                case IDM_EDIT_COPY: DoEditCopy(); break;
                case IDM_EDIT_PASTE: DoEditPaste(); break;
                case IDM_EDIT_DELETE: DoEditDelete(); break;
                case IDM_EDIT_FIND: DoEditFind(hwnd); break;
                case IDM_EDIT_FINDNEXT: DoEditFindNext(hwnd); break;
                case IDM_EDIT_REPLACE: DoEditReplace(hwnd); break;
                case IDM_EDIT_GOTO: DoEditGoTo(hwnd); break;
                case IDM_EDIT_SELECTALL: DoEditSelectAll(); break;
                case IDM_FORMAT_WORDWRAP: DoFormatWordWrap(hwnd); break;
                case IDM_FORMAT_FONT: DoFormatFont(hwnd); break;
            }
            return 0;
        }

        case WM_QUERYENDSESSION:
            if (!ConfirmSaveIfNeeded(hwnd)) {
                return FALSE;
            }
            return TRUE;

        case WM_CLOSE:
            DoFileExit(hwnd);
            return 0;

        case WM_DESTROY:
            ClearEncryptionSession();
            if (g_app.hFont) DeleteObject(g_app.hFont);
            if (g_app.hUiFont) DeleteObject(g_app.hUiFont);
            if (g_app.hEditBrush) DeleteObject(g_app.hEditBrush);
            g_app.hFont = nullptr;
            g_app.hUiFont = nullptr;
            g_app.hEditBrush = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}