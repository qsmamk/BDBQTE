#include "DebugWindow.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
constexpr wchar_t kWindowClass[] = L"QTEVisionDebuggerWindow";
constexpr int kHotkeyF8 = 1;
constexpr int kHotkeyF9 = 2;
constexpr int kHotkeyF10 = 3;
constexpr int kHotkeyF11 = 4;
constexpr int kHotkeyF7 = 5;
constexpr int kHotkeyF12 = 6;

std::wstring DegreesText(const AngleRange& arc) {
    if (!arc.valid) return L"not detected";
    std::wostringstream text;
    text.precision(1);
    text << std::fixed << arc.startDegrees << L" - " << arc.endDegrees << L" deg (" << arc.spanDegrees << L" deg)";
    return text.str();
}

const wchar_t* QteStateText(QteState state) noexcept {
    switch (state) {
    case QteState::WaitingQte: return L"WAITING_QTE";
    case QteState::DetectingPerfect: return L"DETECTING_PERFECT";
    case QteState::PerfectLocked: return L"PERFECT_LOCKED";
    case QteState::Triggered: return L"TRIGGERED";
    case QteState::WaitQteEnd: return L"WAIT_QTE_END";
    }
    return L"UNKNOWN";
}

void DrawTextLine(HDC dc, int x, int y, const std::wstring& text) {
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
}

bool AngleInArc(float angle, const AngleRange& arc) {
    if (!arc.valid) return false;
    float distance = angle - arc.startDegrees;
    while (distance < 0.0f) distance += 360.0f;
    while (distance >= 360.0f) distance -= 360.0f;
    return distance <= arc.spanDegrees;
}
}

bool DebugWindow::Create(HINSTANCE instance, bool visible, bool showPlot, std::wstring& error) {
    plotEnabled_ = showPlot;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"Could not register the debug window class";
        return false;
    }

    window_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"QTE Vision Debugger",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        10, 10, 1020, 850,
        nullptr, nullptr, instance, this);
    if (!window_) {
        error = L"Could not create the debug window";
        return false;
    }
    font_ = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    visible_ = visible;
    if (visible_) {
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
    }

    const bool f8Registered = RegisterHotKey(window_, kHotkeyF8, MOD_NOREPEAT, VK_F8) != FALSE;
    const bool f9Registered = RegisterHotKey(window_, kHotkeyF9, MOD_NOREPEAT, VK_F9) != FALSE;
    const bool f10Registered = RegisterHotKey(window_, kHotkeyF10, MOD_NOREPEAT, VK_F10) != FALSE;
    const bool f11Registered = RegisterHotKey(window_, kHotkeyF11, MOD_NOREPEAT, VK_F11) != FALSE;
    const bool f7Registered = RegisterHotKey(window_, kHotkeyF7, MOD_NOREPEAT, VK_F7) != FALSE;
    const bool f12Registered = RegisterHotKey(window_, kHotkeyF12, MOD_NOREPEAT, VK_F12) != FALSE;
    globalHotkeysAvailable_ = f8Registered && f9Registered && f10Registered && f11Registered
        && f7Registered && f12Registered;
    if (!f8Registered) {
        OutputDebugStringW(L"F8 global hotkey could not be registered; focus the debug window to use it.\n");
    }
    if (!f9Registered) {
        OutputDebugStringW(L"F9 global hotkey could not be registered; focus the debug window to use it.\n");
    }
    if (!f10Registered) {
        OutputDebugStringW(L"F10 global hotkey could not be registered; focus the debug window to use it.\n");
    }
    if (!f11Registered) {
        OutputDebugStringW(L"F11 global hotkey could not be registered; focus the debug window to use it.\n");
    }
    if (!f7Registered) {
        OutputDebugStringW(L"F7 global hotkey could not be registered; focus the debug window to use it.\n");
    }
    if (!f12Registered) {
        OutputDebugStringW(L"F12 global hotkey could not be registered; focus the debug window to use it.\n");
    }
    return true;
}

