#include "notepad.h"
#include "resource.h"

#include <shellapi.h>
#include <shellscalingapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shcore.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    InitAppTheming();

    g_app.hInstance = hInstance;
    g_app.hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));

    const wchar_t* className = L"UmbraNoteClass";

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ZERONOTE));
    wcex.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
    wcex.lpszClassName = className;
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ZERONOTE));

    if (!RegisterClassExW(&wcex)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"UmbraNote",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, className, L"Untitled - UmbraNote",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create main window.", L"UmbraNote",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        OpenFilePath(hwnd, argv[1]);
    }
    if (argv) {
        LocalFree(argv);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd, g_app.hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}
