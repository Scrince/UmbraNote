#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <array>
#include <string>

struct AppState {
    HINSTANCE hInstance = nullptr;
    HWND hwndMain = nullptr;
    HWND hwndMenuBar = nullptr;
    HWND hwndEdit = nullptr;
    HWND hwndStatus = nullptr;
    HMENU hMenu = nullptr;
    HFONT hFont = nullptr;
    HFONT hUiFont = nullptr;
    HBRUSH hEditBrush = nullptr;
    HBRUSH hStatusBrush = nullptr;
    HBRUSH hMenuBrush = nullptr;
    COLORREF editorBg = RGB(255, 255, 255);
    COLORREF editorFg = RGB(0, 0, 0);
    int editorPointSize = 11;
    bool darkMode = false;
    LOGFONTW lf{};
    std::wstring filePath;
    bool modified = false;
    bool wordWrap = true;
    std::wstring findText;
    bool findMatchCase = false;
    bool fileEncrypted = false;
    bool encryptedRequiresKeyfile = false;
    bool encryptedParanoidKdf = true;
    HACCEL hAccel = nullptr;
    std::array<int, 6> statusPartRight{};
    std::array<std::wstring, 6> statusText{};
};

extern AppState g_app;

void InitAppTheming();
void ApplyAppTheme(HWND hwnd);
void ApplyThemeToDialog(HWND hwnd);

void UpdateTitle(HWND hwnd);
void UpdateMenuState(HWND hwnd);
void UpdateStatusBar();
void ResizeControls(HWND hwnd);
void ApplyFont();
void ApplyWordWrap(HWND hwnd);
void SetModified(bool modified);
void SyncModifiedFromEdit();

bool ConfirmSaveIfNeeded(HWND hwnd);
void DoFileNew(HWND hwnd);
bool OpenFilePath(HWND hwnd, const std::wstring& path);
void DoFileOpen(HWND hwnd);
bool DoFileSave(HWND hwnd);
bool DoFileSaveAs(HWND hwnd);
bool DoFileSaveEncryptedAs(HWND hwnd);
void DoFileExportPdf(HWND hwnd);
void DoFilePrint(HWND hwnd);
void ClearEncryptionSession();
void DoFileExit(HWND hwnd);

void DoEditUndo();
void DoEditCut();
void DoEditCopy();
void DoEditPaste();
void DoEditDelete();
void DoEditFind(HWND hwnd);
void DoEditFindNext(HWND hwnd);
void DoEditReplace(HWND hwnd);
void DoEditGoTo(HWND hwnd);
void DoEditSelectAll();

void DoFormatWordWrap(HWND hwnd);
void DoFormatFont(HWND hwnd);

INT_PTR CALLBACK FindDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ReplaceDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK GoToDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK PasswordDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