void DebugWindow::Update(const QteStats& qte, const PerformanceDisplay& performance, const InputDisplay& input,
    const PointerMotionDisplay& pointerMotion) {
    qte_ = qte;
    performance_ = performance;
    input_ = input;
    pointerMotion_ = pointerMotion;
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

bool DebugWindow::ConsumeF8() noexcept { const bool result = f8Pressed_; f8Pressed_ = false; return result; }
bool DebugWindow::ConsumeF9() noexcept { const bool result = f9Pressed_; f9Pressed_ = false; return result; }
bool DebugWindow::ConsumeF10() noexcept { const bool result = f10Pressed_; f10Pressed_ = false; return result; }
bool DebugWindow::ConsumeF11() noexcept { const bool result = f11Pressed_; f11Pressed_ = false; return result; }
bool DebugWindow::ConsumeF7() noexcept { const bool result = f7Pressed_; f7Pressed_ = false; return result; }
bool DebugWindow::ConsumeF12() noexcept { const bool result = f12Pressed_; f12Pressed_ = false; return result; }

LRESULT CALLBACK DebugWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    DebugWindow* self = reinterpret_cast<DebugWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<DebugWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT DebugWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_HOTKEY:
        if (wParam == kHotkeyF8) f8Pressed_ = true;
        if (wParam == kHotkeyF9) f9Pressed_ = true;
        if (wParam == kHotkeyF10) { f10Pressed_ = true; plotEnabled_ = !plotEnabled_; }
        if (wParam == kHotkeyF11) f11Pressed_ = true;
        if (wParam == kHotkeyF7) f7Pressed_ = true;
        if (wParam == kHotkeyF12) f12Pressed_ = true;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            shouldExit_ = true;
            PostQuitMessage(0);
        } else if (wParam == VK_F8) {
            f8Pressed_ = true;
        } else if (wParam == VK_F9) {
            f9Pressed_ = true;
        } else if (wParam == VK_F10) {
            f10Pressed_ = true;
            plotEnabled_ = !plotEnabled_;
        } else if (wParam == VK_F11) {
            f11Pressed_ = true;
        } else if (wParam == VK_F7 && (lParam & (1LL << 30)) == 0) {
            f7Pressed_ = true;
        } else if (wParam == VK_F12 && (lParam & (1LL << 30)) == 0) {
            f12Pressed_ = true;
        }
        return 0;
    case WM_CLOSE:
        shouldExit_ = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(window, kHotkeyF8);
        UnregisterHotKey(window, kHotkeyF9);
        UnregisterHotKey(window, kHotkeyF10);
        UnregisterHotKey(window, kHotkeyF11);
        UnregisterHotKey(window, kHotkeyF7);
        UnregisterHotKey(window, kHotkeyF12);
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

    DrawTextLine(dc, 18, 14, L"QTE Vision Debugger - Phase 2 (visual detection + optional keyboard input)");
    wchar_t line[256]{};
    swprintf_s(line, L"Detection: %s      Capture FPS: %6.1f", performance_.detectionRunning ? L"RUNNING" : L"PAUSED", performance_.captureFps);
    DrawTextLine(dc, 18, 50, line);
    swprintf_s(line, L"Capture: %7.3f ms     ROI: %7.3f ms", performance_.captureMs, qte_.roiTimeMs);
    DrawTextLine(dc, 18, 78, line);
    swprintf_s(line, L"QTE:     %7.3f ms     Vision Latency: %7.3f ms", qte_.qteTimeMs, performance_.visionMs);
    DrawTextLine(dc, 18, 106, line);
    swprintf_s(line, L"Total Latency: %7.3f ms     Avg: %7.3f ms", performance_.totalMs, performance_.averageMs);
    DrawTextLine(dc, 18, 134, line);
    swprintf_s(line, L"Max:     %7.3f ms     P95:    %7.3f ms     P99: %7.3f ms", performance_.maximumMs, performance_.p95Ms, performance_.p99Ms);
    DrawTextLine(dc, 18, 162, line);

    const wchar_t* qteState = !qte_.referenceLoaded ? L"NO REFERENCE" : (qte_.qteDetected ? L"YES" : L"NO");
    if (qte_.referenceLoaded) {
        swprintf_s(line, L"QTE: %s     Match: %.2f %%", qteState, qte_.qteMatchPercent);
    } else {
        swprintf_s(line, L"QTE: %s     Match: --", qteState);
    }
    DrawTextLine(dc, 18, 210, line);
    DrawTextLine(dc, 18, 238, std::wstring(L"Perfect: ") + (qte_.perfectZone.valid ? L"FOUND" : L"NOT FOUND")
        + L"    Perfect Locked: " + (qte_.perfectLocked ? L"YES" : L"NO"));
    if (pointerMotion_.perfectCenterValid) {
        swprintf_s(line, L"Perfect Start Angle: %6.1f deg    Perfect End Angle: %6.1f deg",
            pointerMotion_.perfectStartAngleDegrees, pointerMotion_.perfectEndAngleDegrees);
    } else {
        swprintf_s(line, L"Perfect Start Angle: --    Perfect End Angle: --");
    }
    DrawTextLine(dc, 18, 266, line);
    if (pointerMotion_.perfectCenterValid) {
        swprintf_s(line, L"Perfect Center Angle: %6.1f deg", pointerMotion_.perfectCenterAngleDegrees);
    } else {
        swprintf_s(line, L"Perfect Center Angle: --");
    }
    DrawTextLine(dc, 18, 294, line);
    DrawTextLine(dc, 18, 322, std::wstring(L"Pointer: ") + (qte_.pointerDetected ? L"FOUND" : L"NOT FOUND"));
    if (qte_.pointerDetected) {
        swprintf_s(line, L"Pointer Angle: %6.1f deg", qte_.pointerAngleDegrees);
    } else {
        swprintf_s(line, L"Pointer Angle: not detected");
    }
    DrawTextLine(dc, 18, 350, line);
    if (pointerMotion_.angularVelocityValid) {
        swprintf_s(line, L"Pointer Angular Velocity: %.4f deg/ms", pointerMotion_.angularVelocityDegreesPerMs);
    } else {
        swprintf_s(line, L"Pointer Angular Velocity: -- deg/ms");
    }
    DrawTextLine(dc, 18, 378, line);
    if (pointerMotion_.perfectCenterAlreadyPassed) {
        swprintf_s(line, L"Predicted Time To Perfect Center: ALREADY PASSED");
    } else if (pointerMotion_.predictedTimeValid) {
        swprintf_s(line, L"Predicted Time To Perfect Center: %.2f ms", pointerMotion_.predictedTimeToPerfectCenterMs);
    } else {
        swprintf_s(line, L"Predicted Time To Perfect Center: --");
    }
    DrawTextLine(dc, 18, 406, line);
    swprintf_s(line, L"Red Score: %d", qte_.redPixelCount);
    DrawTextLine(dc, 18, 434, line);
    DrawTextLine(dc, 18, 462, std::wstring(L"Trigger Candidate: ") + (qte_.triggerCandidate ? L"YES" : L"NO"));
    DrawTextLine(dc, 18, 490, std::wstring(L"Triggered: ") + (qte_.triggered ? L"YES" : L"NO"));
    DrawTextLine(dc, 18, 518, std::wstring(L"QTE State: ") + QteStateText(qte_.qteState));
    DrawTextLine(dc, 18, 546, std::wstring(L"AUTO INPUT: ") + (input_.autoInputEnabled ? L"ON" : L"OFF"));
    DrawTextLine(dc, 18, 574, std::wstring(L"INPUT SENT: ") + (input_.inputSent ? L"YES" : L"NO")
        + L"  (" + input_.inputResult + L")");
    wchar_t inputCountLine[96]{};
    swprintf_s(inputCountLine, L"INPUT COUNT: %llu", static_cast<unsigned long long>(input_.inputCount));
    DrawTextLine(dc, 18, 602, inputCountLine);
    DrawTextLine(dc, 18, 630, L"TRIGGER TIMESTAMP: " + input_.triggerTimestamp);
    DrawTextLine(dc, 18, 658, L"INPUT TIMESTAMP:   " + input_.inputTimestamp);
    if (input_.inputLatencyMs >= 0.0) {
        swprintf_s(line, L"INPUT LATENCY: %.3f ms", input_.inputLatencyMs);
    } else {
        swprintf_s(line, L"INPUT LATENCY: --");
    }
    DrawTextLine(dc, 18, 686, line);
    DrawTextLine(dc, 18, 714, L"TEST INPUT: " + input_.testInputResult);

    DrawTextLine(dc, 18, 756, L"F7 Auto Input ON/OFF    F12 Test Input (sends target key once)");
    DrawTextLine(dc, 18, 784, L"F8 Start/Stop    F9 Save reference.bmp    F10 Toggle plot    F11 Save qte_scope.bmp    ESC Exit");

    if (plotEnabled_) {
        const int centerX = 800;
        const int centerY = 430;
        const int radius = 120;
        HPEN ringPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
        HGDIOBJ oldPen = SelectObject(dc, ringPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(ringPen);
        HPEN zonePen = CreatePen(PS_SOLID, 5, RGB(35, 160, 70));
        oldPen = SelectObject(dc, zonePen);
        for (int degree = 0; degree < 360; ++degree) {
            if (!AngleInArc(static_cast<float>(degree), qte_.normalZone)) continue;
            const double angle = static_cast<double>(degree) * 3.14159265358979323846 / 180.0;
            const int x = centerX + static_cast<int>(std::lround(radius * std::cos(angle)));
            const int y = centerY - static_cast<int>(std::lround(radius * std::sin(angle)));
            SetPixelV(dc, x, y, RGB(45, 145, 85));
        }
        SelectObject(dc, oldPen);
        DeleteObject(zonePen);
        HPEN perfectPen = CreatePen(PS_SOLID, 6, RGB(30, 90, 220));
        oldPen = SelectObject(dc, perfectPen);
        for (int degree = 0; degree < 360; ++degree) {
            if (!AngleInArc(static_cast<float>(degree), qte_.perfectZone)) continue;
            const double angle = static_cast<double>(degree) * 3.14159265358979323846 / 180.0;
            const int x = centerX + static_cast<int>(std::lround(radius * std::cos(angle)));
            const int y = centerY - static_cast<int>(std::lround(radius * std::sin(angle)));
            SetPixelV(dc, x, y, RGB(30, 90, 220));
        }
        SelectObject(dc, oldPen);
        DeleteObject(perfectPen);
        if (qte_.pointerDetected) {
            HPEN pointerPen = CreatePen(PS_SOLID, 2, RGB(220, 35, 35));
            oldPen = SelectObject(dc, pointerPen);
            const double angle = static_cast<double>(qte_.pointerAngleDegrees) * 3.14159265358979323846 / 180.0;
            MoveToEx(dc, centerX, centerY, nullptr);
            LineTo(dc, centerX + static_cast<int>(std::lround((radius - 8) * std::cos(angle))),
                centerY - static_cast<int>(std::lround((radius - 8) * std::sin(angle))));
            SelectObject(dc, oldPen);
            DeleteObject(pointerPen);
        }
        SetPixelV(dc, centerX, centerY, RGB(0, 0, 0));
    }
    if (oldFont) SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}
