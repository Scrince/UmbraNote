#include "notepad.h"
#include "resource.h"

#include <zeronote/crypto.h>
#include <zeronote/pdf_export.h>
#include <zeronote/text_codec.h>

#include <algorithm>
#include <iterator>
#include <vector>

#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

AppState g_app;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef MIM_BACKGROUND
#define MIM_BACKGROUND 0x00000004
#endif
#ifndef MIM_APPLYTOSUBMENUS
#define MIM_APPLYTOSUBMENUS 0x80000000
#endif
#ifndef MIM_STYLE
#define MIM_STYLE 0x00000010
#endif
#ifndef MNS_MODE_BACKGROUNDS
#define MNS_MODE_BACKGROUNDS 0x04000000
#endif

constexpr COLORREF kLightEditorBg = RGB(255, 255, 255);
constexpr COLORREF kLightEditorFg = RGB(0, 0, 0);
constexpr COLORREF kLightChromeBorder = RGB(225, 225, 225);
constexpr COLORREF kLightStatusBg = RGB(240, 240, 240);
constexpr COLORREF kLightTitleBarBg = RGB(255, 255, 255);
constexpr COLORREF kLightTitleBarFg = RGB(0, 0, 0);

constexpr COLORREF kDarkMenuBarBg = RGB(31, 31, 31);
constexpr COLORREF kDarkMenuBarHover = RGB(52, 52, 52);
constexpr COLORREF kDarkMenuBarFg = RGB(255, 255, 255);
constexpr COLORREF kDarkTitleBarBg = RGB(24, 24, 24);
constexpr COLORREF kDarkEditorBg = RGB(28, 28, 28);
constexpr COLORREF kDarkEditorFg = RGB(255, 255, 255);
constexpr COLORREF kDarkChromeBorder = RGB(32, 32, 32);
constexpr COLORREF kDarkStatusBg = RGB(31, 31, 31);
constexpr const wchar_t* kMenuBarClassName = L"UmbraNoteMenuBar";
constexpr const wchar_t* kStatusBarClassName = L"UmbraNoteStatusBar";

struct UxThemeApi {
    HMODULE module = nullptr;
    BOOL(WINAPI* allow_dark_mode_for_window)(HWND, BOOL) = nullptr;
    BOOL(WINAPI* set_preferred_app_mode)(int) = nullptr;
    BOOL(WINAPI* should_apps_use_dark_mode)() = nullptr;
    BOOL(WINAPI* flush_menu_themes)() = nullptr;
};

UxThemeApi g_uxtheme;

bool IsSystemDarkModeEnabled() {
    if (g_uxtheme.should_apps_use_dark_mode) {
        return g_uxtheme.should_apps_use_dark_mode() != FALSE;
    }

    DWORD useLightTheme = 1;
    DWORD size = sizeof(useLightTheme);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &useLightTheme, &size);
    if (status == ERROR_SUCCESS) {
        return useLightTheme == 0;
    }
    return false;
}

void UpdateThemeBrushes() {
    if (g_app.hEditBrush) {
        DeleteObject(g_app.hEditBrush);
        g_app.hEditBrush = nullptr;
    }
    if (g_app.hStatusBrush) {
        DeleteObject(g_app.hStatusBrush);
        g_app.hStatusBrush = nullptr;
    }
    if (g_app.hMenuBrush) {
        DeleteObject(g_app.hMenuBrush);
        g_app.hMenuBrush = nullptr;
    }

    g_app.editorBg = g_app.darkMode ? kDarkEditorBg : kLightEditorBg;
    g_app.editorFg = g_app.darkMode ? kDarkEditorFg : kLightEditorFg;
    g_app.hEditBrush = CreateSolidBrush(g_app.editorBg);
    const COLORREF statusBg = g_app.darkMode ? kDarkStatusBg : kLightStatusBg;
    g_app.hStatusBrush = CreateSolidBrush(statusBg);
    if (g_app.darkMode) {
        g_app.hMenuBrush = CreateSolidBrush(kDarkMenuBarBg);
    }
}

