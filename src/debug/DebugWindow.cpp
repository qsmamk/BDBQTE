#include "DebugWindow.h"

#include <cwchar>
#include <string>

namespace {
constexpr wchar_t kWindowClass[] = L"QTEVisionStatusWindow";
constexpr int kHotkeyF7 = 1;
constexpr int kHotkeyF8 = 2;
constexpr int kHotkeyF9 = 3;

void DrawLine(HDC dc, int x, int y, const wchar_t* text) {
    TextOutW(dc, x, y, text, static_cast<int>(wcslen(text)));
}
}

bool DebugWindow::Create(HINSTANCE instance, std::wstring& error) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"Could not register the QTE Vision window class";
        return false;
    }

    window_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"QTE Vision",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        20, 20, 390, 190, nullptr, nullptr, instance, this);
    if (!window_) {
        error = L"Could not create the QTE Vision status window";
        return false;
    }
    font_ = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Segoe UI");
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    const bool f7 = RegisterHotKey(window_, kHotkeyF7, MOD_NOREPEAT, VK_F7) != FALSE;
    const bool f8 = RegisterHotKey(window_, kHotkeyF8, MOD_NOREPEAT, VK_F8) != FALSE;
    const bool f9 = RegisterHotKey(window_, kHotkeyF9, MOD_NOREPEAT, VK_F9) != FALSE;
    globalHotkeysAvailable_ = f7 && f8 && f9;
    return true;
}

void DebugWindow::Update(bool programEnabled, const QteStats& qte) {
    programEnabled_ = programEnabled;
    qte_ = qte;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void DebugWindow::PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            shouldExit_ = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool DebugWindow::ConsumeF7() noexcept { const bool result = f7Pressed_; f7Pressed_ = false; return result; }
bool DebugWindow::ConsumeF8() noexcept { const bool result = f8Pressed_; f8Pressed_ = false; return result; }
bool DebugWindow::ConsumeF9() noexcept { const bool result = f9Pressed_; f9Pressed_ = false; return result; }

LRESULT CALLBACK DebugWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<DebugWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<DebugWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT DebugWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_HOTKEY:
        if (wParam == kHotkeyF7) f7Pressed_ = true;
        else if (wParam == kHotkeyF8) f8Pressed_ = true;
        else if (wParam == kHotkeyF9) f9Pressed_ = true;
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F7) f7Pressed_ = true;
        else if (wParam == VK_F8) f8Pressed_ = true;
        else if (wParam == VK_F9) f9Pressed_ = true;
        return 0;
    case WM_CLOSE:
        shouldExit_ = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(window, kHotkeyF7);
        UnregisterHotKey(window, kHotkeyF8);
        UnregisterHotKey(window, kHotkeyF9);
        if (font_) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(window);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

void DebugWindow::Paint(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    HGDIOBJ oldFont = font_ ? SelectObject(dc, font_) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(25, 25, 25));

    DrawLine(dc, 16, 14, programEnabled_ ? L"状态：运行中" : L"状态：已暂停（图像检测和按键均关闭）");
    DrawLine(dc, 16, 42, !qte_.referenceLoaded ? L"参考图：缺失（显示 QTE 时按 F8 保存）"
        : (qte_.qteDetected ? L"QTE：已检测到" : L"QTE：等待中"));
    DrawLine(dc, 16, 70, qte_.perfectLocked ? L"Perfect：已锁定" : L"Perfect：识别中");
    DrawLine(dc, 16, 106, L"F7 启动/暂停    F8 保存参考图    F9 保存 QTE 范围图");
    DrawLine(dc, 16, 134, L"关闭此窗口即可退出程序");

    if (oldFont) SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}
