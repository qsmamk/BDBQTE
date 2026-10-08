#include "capture/DxgiCapture.h"
#include "config/Config.h"
#include "debug/DebugWindow.h"
#include "vision/ImageIO.h"
#include "vision/PixelUtils.h"
#include "vision/QteDetector.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
LARGE_INTEGER QpcNow() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value;
}

double QpcMilliseconds(LARGE_INTEGER start, LARGE_INTEGER end) noexcept {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    return (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0) / frequency;
}

std::wstring HrText(const wchar_t* action, HRESULT hr) {
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%s (HRESULT = 0x%08X)", action, static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring LocalTimestampText() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t text[64]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return text;
}

WORD TargetVirtualKey(const std::wstring& key) noexcept {
    const wchar_t ch = key[0];
    if (ch >= L'A' && ch <= L'Z') return static_cast<WORD>(L'A' + (ch - L'A'));
    return static_cast<WORD>(L'0' + (ch - L'0'));
}

UINT SendTargetKey(WORD virtualKey) noexcept {
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
    return sent;
}

double NormalizeAngleDegrees(double angle) noexcept {
    angle = std::fmod(angle, 360.0);
    return angle < 0.0 ? angle + 360.0 : angle;
}

double ClockwiseAngleDifference(double previousAngle, double currentAngle) noexcept {
    // PointerAngle uses mathematical coordinates (0° right, 90° up), so clockwise motion is previous-current.
    return NormalizeAngleDegrees(previousAngle - currentAngle);
}

struct PointerMotionTracker {
    bool previousSampleValid = false;
    double previousAngleDegrees = 0.0;
    LARGE_INTEGER previousFrameTimestamp{};
    bool trackedPerfectCenterValid = false;
    double trackedPerfectCenterAngleDegrees = 0.0;
    bool perfectCenterAlreadyPassed = false;
    PointerMotionDisplay display{};
};

