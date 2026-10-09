#pragma once

#include "../vision/QteDetector.h"

#include <Windows.h>

class DebugWindow {
public:
    bool Create(HINSTANCE instance, std::wstring& error);
    void Update(bool programEnabled, const QteStats& qte);
    void PumpMessages();
    bool ConsumeF7() noexcept;
    bool ConsumeF8() noexcept;
    bool ConsumeF9() noexcept;
    bool ShouldExit() const noexcept { return shouldExit_; }
    bool GlobalHotkeysAvailable() const noexcept { return globalHotkeysAvailable_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint(HWND window);

    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    bool programEnabled_ = true;
    QteStats qte_{};
    bool f7Pressed_ = false;
    bool f8Pressed_ = false;
    bool f9Pressed_ = false;
    bool globalHotkeysAvailable_ = true;
    bool shouldExit_ = false;
};
