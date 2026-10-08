#pragma once

#include "../vision/QteDetector.h"

#include <Windows.h>

#include <string>

struct PerformanceDisplay {
    double captureFps = 0.0;
    double captureMs = 0.0;
    double visionMs = 0.0;
    double totalMs = 0.0;
    double averageMs = 0.0;
    double maximumMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    bool detectionRunning = true;
};

class DebugWindow {
public:
    bool Create(HINSTANCE instance, bool visible, bool showPlot, std::wstring& error);
    void Update(const QteStats& qte, const PerformanceDisplay& performance);
    void PumpMessages();
    bool ConsumeF8() noexcept;
    bool ConsumeF9() noexcept;
    bool ConsumeF10() noexcept;
    bool ConsumeF11() noexcept;
    bool ShouldExit() const noexcept { return shouldExit_; }
    bool PlotEnabled() const noexcept { return plotEnabled_; }
    bool GlobalHotkeysAvailable() const noexcept { return globalHotkeysAvailable_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint(HWND window);

    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    QteStats qte_{};
    PerformanceDisplay performance_{};
    bool visible_ = false;
    bool plotEnabled_ = true;
    bool f8Pressed_ = false;
    bool f9Pressed_ = false;
    bool f10Pressed_ = false;
    bool f11Pressed_ = false;
    bool globalHotkeysAvailable_ = true;
    bool shouldExit_ = false;
};
