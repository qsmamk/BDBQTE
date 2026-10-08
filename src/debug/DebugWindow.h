#pragma once

#include "../vision/QteDetector.h"

#include <Windows.h>

#include <cstdint>
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

struct InputDisplay {
    bool autoInputEnabled = false;
    bool inputSent = false;
    std::uint64_t inputCount = 0;
    std::wstring triggerTimestamp = L"--";
    std::wstring inputTimestamp = L"--";
    double inputLatencyMs = -1.0;
    std::wstring inputResult = L"WAITING FOR TRIGGER";
    std::wstring testInputResult = L"NOT USED";
};

struct PointerMotionDisplay {
    bool perfectCenterValid = false;
    double perfectStartAngleDegrees = 0.0;
    double perfectEndAngleDegrees = 0.0;
    double perfectCenterAngleDegrees = 0.0;
    bool angularVelocityValid = false;
    double angularVelocityDegreesPerMs = 0.0;
    bool perfectCenterAlreadyPassed = false;
    bool predictedTimeValid = false;
    double predictedTimeToPerfectCenterMs = 0.0;
};

class DebugWindow {
public:
    bool Create(HINSTANCE instance, bool visible, bool showPlot, std::wstring& error);
    void Update(const QteStats& qte, const PerformanceDisplay& performance, const InputDisplay& input,
        const PointerMotionDisplay& pointerMotion);
    void PumpMessages();
    bool ConsumeF8() noexcept;
    bool ConsumeF9() noexcept;
    bool ConsumeF10() noexcept;
    bool ConsumeF11() noexcept;
    bool ConsumeF7() noexcept;
    bool ConsumeF12() noexcept;
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
    InputDisplay input_{};
    PointerMotionDisplay pointerMotion_{};
    bool visible_ = false;
    bool plotEnabled_ = true;
    bool f8Pressed_ = false;
    bool f9Pressed_ = false;
    bool f10Pressed_ = false;
    bool f11Pressed_ = false;
    bool f7Pressed_ = false;
    bool f12Pressed_ = false;
    bool globalHotkeysAvailable_ = true;
    bool shouldExit_ = false;
};
