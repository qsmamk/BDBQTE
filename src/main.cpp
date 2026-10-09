#include "capture/DxgiCapture.h"
#include "config/Config.h"
#include "debug/DebugWindow.h"
#include "vision/ImageIO.h"
#include "vision/PixelUtils.h"
#include "vision/QteDetector.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
std::wstring HrText(const wchar_t* action, HRESULT hr) {
    wchar_t buffer[96]{};
    swprintf_s(buffer, L"%s (HRESULT = 0x%08X)", action, static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring GetExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path().wstring();
}

std::wstring GetConfigPath() {
    const fs::path currentPath = fs::current_path() / L"config.ini";
    if (fs::exists(currentPath)) return currentPath.wstring();
    return (fs::path(GetExecutableDirectory()) / L"config.ini").wstring();
}

void ShowFatalError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"QTE Vision", MB_OK | MB_ICONERROR);
}

class EventLogger {
public:
    explicit EventLogger(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::deque<std::string> recentLines;
        std::string line;
        while (std::getline(input, line)) {
            recentLines.push_back(line);
            if (recentLines.size() > 500) recentLines.pop_front();
        }
        if (input.is_open()) {
            std::ofstream trimmed(path, std::ios::binary | std::ios::trunc);
            for (const auto& recentLine : recentLines) trimmed << recentLine << '\n';
        }
        file_.open(path, std::ios::app);
    }

    void Write(const std::wstring& message) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t prefix[40]{};
        swprintf_s(prefix, L"[%02u:%02u:%02u.%03u] ", time.wHour, time.wMinute,
            time.wSecond, time.wMilliseconds);
        if (file_) {
            file_ << prefix << message << L"\n";
            file_.flush();
        }
    }

private:
    std::wofstream file_;
};

WORD TargetVirtualKey(const std::wstring& key) noexcept {
    const wchar_t ch = key[0];
    if (ch >= L'A' && ch <= L'Z') return static_cast<WORD>(L'A' + (ch - L'A'));
    return static_cast<WORD>(L'0' + (ch - L'0'));
}

bool SendTargetKey(WORD virtualKey) noexcept {
    INPUT events[2]{};
    events[0].type = INPUT_KEYBOARD;
    events[0].ki.wVk = virtualKey;
    events[1].type = INPUT_KEYBOARD;
    events[1].ki.wVk = virtualKey;
    events[1].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sent = SendInput(2, events, sizeof(INPUT));
    if (sent != 2) {
        INPUT release{};
        release.type = INPUT_KEYBOARD;
        release.ki.wVk = virtualKey;
        release.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &release, sizeof(INPUT));
    }
    return sent == 2;
}

void DrawScopePixel(CpuImage& image, int x, int y, const BgraPixel& color) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    auto* pixel = reinterpret_cast<BgraPixel*>(image.pixels.data()
        + static_cast<size_t>(y) * static_cast<size_t>(image.stride) + static_cast<size_t>(x) * 4);
    *pixel = color;
}