void RestoreStandardMenuBar(HWND hwnd) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) {
        return;
    }

    const int itemCount = GetMenuItemCount(menu);
    for (int i = 0; i < itemCount; ++i) {
        wchar_t label[64] = {};
        GetMenuStringW(menu, i, label, static_cast<int>(std::size(label)), MF_BYPOSITION);

        MENUITEMINFOW itemInfo{};
        itemInfo.cbSize = sizeof(MENUITEMINFOW);
        itemInfo.fMask = MIIM_TYPE | MIIM_DATA | MIIM_SUBMENU | MIIM_ID | MIIM_STATE;
        GetMenuItemInfoW(menu, i, MF_BYPOSITION, &itemInfo);

        itemInfo.fType = MFT_STRING;
        itemInfo.fMask = MIIM_TYPE | MIIM_DATA | MIIM_SUBMENU | MIIM_ID | MIIM_STATE;
        itemInfo.dwTypeData = label;
        SetMenuItemInfoW(menu, i, MF_BYPOSITION, &itemInfo);
    }

    MENUINFO menuInfo{};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS | MIM_STYLE;
    menuInfo.dwStyle = 0;
    menuInfo.hbrBack = nullptr;
    SetMenuInfo(menu, &menuInfo);
    DrawMenuBar(hwnd);
}

void SetupOwnerDrawMenuBar(HWND hwnd) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) {
        return;
    }

    const int itemCount = GetMenuItemCount(menu);
    for (int i = 0; i < itemCount; ++i) {
        MENUITEMINFOW itemInfo{};
        itemInfo.cbSize = sizeof(MENUITEMINFOW);
        itemInfo.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(menu, i, MF_BYPOSITION, &itemInfo) || !itemInfo.hSubMenu) {
            continue;
        }

        itemInfo.fMask = MIIM_FTYPE | MIIM_DATA;
        itemInfo.fType = MFT_OWNERDRAW;
        itemInfo.dwItemData = static_cast<ULONG_PTR>(i);
        SetMenuItemInfoW(menu, i, MF_BYPOSITION, &itemInfo);
    }

    DrawMenuBar(hwnd);
}

void ApplyMenuTheme(HWND hwnd) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) {
        return;
    }

    if (g_app.darkMode) {
        SetupOwnerDrawMenuBar(hwnd);
    } else {
        RestoreStandardMenuBar(hwnd);
        return;
    }

    MENUINFO menuInfo{};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS | MIM_STYLE;
    menuInfo.dwStyle = MNS_MODE_BACKGROUNDS;
    menuInfo.hbrBack = g_app.hMenuBrush;
    SetMenuInfo(menu, &menuInfo);
    DrawMenuBar(hwnd);
}

void MeasureOwnerDrawMenuItem(HWND hwnd, MEASUREITEMSTRUCT* measureInfo) {
    wchar_t label[64] = {};
    GetMenuStringW(GetMenu(hwnd), static_cast<int>(measureInfo->itemData), label,
                   static_cast<int>(std::size(label)), MF_BYPOSITION);

    const HDC hdc = GetDC(hwnd);
    const HFONT oldFont = static_cast<HFONT>(SelectObject(
        hdc, g_app.hUiFont ? g_app.hUiFont : GetStockObject(DEFAULT_GUI_FONT)));
    SIZE textSize{};
    GetTextExtentPoint32W(hdc, label, static_cast<int>(wcslen(label)), &textSize);
    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);

    measureInfo->itemHeight = 24;
    measureInfo->itemWidth = textSize.cx + 28;
}