void UpdatePointerMotion(const QteStats& stats, LARGE_INTEGER frameTimestamp, PointerMotionTracker& tracker) {
    tracker.display = {};
    const AngleRange& perfect = stats.lockedPerfectZone;
    if (!stats.qteDetected) {
        tracker = {};
        return;
    }

    if (stats.qteDetected && perfect.valid) {
        tracker.display.perfectCenterValid = true;
        tracker.display.perfectStartAngleDegrees = perfect.startDegrees;
        tracker.display.perfectEndAngleDegrees = perfect.endDegrees;
        tracker.display.perfectCenterAngleDegrees = NormalizeAngleDegrees(
            static_cast<double>(perfect.startDegrees) + static_cast<double>(perfect.spanDegrees) * 0.5);

        if (!tracker.trackedPerfectCenterValid
            || std::abs(tracker.trackedPerfectCenterAngleDegrees - tracker.display.perfectCenterAngleDegrees) > 0.01) {
            tracker.trackedPerfectCenterValid = true;
            tracker.trackedPerfectCenterAngleDegrees = tracker.display.perfectCenterAngleDegrees;
            tracker.perfectCenterAlreadyPassed = false;

            // If the first locked frame already sees the pointer beyond the center inside the arc,
            // do not interpret the opposite-side modular difference as another full revolution.
            if (stats.pointerDetected && stats.triggerCandidate) {
                const double clockwisePositionFromEntry = ClockwiseAngleDifference(
                    perfect.endDegrees, static_cast<double>(stats.pointerAngleDegrees));
                tracker.perfectCenterAlreadyPassed = clockwisePositionFromEntry > perfect.spanDegrees * 0.5;
            }
        }
        tracker.display.perfectCenterAlreadyPassed = tracker.perfectCenterAlreadyPassed;
    }

    if (!stats.pointerDetected || !std::isfinite(stats.pointerAngleDegrees)) {
        // Require two consecutive valid pointer samples before refreshing the measured speed.
        tracker.previousSampleValid = false;
        return;
    }

    if (tracker.previousSampleValid && frameTimestamp.QuadPart > tracker.previousFrameTimestamp.QuadPart) {
        const double elapsedMs = QpcMilliseconds(tracker.previousFrameTimestamp, frameTimestamp);
        if (elapsedMs > 0.0) {
            const double clockwiseDelta = ClockwiseAngleDifference(
                tracker.previousAngleDegrees, static_cast<double>(stats.pointerAngleDegrees));
            tracker.display.angularVelocityDegreesPerMs = clockwiseDelta / elapsedMs;
            tracker.display.angularVelocityValid = true;
            if (tracker.display.perfectCenterValid) {
                const double clockwiseDistanceFromPreviousToCenter = ClockwiseAngleDifference(
                    tracker.previousAngleDegrees, tracker.display.perfectCenterAngleDegrees);
                if (clockwiseDistanceFromPreviousToCenter < clockwiseDelta - 0.01) {
                    tracker.perfectCenterAlreadyPassed = true;
                }
            }
        }
    }

    tracker.previousAngleDegrees = static_cast<double>(stats.pointerAngleDegrees);
    tracker.previousFrameTimestamp = frameTimestamp;
    tracker.previousSampleValid = true;

    tracker.display.perfectCenterAlreadyPassed = tracker.perfectCenterAlreadyPassed;
    if (tracker.display.angularVelocityValid && tracker.display.angularVelocityDegreesPerMs > 0.0
        && tracker.display.perfectCenterValid && !tracker.perfectCenterAlreadyPassed) {
        const double clockwiseDistanceToCenter = ClockwiseAngleDifference(
            static_cast<double>(stats.pointerAngleDegrees), tracker.display.perfectCenterAngleDegrees);
        const double predictedTimeMs = clockwiseDistanceToCenter
            / tracker.display.angularVelocityDegreesPerMs;
        if (std::isfinite(predictedTimeMs) && predictedTimeMs >= 0.0) {
            tracker.display.predictedTimeToPerfectCenterMs = predictedTimeMs;
            tracker.display.predictedTimeValid = true;
        }
    }
}

void ShowFatalError(const std::wstring& message) {
    std::wcerr << L"ERROR: " << message << L"\n";
    MessageBoxW(nullptr, message.c_str(), L"QTE Vision Debugger", MB_OK | MB_ICONERROR);
}

std::wstring GetExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path().wstring();
}

std::wstring GetConfigPath(int argc, wchar_t** argv) {
    if (argc >= 3 && _wcsicmp(argv[1], L"--config") == 0) return argv[2];
    const fs::path currentPath = fs::current_path() / L"config.ini";
    if (fs::exists(currentPath)) return currentPath.wstring();
    return (fs::path(GetExecutableDirectory()) / L"config.ini").wstring();
}

class EventLogger {
public:
    EventLogger(int level, const fs::path& logPath) : level_(level), file_(logPath, std::ios::app) {}