bool WriteQteScopeSnapshot(const FrameView& view, const AppConfig& config, const std::wstring& path,
    std::wstring& error) {
    const int padding = std::max(8, config.ringThickness / 2 + 4);
    const int extent = config.radius + padding;
    const int left = config.centerX - extent;
    const int top = config.centerY - extent;
    const int side = extent * 2 + 1;
    if (left < 0 || top < 0 || left + side > view.width || top + side > view.height) {
        error = L"Configured QTE scope is outside the captured monitor; check center_x, center_y, and radius.";
        return false;
    }

    CpuImage image{};
    image.width = side;
    image.height = side;
    image.stride = image.width * 4;
    image.pixels.resize(static_cast<size_t>(image.stride) * static_cast<size_t>(image.height));
    for (int y = 0; y < side; ++y) {
        const auto* source = view.pixels + static_cast<size_t>(top + y) * static_cast<size_t>(view.rowPitch)
            + static_cast<size_t>(left) * 4;
        std::copy_n(source, image.stride,
            image.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(image.stride));
    }

    const BgraPixel guide{ 255, 80, 255, 255 };
    const BgraPixel crosshair{ 40, 220, 40, 255 };
    for (int step = 0; step < 720; ++step) {
        const double angle = static_cast<double>(step) * 2.0 * 3.14159265358979323846 / 720.0;
        DrawScopePixel(image, extent + static_cast<int>(std::lround(config.radius * std::cos(angle))),
            extent - static_cast<int>(std::lround(config.radius * std::sin(angle))), guide);
    }
    for (int offset = -5; offset <= 5; ++offset) {
        DrawScopePixel(image, extent + offset, extent, crosshair);
        DrawScopePixel(image, extent, extent + offset, crosshair);
    }
    return SaveBmpWic(path, image, error);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        ShowFatalError(HrText(L"COM initialization failed", comResult));
        return 1;
    }

    AppConfig config{};
    std::wstring error;
    const std::wstring configPath = GetConfigPath();
    if (!LoadConfig(configPath, config, error)) {
        ShowFatalError(error);
        CoUninitialize();
        return 1;
    }

    const fs::path appDirectory = fs::path(configPath).parent_path();
    EventLogger logger(appDirectory / L"qtevision.log");
    logger.Write(L"QTE Vision started; target key=" + config.targetKey
        + L", trigger_offset_deg=" + std::to_wstring(config.triggerOffsetDegrees));

    QteDetector detector(config);
    const std::wstring referencePath = (appDirectory / L"reference.bmp").wstring();
    if (fs::exists(referencePath)) {
        if (detector.LoadReference(referencePath, error)) logger.Write(L"Loaded reference.bmp");
        else logger.Write(L"Reference image could not be loaded: " + error);
    } else {
        logger.Write(L"reference.bmp is missing; press F8 while the QTE is visible to save it");
    }

    DebugWindow window;
    if (!window.Create(instance, error)) {
        ShowFatalError(error);
        CoUninitialize();
        return 1;
    }
    if (!window.GlobalHotkeysAvailable()) {
        logger.Write(L"One or more global hotkeys are unavailable; focus the QTE Vision window to use F7, F8, and F9");
    }

    DxgiCapture capture;
    if (!capture.Initialize(config.monitorIndex, error)) {
        ShowFatalError(error);
        CoUninitialize();
        return 1;
    }

    QteStats latestStats{};
    bool programEnabled = true;
    bool saveReferenceRequested = false;
    bool saveScopeRequested = false;
    bool previousQte = false;
    bool previousLock = false;
    bool previousTriggered = false;
    int validatedWidth = 0;
    int validatedHeight = 0;
    const WORD targetVirtualKey = TargetVirtualKey(config.targetKey);
    window.Update(programEnabled, latestStats);

    while (!window.ShouldExit()) {
        window.PumpMessages();
        if (window.ShouldExit()) break;

        if (window.ConsumeF7()) {
            programEnabled = !programEnabled;
            detector.ResetState();
            latestStats = {};
            previousQte = false;
            previousLock = false;
            previousTriggered = false;
            window.Update(programEnabled, latestStats);
            logger.Write(programEnabled ? L"Program enabled" : L"Program paused; capture, detection, and input stopped");
        }
        if (!programEnabled) {
            window.ConsumeF8();
            window.ConsumeF9();
            Sleep(10);
            continue;
        }
        if (window.ConsumeF8()) saveReferenceRequested = true;
        if (window.ConsumeF9()) saveScopeRequested = true;

        FrameView frame{};
        HRESULT captureHr = S_OK;
        const CaptureStatus captureStatus = capture.BeginFrame(frame, captureHr, error);
        if (captureStatus == CaptureStatus::Failed) {
            const std::wstring failure = error.empty() ? HrText(L"Desktop capture failed", captureHr) : error;
            ShowFatalError(failure);
            logger.Write(failure);
            break;
        }
        if (captureStatus == CaptureStatus::Reinitialized) logger.Write(L"Display capture restarted after an output change");
        if (captureStatus != CaptureStatus::Frame) {
            if (captureStatus == CaptureStatus::Timeout) MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        window.PumpMessages();
        if (window.ConsumeF7()) {
            programEnabled = false;
            detector.ResetState();
            latestStats = {};
            previousQte = false;
            previousLock = false;
            previousTriggered = false;
            window.Update(false, latestStats);
            logger.Write(L"Program paused; capture, detection, and input stopped");
            capture.EndFrame();
            continue;
        }

        if (frame.width != validatedWidth || frame.height != validatedHeight) {
            if (!detector.ValidateFrame(frame, error)) {
                ShowFatalError(error);
                logger.Write(error);
                capture.EndFrame();
                break;
            }
            validatedWidth = frame.width;
            validatedHeight = frame.height;
        }

        if (saveReferenceRequested) {
            if (detector.SaveReferenceFromFrame(frame, referencePath, error)) {
                logger.Write(L"Saved center reference image: reference.bmp");
                detector.ResetState();
            } else {
                logger.Write(L"Could not save reference.bmp: " + error);
            }
            saveReferenceRequested = false;
        }
        if (saveScopeRequested) {
            const std::wstring path = (appDirectory / L"qte_scope.bmp").wstring();
            if (WriteQteScopeSnapshot(frame, config, path, error)) logger.Write(L"Saved QTE scope image: qte_scope.bmp");
            else logger.Write(L"Could not save qte_scope.bmp: " + error);
            saveScopeRequested = false;
        }

        latestStats = detector.Process(frame);
        capture.EndFrame();

        if (latestStats.qteDetected && !previousQte) logger.Write(L"QTE detected");
        if (latestStats.perfectLocked && !previousLock) {
            wchar_t message[128]{};
            swprintf_s(message, L"Perfect area locked: %.1f-%.1f deg",
                latestStats.lockedPerfectZone.startDegrees, latestStats.lockedPerfectZone.endDegrees);
            logger.Write(message);
        }
        if (latestStats.qteState == QteState::Triggered && !previousTriggered) {
            wchar_t message[160]{};
            swprintf_s(message, L"Pointer entered offset trigger range at %.1f deg; TRIGGERED",
                latestStats.pointerAngleDegrees);
            logger.Write(message);
            if (SendTargetKey(targetVirtualKey)) logger.Write(L"SendInput sent target key " + config.targetKey);
            else logger.Write(L"SendInput failed for target key " + config.targetKey);
        }
        if (!latestStats.qteDetected && previousQte) logger.Write(L"QTE ended; trigger state reset");

        previousQte = latestStats.qteDetected;
        previousLock = latestStats.perfectLocked;
        previousTriggered = latestStats.triggered;
        window.Update(programEnabled, latestStats);
    }

    capture.EndFrame();
    logger.Write(L"QTE Vision stopped");
    CoUninitialize();
    return 0;
}
