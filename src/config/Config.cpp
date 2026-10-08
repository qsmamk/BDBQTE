#include "Config.h"

#include <Windows.h>

#include <cwchar>
#include <cwctype>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

float ReadFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key, float fallback) {
    wchar_t buffer[64]{};
    const wchar_t* fallbackText = L"";
    GetPrivateProfileStringW(section, key, fallbackText, buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (buffer[0] == L'\0') {
        return fallback;
    }
    wchar_t* end = nullptr;
    const double value = std::wcstod(buffer, &end);
    if (end == buffer || *end != L'\0' || !std::isfinite(value)
        || value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max()) {
        return fallback;
    }
    return static_cast<float>(value);
}

bool ReadBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback) {
    wchar_t buffer[16]{};
    GetPrivateProfileStringW(section, key, fallback ? L"true" : L"false", buffer,
        static_cast<DWORD>(std::size(buffer)), path.c_str());
    return _wcsicmp(buffer, L"true") == 0 || wcscmp(buffer, L"1") == 0 || _wcsicmp(buffer, L"yes") == 0;
}
}

bool LoadConfig(const std::wstring& path, AppConfig& c, std::wstring& error) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        error = L"Could not find config.ini at: " + path;
        return false;
    }

    c.monitorIndex = ReadInt(path, L"Capture", L"monitor", c.monitorIndex);
    c.centerX = ReadInt(path, L"QTE", L"center_x", c.centerX);
    c.centerY = ReadInt(path, L"QTE", L"center_y", c.centerY);
    c.radius = ReadInt(path, L"QTE", L"radius", c.radius);
    c.ringThickness = ReadInt(path, L"QTE", L"ring_thickness", c.ringThickness);
    c.centerRoiX = ReadInt(path, L"QTE", L"center_roi_x", c.centerRoiX);
    c.centerRoiY = ReadInt(path, L"QTE", L"center_roi_y", c.centerRoiY);
    c.centerRoiWidth = ReadInt(path, L"QTE", L"center_roi_width", c.centerRoiWidth);
    c.centerRoiHeight = ReadInt(path, L"QTE", L"center_roi_height", c.centerRoiHeight);

    c.whiteThreshold = ReadInt(path, L"Vision", L"white_threshold", c.whiteThreshold);
    c.whiteChannelTolerance = ReadInt(path, L"Vision", L"white_channel_tolerance", c.whiteChannelTolerance);
    c.redThreshold = ReadInt(path, L"Vision", L"red_threshold", c.redThreshold);
    c.redRatio = ReadFloat(path, L"Vision", L"red_ratio", c.redRatio);
    c.angleSteps = ReadInt(path, L"Vision", L"angle_steps", c.angleSteps);
    c.radialSamples = ReadInt(path, L"Vision", L"radial_samples", c.radialSamples);
    c.normalDensityThreshold = ReadFloat(path, L"Vision", L"normal_density_threshold", c.normalDensityThreshold);
    c.normalDensityDelta = ReadFloat(path, L"Vision", L"normal_density_delta", c.normalDensityDelta);
    c.perfectDensityThreshold = ReadFloat(path, L"Vision", L"perfect_density_threshold", c.perfectDensityThreshold);
    c.perfectDensityDelta = ReadFloat(path, L"Vision", L"perfect_density_delta", c.perfectDensityDelta);
    c.minZoneAngleDegrees = ReadFloat(path, L"Vision", L"min_zone_angle_deg", c.minZoneAngleDegrees);
    c.maxZoneAngleDegrees = ReadFloat(path, L"Vision", L"max_zone_angle_deg", c.maxZoneAngleDegrees);
    c.referencePixelTolerance = ReadInt(path, L"Vision", L"reference_pixel_tolerance", c.referencePixelTolerance);
    c.qteMatchThreshold = ReadFloat(path, L"Vision", L"qte_match_threshold", c.qteMatchThreshold);
    c.whiteLossThreshold = ReadInt(path, L"Vision", L"white_loss_threshold", c.whiteLossThreshold);
    c.redPixelsThreshold = ReadInt(path, L"Vision", L"red_pixels_threshold", c.redPixelsThreshold);

    wchar_t targetKey[16]{};
    GetPrivateProfileStringW(L"Input", L"target_key", c.targetKey.c_str(), targetKey,
        static_cast<DWORD>(std::size(targetKey)), path.c_str());
    c.targetKey = targetKey;
    if (c.targetKey.size() == 1) {
        c.targetKey[0] = static_cast<wchar_t>(towupper(c.targetKey[0]));
    }

    c.showWindow = ReadBool(path, L"Debug", L"show_window", c.showWindow);
    c.saveFrames = ReadBool(path, L"Debug", L"save_frames", c.saveFrames);
    c.verbose = ReadBool(path, L"Debug", L"verbose", c.verbose);
    c.logLevel = ReadInt(path, L"Debug", L"log_level", c.logLevel);
    c.plotOverlay = ReadBool(path, L"Debug", L"plot_overlay", c.plotOverlay);

    c.testMode = ReadBool(path, L"Test", L"test_mode", c.testMode);
    wchar_t directory[ MAX_PATH ]{};
    GetPrivateProfileStringW(L"Test", L"test_frames_dir", c.testFramesDirectory.c_str(), directory,
        static_cast<DWORD>(std::size(directory)), path.c_str());
    c.testFramesDirectory = directory;
    c.testFrameIntervalMs = ReadInt(path, L"Test", L"frame_interval_ms", c.testFrameIntervalMs);
    c.loopTestFrames = ReadBool(path, L"Test", L"loop", c.loopTestFrames);

    if (c.monitorIndex < 0 || c.radius <= 0 || c.ringThickness < 1
        || static_cast<std::int64_t>(c.ringThickness) >= static_cast<std::int64_t>(c.radius) * 2
        || c.centerRoiWidth < 1 || c.centerRoiHeight < 1
        || c.angleSteps < 36 || c.angleSteps > 4096 || c.radialSamples < 3 || c.radialSamples > 128
        || c.whiteThreshold < 0 || c.whiteThreshold > 255 || c.whiteChannelTolerance < 0 || c.whiteChannelTolerance > 255
        || c.redThreshold < 0 || c.redThreshold > 255 || !std::isfinite(c.redRatio) || c.redRatio <= 0.0f
        || !std::isfinite(c.normalDensityThreshold) || c.normalDensityThreshold < 0.0f || c.normalDensityThreshold > 1.0f
        || !std::isfinite(c.normalDensityDelta) || c.normalDensityDelta < 0.0f || c.normalDensityDelta > 1.0f
        || !std::isfinite(c.perfectDensityThreshold) || c.perfectDensityThreshold < 0.0f || c.perfectDensityThreshold > 1.0f
        || !std::isfinite(c.perfectDensityDelta) || c.perfectDensityDelta < 0.0f || c.perfectDensityDelta > 1.0f
        || c.normalDensityThreshold > c.perfectDensityThreshold || c.normalDensityDelta > c.perfectDensityDelta
        || !std::isfinite(c.minZoneAngleDegrees) || c.minZoneAngleDegrees <= 0.0f
        || !std::isfinite(c.maxZoneAngleDegrees) || c.maxZoneAngleDegrees < c.minZoneAngleDegrees || c.maxZoneAngleDegrees > 360.0f
        || c.referencePixelTolerance < 0 || c.referencePixelTolerance > 255
        || !std::isfinite(c.qteMatchThreshold) || c.qteMatchThreshold < 0.0f || c.qteMatchThreshold > 100.0f
        || c.whiteLossThreshold < 0 || c.redPixelsThreshold < 0
        || c.targetKey.size() != 1
        || !((c.targetKey[0] >= L'A' && c.targetKey[0] <= L'Z') || (c.targetKey[0] >= L'0' && c.targetKey[0] <= L'9'))
        || c.logLevel < 0 || c.logLevel > 2 || c.testFrameIntervalMs < 0) {
        error = L"One or more config.ini values are outside their supported range. Check QTE, Vision, Input, Debug, and Test settings. Input target_key must be one letter A-Z or digit 0-9.";
        return false;
    }
    return true;
}