    void Write(int level, const std::wstring& message) {
        if (level_ < level || level_ == 0) return;
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t prefix[64]{};
        swprintf_s(prefix, L"[%02u:%02u:%02u.%03u] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        const std::wstring line = std::wstring(prefix) + message;
        std::wcout << line << L"\n";
        if (file_) file_ << line << L"\n";
    }

private:
    int level_ = 0;
    std::wofstream file_;
};

void LogPerfectEntryMeasurement(const QteStats& stats, const PointerMotionDisplay& motion, EventLogger& logger) {
    wchar_t line[320]{};
    if (motion.perfectCenterValid && motion.angularVelocityValid && motion.perfectCenterAlreadyPassed) {
        swprintf_s(line, L"Perfect entry telemetry: PointerAngle=%.1f deg, PerfectCenter=%.1f deg, AngularVelocity=%.5f deg/ms, PredictedTimeToCenter=ALREADY_PASSED",
            stats.pointerAngleDegrees, motion.perfectCenterAngleDegrees,
            motion.angularVelocityDegreesPerMs);
    } else if (motion.perfectCenterValid && motion.angularVelocityValid && motion.predictedTimeValid) {
        swprintf_s(line, L"Perfect entry telemetry: PointerAngle=%.1f deg, PerfectCenter=%.1f deg, AngularVelocity=%.5f deg/ms, PredictedTimeToCenter=%.2f ms",
            stats.pointerAngleDegrees, motion.perfectCenterAngleDegrees,
            motion.angularVelocityDegreesPerMs, motion.predictedTimeToPerfectCenterMs);
    } else if (motion.perfectCenterValid && motion.angularVelocityValid) {
        swprintf_s(line, L"Perfect entry telemetry: PointerAngle=%.1f deg, PerfectCenter=%.1f deg, AngularVelocity=%.5f deg/ms, PredictedTimeToCenter=N/A",
            stats.pointerAngleDegrees, motion.perfectCenterAngleDegrees, motion.angularVelocityDegreesPerMs);
    } else if (motion.perfectCenterValid) {
        swprintf_s(line, L"Perfect entry telemetry: PointerAngle=%.1f deg, PerfectCenter=%.1f deg, AngularVelocity=N/A, PredictedTimeToCenter=N/A",
            stats.pointerAngleDegrees, motion.perfectCenterAngleDegrees);
    } else {
        swprintf_s(line, L"Perfect entry telemetry: PointerAngle=%.1f deg, PerfectCenter=N/A, AngularVelocity=N/A, PredictedTimeToCenter=N/A",
            stats.pointerAngleDegrees);
    }
    logger.Write(1, line);
}

class PerformanceTracker {
public:
    PerformanceTracker() : intervalStart_(QpcNow()) {}

    void Record(double captureMs, double visionMs) {
        const double totalMs = captureMs + visionMs;
        latestCaptureMs_ = captureMs;
        latestVisionMs_ = visionMs;
        latestTotalMs_ = totalMs;
        sumMs_ += totalMs;
        ++sampleCount_;
        maximumMs_ = std::max(maximumMs_, totalMs);
        samples_[nextSample_] = totalMs;
        nextSample_ = (nextSample_ + 1) % samples_.size();
        sampleWindowCount_ = std::min(sampleWindowCount_ + 1, samples_.size());

        ++intervalFrames_;
        const LARGE_INTEGER now = QpcNow();
        const double seconds = QpcMilliseconds(intervalStart_, now) / 1000.0;
        if (seconds >= 1.0) {
            fps_ = static_cast<double>(intervalFrames_) / seconds;
            intervalFrames_ = 0;
            intervalStart_ = now;
            CalculatePercentiles();
        }
    }

    PerformanceDisplay Get(bool detectionRunning) const {
        PerformanceDisplay display{};
        display.captureFps = fps_;
        display.captureMs = latestCaptureMs_;
        display.visionMs = latestVisionMs_;
        display.totalMs = latestTotalMs_;
        display.averageMs = sampleCount_ ? sumMs_ / static_cast<double>(sampleCount_) : 0.0;
        display.maximumMs = maximumMs_;
        display.p95Ms = p95Ms_;
        display.p99Ms = p99Ms_;
        display.detectionRunning = detectionRunning;
        return display;
    }

private:
    void CalculatePercentiles() {
        if (sampleWindowCount_ == 0) return;
        std::array<double, 2048> sorted{};
        std::copy_n(samples_.begin(), sampleWindowCount_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sampleWindowCount_));
        const auto percentile = [&](double fraction) {
            const size_t index = static_cast<size_t>(std::ceil(fraction * static_cast<double>(sampleWindowCount_))) - 1;
            return sorted[std::min(index, sampleWindowCount_ - 1)];
        };
        p95Ms_ = percentile(0.95);
        p99Ms_ = percentile(0.99);
    }