void DrawOwnerDrawMenuItem(HWND hwnd, DRAWITEMSTRUCT* drawInfo) {
    wchar_t label[64] = {};
    GetMenuStringW(GetMenu(hwnd), static_cast<int>(drawInfo->itemData), label,
                   static_cast<int>(std::size(label)), MF_BYPOSITION);

    const bool selected = (drawInfo->itemState & ODS_SELECTED) != 0;
    const COLORREF background = selected ? kDarkMenuBarHover : kDarkMenuBarBg;
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(drawInfo->hDC, &drawInfo->rcItem, brush);
    DeleteObject(brush);

    SetBkMode(drawInfo->hDC, TRANSPARENT);
    SetTextColor(drawInfo->hDC, kDarkMenuBarFg);
    const HFONT oldFont = static_cast<HFONT>(SelectObject(
        drawInfo->hDC, g_app.hUiFont ? g_app.hUiFont : GetStockObject(DEFAULT_GUI_FONT)));
    DrawTextW(drawInfo->hDC, label, -1, &drawInfo->rcItem,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SelectObject(drawInfo->hDC, oldFont);
}

void ApplyDwmWindowColors(HWND hwnd, bool enabled) {
    const BOOL useDark = enabled ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &useDark,
                          sizeof(useDark));

    const COLORREF border = enabled ? kDarkChromeBorder : kLightChromeBorder;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));

    const COLORREF captionBg = enabled ? kDarkTitleBarBg : kLightTitleBarBg;
    const COLORREF captionFg = enabled ? kDarkEditorFg : kLightTitleBarFg;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionBg, sizeof(captionBg));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &captionFg, sizeof(captionFg));
}

void ApplyDarkModeToWindow(HWND hwnd, bool enabled) {
    if (!hwnd) return;

    if (g_uxtheme.allow_dark_mode_for_window) {
        g_uxtheme.allow_dark_mode_for_window(hwnd, enabled ? TRUE : FALSE);
    }

    SetWindowTheme(hwnd, enabled ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ApplyDwmWindowColors(hwnd, enabled);
}

LRESULT ThemedEditColorResult(HWND ctrl, HDC hdc) {
    if (!g_app.hEditBrush) {
        return 0;
    }

    wchar_t className[16] = {};
    GetClassNameW(ctrl, className, static_cast<int>(std::size(className)));
    if (_wcsicmp(className, L"Edit") != 0) {
        return 0;
    }

    SetBkColor(hdc, g_app.editorBg);
    SetTextColor(hdc, g_app.editorFg);
    return reinterpret_cast<LRESULT>(g_app.hEditBrush);
}

int MenuItemWidth(int index) {
    constexpr int widths[] = {72, 72, 96};
    return widths[index];
}

RECT MenuItemRect(int index) {
    RECT rc{0, 0, 0, 28};
    for (int i = 0; i < index; ++i) {
        rc.left += MenuItemWidth(i);
    }
    rc.right = rc.left + MenuItemWidth(index);
    return rc;
}

int HitTestMenuBar(int x, int y) {
    if (y < 0 || y >= 28) {
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        const RECT rc = MenuItemRect(i);
        if (x >= rc.left && x < rc.right) {
            return i;
        }
    }
    return -1;
}

void ShowMenuBarPopup(HWND hwndMenuBar, int index) {
    if (!g_app.hMenu || index < 0) {
        return;
    }

    UpdateMenuState(g_app.hwndMain);

    RECT rc = MenuItemRect(index);
    POINT pt{rc.left, rc.bottom};
    ClientToScreen(hwndMenuBar, &pt);

    HMENU popup = GetSubMenu(g_app.hMenu, index);
    if (!popup) {
        return;
    }

    const UINT command = TrackPopupMenu(
        popup,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
        pt.x, pt.y, 0, g_app.hwndMain, nullptr);
    if (command != 0) {
        SendMessageW(g_app.hwndMain, WM_COMMAND, command, 0);
    }
}

LRESULT CALLBACK MenuBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int hovered = -1;

    switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            const int hit = HitTestMenuBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (hovered != hit) {
                hovered = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            hovered = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            const int hit = HitTestMenuBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (hit >= 0) {
                hovered = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
                ShowMenuBarPopup(hwnd, hit);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            HBRUSH background = CreateSolidBrush(kDarkMenuBarBg);
            FillRect(hdc, &client, background);
            DeleteObject(background);

            const wchar_t* labels[] = {L"File", L"Edit", L"Format"};
            const HFONT oldFont = static_cast<HFONT>(SelectObject(
                hdc, g_app.hUiFont ? g_app.hUiFont : GetStockObject(DEFAULT_GUI_FONT)));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kDarkMenuBarFg);

            for (int i = 0; i < 3; ++i) {
                RECT item = MenuItemRect(i);
                if (i == hovered) {
                    HBRUSH hoverBrush = CreateSolidBrush(kDarkMenuBarHover);
                    FillRect(hdc, &item, hoverBrush);
                    DeleteObject(hoverBrush);
                }
                RECT textRect = item;
                textRect.left += 26;
                DrawTextW(hdc, labels[i], -1, &textRect,
                          DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
            }

            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            HBRUSH background = CreateSolidBrush(kDarkStatusBg);
            FillRect(hdc, &client, background);
            DeleteObject(background);

            const HPEN separatorPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 45));
            const HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, separatorPen));
            const HFONT oldFont = static_cast<HFONT>(SelectObject(
                hdc, g_app.hUiFont ? g_app.hUiFont : GetStockObject(DEFAULT_GUI_FONT)));

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(222, 222, 222));

            int left = 0;
            for (size_t i = 0; i < g_app.statusPartRight.size(); ++i) {
                int right = g_app.statusPartRight[i];
                if (right < 0) {
                    right = client.right;
                }
                if (right <= left) {
                    continue;
                }

                if (i > 0) {
                    MoveToEx(hdc, left, 8, nullptr);
                    LineTo(hdc, left, client.bottom - 6);
                }

                RECT textRect{left + 12, 0, right - 8, client.bottom};
                DrawTextW(hdc, g_app.statusText[i].c_str(), -1, &textRect,
                          DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
                left = right;
            }

            SelectObject(hdc, oldFont);
            SelectObject(hdc, oldPen);
            DeleteObject(separatorPen);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

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
constexpr int kDefaultEditorPointSize = 11;
constexpr int kMinEditorPointSize = 8;
constexpr int kMaxEditorPointSize = 72;
constexpr UINT_PTR kEditorSubclassId = 1;

int PointsToLogFontHeight(HWND hwnd, int pointSize) {
    const HDC hdc = GetDC(hwnd);
    const int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);
    return -MulDiv(pointSize, logPixelsY, 72);
}