    std::array<double, 2048> samples_{};
    size_t nextSample_ = 0;
    size_t sampleWindowCount_ = 0;
    uint64_t sampleCount_ = 0;
    double sumMs_ = 0.0;
    double maximumMs_ = 0.0;
    double latestCaptureMs_ = 0.0;
    double latestVisionMs_ = 0.0;
    double latestTotalMs_ = 0.0;
    double p95Ms_ = 0.0;
    double p99Ms_ = 0.0;
    double fps_ = 0.0;
    uint64_t intervalFrames_ = 0;
    LARGE_INTEGER intervalStart_{};
};

std::vector<fs::path> FindTestFrames(const fs::path& directory, std::wstring& error) {
    std::vector<fs::path> paths;
    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
        error = L"Test mode is enabled, but the frame folder was not found: " + directory.wstring()
            + L". Add numbered BMP/PNG frames there, or set [Test] test_mode=false to capture the live desktop.";
        return paths;
    }
    for (const auto& item : fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!item.is_regular_file()) continue;
        std::wstring extension = item.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(towlower(ch));
        });
        if (extension == L".bmp" || extension == L".png") paths.push_back(item.path());
    }
    std::sort(paths.begin(), paths.end());
    if (ec) error = L"Could not enumerate test frame folder: " + directory.wstring();
    else if (paths.empty()) {
        error = L"Test mode is enabled, but no BMP or PNG frames were found in: " + directory.wstring()
            + L". Add numbered image frames there, or set [Test] test_mode=false to capture the live desktop. MP4 files are not read directly.";
    }
    return paths;
}

void WriteFrameSnapshot(const FrameView& view, const std::wstring& folder, EventLogger& logger) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        logger.Write(1, L"Could not create debug_frames folder");
        return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t name[64]{};
    swprintf_s(name, L"frame_%02u%02u%02u_%03u.bmp", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    CpuImage image{};
    image.width = view.width;
    image.height = view.height;
    image.stride = view.width * 4;
    image.pixels.resize(static_cast<size_t>(image.stride) * static_cast<size_t>(image.height));
    for (int y = 0; y < view.height; ++y) {
        std::copy_n(view.pixels + static_cast<size_t>(y) * static_cast<size_t>(view.rowPitch), image.stride,
            image.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(image.stride));
    }
    std::wstring saveError;
    if (SaveBmpWic((fs::path(folder) / name).wstring(), image, saveError)) logger.Write(1, L"Saved candidate frame to debug_frames");
    else logger.Write(1, L"Frame save failed: " + saveError);
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
        error = L"The configured QTE scope is outside the captured monitor; check center_x, center_y, and radius.";
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
        std::copy_n(source, image.stride, image.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(image.stride));
    }

    const BgraPixel guide{ 255, 80, 255, 255 };
    const BgraPixel crosshair{ 40, 220, 40, 255 };
    const int localCenter = extent;
    for (int step = 0; step < 720; ++step) {
        const double angle = static_cast<double>(step) * 2.0 * 3.14159265358979323846 / 720.0;
        const int x = localCenter + static_cast<int>(std::lround(config.radius * std::cos(angle)));
        const int y = localCenter - static_cast<int>(std::lround(config.radius * std::sin(angle)));
        DrawScopePixel(image, x, y, guide);
    }
    for (int offset = -5; offset <= 5; ++offset) {
        DrawScopePixel(image, localCenter + offset, localCenter, crosshair);
        DrawScopePixel(image, localCenter, localCenter + offset, crosshair);
    }
    return SaveBmpWic(path, image, error);
}

void LogDetectionChanges(const QteStats& stats, QteStats& previous, EventLogger& logger) {
    if (stats.qteDetected != previous.qteDetected) {
        logger.Write(1, stats.qteDetected ? L"QTE detected" : L"QTE ended");
    }
    const bool targetNow = stats.normalZone.valid && stats.perfectZone.valid;
    const bool targetBefore = previous.normalZone.valid && previous.perfectZone.valid;
    if (targetNow && !targetBefore) {
        wchar_t line[256]{};
        swprintf_s(line, L"Target detected: normal %.1f-%.1f deg, perfect %.1f-%.1f deg",
            stats.normalZone.startDegrees, stats.normalZone.endDegrees,
            stats.perfectZone.startDegrees, stats.perfectZone.endDegrees);
        logger.Write(1, line);
    }
    if (stats.pointerDetected && !previous.pointerDetected) {
        wchar_t line[128]{};
        swprintf_s(line, L"Pointer detected at %.1f deg", stats.pointerAngleDegrees);
        logger.Write(2, line);
    }
    if (stats.triggered && !previous.triggered) {
        wchar_t line[160]{};
        swprintf_s(line, L"Pointer=%.1f deg inside locked perfect arc; TRIGGERED", stats.pointerAngleDegrees);
        logger.Write(1, line);
    }
    previous = stats;
}
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        ShowFatalError(HrText(L"COM initialization failed", comResult));
        return 1;
    }

    AppConfig config{};
    std::wstring error;
    const std::wstring configPath = GetConfigPath(argc, argv);
    if (!LoadConfig(configPath, config, error)) {
        ShowFatalError(error);
        CoUninitialize();
        return 1;
    }
    if (argc >= 2 && _wcsicmp(argv[1], L"--test") == 0) config.testMode = true;

    const fs::path appDirectory = fs::path(configPath).parent_path();
    const int effectiveLogLevel = config.verbose ? 2 : config.logLevel;
    EventLogger logger(effectiveLogLevel, appDirectory / L"qtevision.log");
    logger.Write(1, L"QTE Vision Debugger starting; Phase 2 optional keyboard input (default OFF)");
    logger.Write(1, L"Config: " + configPath);
    logger.Write(1, L"Target key: " + config.targetKey);
    const WORD targetVirtualKey = TargetVirtualKey(config.targetKey);

    QteDetector detector(config);
    const std::wstring referencePath = (appDirectory / L"reference.bmp").wstring();
    if (fs::exists(referencePath)) {
        if (detector.LoadReference(referencePath, error)) logger.Write(1, L"Loaded reference.bmp");
        else logger.Write(1, L"Reference image ignored: " + error);
    } else {
        logger.Write(1, L"No reference.bmp yet. Show the QTE and press F9 to save the center ROI.");
    }

    DebugWindow window;
    if (!window.Create(GetModuleHandleW(nullptr), config.showWindow, config.plotOverlay, error)) {
        ShowFatalError(error);
        CoUninitialize();
        return 1;
    }
    if (!window.GlobalHotkeysAvailable()) {
        logger.Write(1, L"Warning: a global function key is already in use. Focus the debugger window for its local shortcut.");
    }
    PerformanceTracker performance;
    bool detectionRunning = true;
    bool saveReferenceRequested = false;
    bool saveQteScopeRequested = false;
    QteStats latestStats{};
    QteStats previousLogStats{};
    InputDisplay inputDisplay{};
    PointerMotionTracker pointerMotionTracker{};
    LARGE_INTEGER lastUiUpdate = QpcNow();
    int validatedWidth = 0;
    int validatedHeight = 0;

    DxgiCapture capture;
    std::vector<fs::path> testFrames;
    size_t testFrameIndex = 0;
    CpuImage offlineImage{};
    if (config.testMode) {
        const fs::path frameDirectory = fs::path(config.testFramesDirectory).is_absolute()
            ? fs::path(config.testFramesDirectory) : appDirectory / config.testFramesDirectory;
        testFrames = FindTestFrames(frameDirectory, error);
        if (testFrames.empty()) {
            ShowFatalError(error);
            CoUninitialize();
            return 1;
        }
        logger.Write(1, L"Offline test mode: " + std::to_wstring(testFrames.size()) + L" frames from " + frameDirectory.wstring());
    } else if (!capture.Initialize(config.monitorIndex, error)) {
        ShowFatalError(error);
        CoUninitialize();
        return 1;
    }

    while (!window.ShouldExit()) {
        window.PumpMessages();
        if (window.ShouldExit()) break;
        if (window.ConsumeF8()) {
            detectionRunning = !detectionRunning;
            logger.Write(1, detectionRunning ? L"Detection started" : L"Detection paused");
        }
        if (window.ConsumeF9()) saveReferenceRequested = true;
        if (window.ConsumeF10()) logger.Write(2, window.PlotEnabled() ? L"Ring plot enabled" : L"Ring plot hidden");
        if (window.ConsumeF11()) saveQteScopeRequested = true;
        if (window.ConsumeF7()) {
            inputDisplay.autoInputEnabled = !inputDisplay.autoInputEnabled;
            logger.Write(1, inputDisplay.autoInputEnabled ? L"AUTO INPUT: ON" : L"AUTO INPUT: OFF");
        }
        if (window.ConsumeF12()) {
            const std::wstring testTimestamp = LocalTimestampText();
            const UINT sent = SendTargetKey(targetVirtualKey);
            if (sent == 2) {
                inputDisplay.testInputResult = L"SENT " + config.targetKey + L" at " + testTimestamp;
                logger.Write(1, L"TEST INPUT sent target key " + config.targetKey + L" at " + testTimestamp);
            } else {
                inputDisplay.testInputResult = L"FAILED " + config.targetKey + L" at " + testTimestamp;
                logger.Write(1, L"TEST INPUT failed for target key " + config.targetKey
                    + L" (SendInput inserted " + std::to_wstring(sent) + L" of 2 events)");
            }
        }

        FrameView frame{};
        double captureMs = 0.0;
        bool haveFrame = false;
        if (config.testMode) {
            if (testFrameIndex >= testFrames.size()) {
                if (config.loopTestFrames) testFrameIndex = 0;
                else {
                    logger.Write(1, L"Offline frame sequence finished");
                    break;
                }
            }
            const LARGE_INTEGER start = QpcNow();
            if (!LoadImageWic(testFrames[testFrameIndex].wstring(), offlineImage, error)) {
                ShowFatalError(error);
                break;
            }
            ++testFrameIndex;
            captureMs = QpcMilliseconds(start, QpcNow());
            frame.pixels = offlineImage.pixels.data();
            frame.width = offlineImage.width;
            frame.height = offlineImage.height;
            frame.rowPitch = offlineImage.stride;
            frame.desktopRect = { 0, 0, frame.width, frame.height };
            haveFrame = true;
        } else {
            const LARGE_INTEGER start = QpcNow();
            HRESULT captureHr = S_OK;
            const CaptureStatus status = capture.BeginFrame(frame, captureHr, error);
            captureMs = QpcMilliseconds(start, QpcNow());
            if (status == CaptureStatus::Failed) {
                ShowFatalError(error.empty() ? HrText(L"Desktop capture failed", captureHr) : error);
                break;
            }
            if (status == CaptureStatus::Reinitialized) {
                logger.Write(1, L"Display capture was reinitialized after an output change");
            }
            haveFrame = status == CaptureStatus::Frame;
            if (!haveFrame) {
                if (status == CaptureStatus::Timeout) MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }

        if (haveFrame) {
            const LARGE_INTEGER frameTimestamp = QpcNow();
            if (frame.width != validatedWidth || frame.height != validatedHeight) {
                if (!detector.ValidateFrame(frame, error)) {
                    ShowFatalError(error);
                    if (!config.testMode) capture.EndFrame();
                    break;
                }
                validatedWidth = frame.width;
                validatedHeight = frame.height;
            }
            if (saveReferenceRequested) {
                if (detector.SaveReferenceFromFrame(frame, referencePath, error)) {
                    logger.Write(1, L"Saved center ROI to reference.bmp; QTE matching is now active");
                    latestStats = {};
                    latestStats.referenceLoaded = true;
                    previousLogStats = {};
                } else {
                    logger.Write(1, L"Reference save failed: " + error);
                }
                saveReferenceRequested = false;
            }
            if (saveQteScopeRequested) {
                const std::wstring scopePath = (appDirectory / L"qte_scope.bmp").wstring();
                if (WriteQteScopeSnapshot(frame, config, scopePath, error)) {
                    logger.Write(1, L"Saved configured QTE scope to qte_scope.bmp (magenta: radius, green: center)");
                } else {
                    logger.Write(1, L"QTE scope save failed: " + error);
                }
                saveQteScopeRequested = false;
            }

            double visionMs = 0.0;
            if (detectionRunning) {
                latestStats = detector.Process(frame);
                visionMs = latestStats.visionTimeMs;
                const bool enteredPerfect = latestStats.triggerCandidate && !previousLogStats.triggerCandidate;
                LogDetectionChanges(latestStats, previousLogStats, logger);
                if (latestStats.qteState == QteState::Triggered) {
                    const LARGE_INTEGER triggerQpc = QpcNow();
                    inputDisplay.triggerTimestamp = LocalTimestampText();
                    inputDisplay.inputTimestamp = L"--";
                    inputDisplay.inputLatencyMs = -1.0;
                    inputDisplay.inputSent = false;
                    if (inputDisplay.autoInputEnabled) {
                        const UINT sent = SendTargetKey(targetVirtualKey);
                        if (sent == 2) {
                            const LARGE_INTEGER inputQpc = QpcNow();
                            inputDisplay.inputSent = true;
                            ++inputDisplay.inputCount;
                            inputDisplay.inputTimestamp = LocalTimestampText();
                            inputDisplay.inputLatencyMs = QpcMilliseconds(triggerQpc, inputQpc);
                            inputDisplay.inputResult = L"SENT";
                            logger.Write(1, L"AUTO INPUT sent target key " + config.targetKey
                                + L" at " + inputDisplay.inputTimestamp + L"; latency="
                                + std::to_wstring(inputDisplay.inputLatencyMs) + L" ms");
                        } else {
                            inputDisplay.inputResult = L"SEND FAILED";
                            logger.Write(1, L"AUTO INPUT failed for target key " + config.targetKey
                                + L" (SendInput inserted " + std::to_wstring(sent) + L" of 2 events)");
                        }
                    } else {
                        inputDisplay.inputResult = L"AUTO INPUT OFF";
                    }
                }
                if (latestStats.qteState == QteState::Triggered && config.saveFrames) {
                    WriteFrameSnapshot(frame, (appDirectory / L"debug_frames").wstring(), logger);
                }
                UpdatePointerMotion(latestStats, frameTimestamp, pointerMotionTracker);
                if (enteredPerfect) {
                    LogPerfectEntryMeasurement(latestStats, pointerMotionTracker.display, logger);
                }
            } else {
                latestStats.triggerCandidate = false;
                latestStats.whiteLostCount = 0;
                latestStats.triggered = false;
            }
            performance.Record(captureMs, visionMs);

            LARGE_INTEGER now = QpcNow();
            if (QpcMilliseconds(lastUiUpdate, now) >= 100.0) {
                window.Update(latestStats, performance.Get(detectionRunning), inputDisplay,
                    pointerMotionTracker.display);
                lastUiUpdate = now;
            }
            if (!config.testMode) capture.EndFrame();

            if (config.testMode && config.testFrameIntervalMs > 0) {
                const LARGE_INTEGER delayStart = QpcNow();
                while (QpcMilliseconds(delayStart, QpcNow()) < static_cast<double>(config.testFrameIntervalMs)) {
                    window.PumpMessages();
                    if (window.ShouldExit()) break;
                    Sleep(1);
                }
            }
        }
    }

    capture.EndFrame();
    logger.Write(1, L"QTE Vision Debugger stopped");
    CoUninitialize();
    return 0;
}