void InitDefaultEditorLogFont(HWND hwnd) {
    g_app.lf = LOGFONTW{};
    g_app.editorPointSize = kDefaultEditorPointSize;
    g_app.lf.lfHeight = PointsToLogFontHeight(hwnd, g_app.editorPointSize);
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

void SetStatusBarParts(HWND hwnd) {
    if (!g_app.hwndStatus) return;

    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int width = rc.right - rc.left;
    const int encodingWidth = 150;
    const int lineEndingWidth = 210;
    const int zoomWidth = 90;
    const int plainTextWidth = 240;
    const int charWidth = 260;
    const int cursorWidth = 120;

    int parts[6]{};
    parts[5] = -1;
    parts[4] = width - encodingWidth;
    parts[3] = parts[4] - lineEndingWidth;
    parts[2] = parts[3] - zoomWidth;
    parts[1] = cursorWidth + charWidth;
    parts[0] = cursorWidth;

    const int minimumMiddle = parts[1] + plainTextWidth;
    if (parts[2] < minimumMiddle) {
        parts[1] = std::min(parts[1], width / 2);
        parts[2] = std::max(parts[1] + 120, parts[2]);
    }

    for (size_t i = 0; i < g_app.statusPartRight.size(); ++i) {
        g_app.statusPartRight[i] = parts[i];
    }
    InvalidateRect(g_app.hwndStatus, nullptr, FALSE);
}

int CountDisplayCharacters(const std::wstring& text) {
    return static_cast<int>(std::count_if(text.begin(), text.end(), [](wchar_t ch) {
        return ch != L'\r';
    }));
}

void ApplyEditorPointSize(HWND hwnd, int pointSize) {
    g_app.editorPointSize = std::max(kMinEditorPointSize,
                                     std::min(kMaxEditorPointSize, pointSize));
    g_app.lf.lfHeight = PointsToLogFontHeight(hwnd, g_app.editorPointSize);
    ApplyFont();
    UpdateStatusBar();
}

LRESULT CALLBACK EditorSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR subclassId, DWORD_PTR refData) {
    UNREFERENCED_PARAMETER(subclassId);
    UNREFERENCED_PARAMETER(refData);

    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta != 0) {
            ApplyEditorPointSize(GetParent(hwnd),
                                 g_app.editorPointSize + (delta > 0 ? 1 : -1));
        }
        return 0;
    }

    const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_KEYDOWN:
        case WM_LBUTTONUP:
        case WM_SETFOCUS:
            UpdateStatusBar();
            break;
        case WM_MOUSEMOVE:
            if ((wParam & MK_LBUTTON) != 0) {
                UpdateStatusBar();
            }
            break;
    }
    return result;
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
    SetWindowSubclass(hwndEdit, EditorSubclassProc, kEditorSubclassId, 0);
    return hwndEdit;
}

BOOL CALLBACK SetChildFontProc(HWND child, LPARAM lParam) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
    return TRUE;
}

void ApplyUiFontToDialog(HWND hwnd) {
    ApplyThemeToDialog(hwnd);
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
    return zeronote::WriteFileBytesAtomic(path, encrypted);
}

bool VerifyExistingEncryptedFile(const std::wstring& path, const PasswordDialogParams& auth,
                                 std::wstring& error) {
    std::vector<uint8_t> bytes;
    if (!zeronote::ReadFileBytes(path, bytes)) {
        error = L"Cannot read the encrypted file before saving.";
        return false;
    }
    std::string plaintext;
    std::string err;
    if (!zeronote::crypto::DecryptText(bytes, zeronote::WideToUtf8(auth.password),
                                       plaintext, err, BuildEncryptionOptions(auth))) {
        error = zeronote::Utf8ToWide(err.empty()
                                         ? "Password or keyfile does not match this file."
                                         : err);
        return false;
    }
    return true;
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
    HMENU menu = g_app.hMenu ? g_app.hMenu : GetMenu(hwnd);
    if (!menu) return;
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

void EnableMenuItemById(HWND hwnd, UINT id, bool enabled) {
    HMENU menu = g_app.hMenu ? g_app.hMenu : GetMenu(hwnd);
    if (!menu) return;
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

}

void InitAppTheming() {
    g_uxtheme.module = LoadLibraryW(L"uxtheme.dll");
    if (!g_uxtheme.module) {
        return;
    }

    g_uxtheme.allow_dark_mode_for_window = reinterpret_cast<BOOL(WINAPI*)(HWND, BOOL)>(
        GetProcAddress(g_uxtheme.module, MAKEINTRESOURCEA(133)));
    g_uxtheme.set_preferred_app_mode = reinterpret_cast<BOOL(WINAPI*)(int)>(
        GetProcAddress(g_uxtheme.module, MAKEINTRESOURCEA(135)));
    g_uxtheme.should_apps_use_dark_mode = reinterpret_cast<BOOL(WINAPI*)()>(
        GetProcAddress(g_uxtheme.module, MAKEINTRESOURCEA(132)));
    g_uxtheme.flush_menu_themes = reinterpret_cast<BOOL(WINAPI*)()>(
        GetProcAddress(g_uxtheme.module, MAKEINTRESOURCEA(136)));

    if (g_uxtheme.set_preferred_app_mode) {
        g_uxtheme.set_preferred_app_mode(2);
    }
}

void ApplyAppTheme(HWND hwnd) {
    g_app.darkMode = IsSystemDarkModeEnabled();
    UpdateThemeBrushes();

    ApplyDwmWindowColors(hwnd, g_app.darkMode);

    const int roundCorners = 2;
    DwmSetWindowAttribute(hwnd, 33, &roundCorners, sizeof(roundCorners));

    if (g_uxtheme.flush_menu_themes) {
        g_uxtheme.flush_menu_themes();
    }

    ApplyDarkModeToWindow(hwnd, g_app.darkMode);
    ApplyMenuTheme(hwnd);

    if (g_app.hwndStatus) {
        InvalidateRect(g_app.hwndStatus, nullptr, TRUE);
    }
    if (g_app.hwndEdit) {
        ApplyDarkModeToWindow(g_app.hwndEdit, g_app.darkMode);
        InvalidateRect(g_app.hwndEdit, nullptr, TRUE);
    }
    if (g_app.hwndMenuBar) {
        InvalidateRect(g_app.hwndMenuBar, nullptr, TRUE);
    }

    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void ApplyThemeToDialog(HWND hwnd) {
    ApplyDarkModeToWindow(hwnd, g_app.darkMode);
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
    if (!g_app.hwndStatus) return;

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

    wchar_t cursorStatus[64];
    swprintf_s(cursorStatus, L"  Ln %d, Col %d", line, col);

    wchar_t characterStatus[64];
    swprintf_s(characterStatus, L"  %d characters", CountDisplayCharacters(text));

    wchar_t zoomStatus[32];
    swprintf_s(zoomStatus, L"  %d%%", MulDiv(g_app.editorPointSize, 100,
                                             kDefaultEditorPointSize));

    g_app.statusText[0] = cursorStatus;
    g_app.statusText[1] = characterStatus;
    g_app.statusText[2] = L"Plain text";
    g_app.statusText[3] = zoomStatus;
    g_app.statusText[4] = L"Windows (CRLF)";
    g_app.statusText[5] = L"UTF-8";
    InvalidateRect(g_app.hwndStatus, nullptr, FALSE);
}

void ResizeControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);

    const int menuHeight = 28;
    const int statusHeight = kStatusHeight;
    if (g_app.hwndMenuBar) {
        SetWindowPos(g_app.hwndMenuBar, nullptr, 0, 0, rc.right, menuHeight, SWP_NOZORDER);
    }
    if (g_app.hwndStatus) {
        ShowWindow(g_app.hwndStatus, SW_SHOW);
        SetWindowPos(g_app.hwndStatus, nullptr, 0, rc.bottom - statusHeight,
                     rc.right, statusHeight, SWP_NOZORDER);
        SetStatusBarParts(hwnd);
    }

    if (g_app.hwndEdit) {
        SetWindowPos(g_app.hwndEdit, nullptr, 0, menuHeight, rc.right,
                     rc.bottom - statusHeight - menuHeight, SWP_NOZORDER);
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
        if (!VerifyExistingEncryptedFile(g_app.filePath, auth, error)) {
            MessageBoxW(hwnd, error.empty() ? L"Password or keyfile does not match this file."
                                            : error.c_str(),
                        L"UmbraNote", MB_OK | MB_ICONERROR);
            return false;
        }
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

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const LRESULT result = ThemedEditColorResult(reinterpret_cast<HWND>(lParam),
                                                         reinterpret_cast<HDC>(wParam));
            if (result) {
                return result;
            }
            break;
        }
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

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const LRESULT result = ThemedEditColorResult(reinterpret_cast<HWND>(lParam),
                                                         reinterpret_cast<HDC>(wParam));
            if (result) {
                return result;
            }
            break;
        }
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

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const LRESULT result = ThemedEditColorResult(reinterpret_cast<HWND>(lParam),
                                                         reinterpret_cast<HDC>(wParam));
            if (result) {
                return result;
            }
            break;
        }
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

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const LRESULT result = ThemedEditColorResult(reinterpret_cast<HWND>(lParam),
                                                         reinterpret_cast<HDC>(wParam));
            if (result) {
                return result;
            }
            break;
        }
    }
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwndMain = hwnd;
            g_app.hMenu = GetMenu(hwnd);
            SetMenu(hwnd, nullptr);

            WNDCLASSEXW menuClass{};
            menuClass.cbSize = sizeof(menuClass);
            menuClass.lpfnWndProc = MenuBarProc;
            menuClass.hInstance = g_app.hInstance;
            menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            menuClass.lpszClassName = kMenuBarClassName;
            RegisterClassExW(&menuClass);

            WNDCLASSEXW statusClass{};
            statusClass.cbSize = sizeof(statusClass);
            statusClass.lpfnWndProc = StatusBarProc;
            statusClass.hInstance = g_app.hInstance;
            statusClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            statusClass.lpszClassName = kStatusBarClassName;
            RegisterClassExW(&statusClass);

            INITCOMMONCONTROLSEX icc{};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icc);

            g_app.hUiFont = CreateFontW(
                PointsToLogFontHeight(hwnd, 10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

            g_app.hwndMenuBar = CreateWindowExW(
                0, kMenuBarClassName, nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hwnd, nullptr,
                g_app.hInstance, nullptr);

            g_app.hwndStatus = CreateWindowExW(
                0, kStatusBarClassName, nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUS),
                g_app.hInstance, nullptr);

            InitDefaultEditorLogFont(hwnd);
            g_app.hwndEdit = CreateEditorWindow(hwnd);
            ApplyFont();
            ApplyAppTheme(hwnd);

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

        case WM_MOUSEWHEEL:
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (delta != 0) {
                    ApplyEditorPointSize(hwnd,
                                         g_app.editorPointSize + (delta > 0 ? 1 : -1));
                }
                return 0;
            }
            break;

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = g_app.hEditBrush ? g_app.hEditBrush : GetSysColorBrush(COLOR_WINDOW);
            FillRect(hdc, &rc, brush);
            return TRUE;
        }

        case WM_SETFOCUS:
            SetFocus(g_app.hwndEdit);
            return 0;

        case WM_SETTINGCHANGE:
            if (lParam != 0) {
                const wchar_t* section = reinterpret_cast<const wchar_t*>(lParam);
                if (wcscmp(section, L"ImmersiveColorSet") == 0 ||
                    wcscmp(section, L"WindowsThemeElement") == 0) {
                    ApplyAppTheme(hwnd);
                }
            }
            return 0;

        case WM_INITMENUPOPUP: {
            HMENU popupMenu = reinterpret_cast<HMENU>(lParam);
            if (g_app.darkMode && g_app.hMenuBrush && popupMenu) {
                MENUINFO popupInfo{};
                popupInfo.cbSize = sizeof(MENUINFO);
                popupInfo.fMask = MIM_BACKGROUND | MIM_STYLE;
                popupInfo.dwStyle = MNS_MODE_BACKGROUNDS;
                popupInfo.hbrBack = g_app.hMenuBrush;
                SetMenuInfo(popupMenu, &popupInfo);
            }
            break;
        }

        case WM_MEASUREITEM: {
            auto* measureInfo = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measureInfo->CtlType == ODT_MENU && g_app.darkMode) {
                MeasureOwnerDrawMenuItem(hwnd, measureInfo);
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM: {
            auto* drawInfo = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (drawInfo->CtlType == ODT_MENU && g_app.darkMode) {
                DrawOwnerDrawMenuItem(hwnd, drawInfo);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            const LRESULT result = ThemedEditColorResult(reinterpret_cast<HWND>(lParam), hdc);
            if (result) {
                return result;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            const HWND ctrl = reinterpret_cast<HWND>(lParam);
            HDC hdc = reinterpret_cast<HDC>(wParam);
            if (ctrl == g_app.hwndStatus && g_app.hStatusBrush) {
                const COLORREF statusBg = g_app.darkMode ? kDarkStatusBg : kLightStatusBg;
                SetBkColor(hdc, statusBg);
                SetTextColor(hdc, g_app.darkMode ? kDarkEditorFg : kLightEditorFg);
                return reinterpret_cast<LRESULT>(g_app.hStatusBrush);
            }
            const LRESULT result = ThemedEditColorResult(ctrl, hdc);
            if (result) {
                return result;
            }
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
            if (g_app.hMenu) DestroyMenu(g_app.hMenu);
            if (g_app.hFont) DeleteObject(g_app.hFont);
            if (g_app.hUiFont) DeleteObject(g_app.hUiFont);
            if (g_app.hEditBrush) DeleteObject(g_app.hEditBrush);
            if (g_app.hStatusBrush) DeleteObject(g_app.hStatusBrush);
            if (g_app.hMenuBrush) DeleteObject(g_app.hMenuBrush);
            g_app.hMenu = nullptr;
            g_app.hFont = nullptr;
            g_app.hUiFont = nullptr;
            g_app.hEditBrush = nullptr;
            g_app.hStatusBrush = nullptr;
            g_app.hMenuBrush = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
