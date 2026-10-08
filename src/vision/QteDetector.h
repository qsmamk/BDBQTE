#pragma once

#include "../capture/DxgiCapture.h"
#include "../config/Config.h"
#include "ImageIO.h"

#include <cstdint>
#include <vector>

struct AngleRange {
    bool valid = false;
    float startDegrees = 0.0f;
    float endDegrees = 0.0f;
    float spanDegrees = 0.0f;
};

struct QteStats {
    bool referenceLoaded = false;
    bool qteDetected = false;
    float qteMatchPercent = 0.0f;
    float qteTimeMs = 0.0f;
    float roiTimeMs = 0.0f;
    float visionTimeMs = 0.0f;

    AngleRange normalZone;
    AngleRange perfectZone;
    bool pointerDetected = false;
    float pointerAngleDegrees = 0.0f;
    int whitePixelCount = 0;
    int whiteLostCount = 0;
    int redPixelCount = 0;
    int targetRedPixelCount = 0;
    bool triggerCandidate = false;
};

class QteDetector {
public:
    explicit QteDetector(const AppConfig& config);

    bool LoadReference(const std::wstring& path, std::wstring& error);
    bool SaveReferenceFromFrame(const FrameView& frame, const std::wstring& path, std::wstring& error);
    bool ValidateFrame(const FrameView& frame, std::wstring& error) const;
    QteStats Process(const FrameView& frame);
    bool ReferenceLoaded() const noexcept { return referenceLoaded_; }
    const AppConfig& Config() const noexcept { return config_; }

private:
    bool IsFramePointValid(const FrameView& frame, int x, int y) const noexcept;
    bool IsCenterRoiValid(const FrameView& frame) const noexcept;

    AppConfig config_;
    CpuImage reference_;
    bool referenceLoaded_ = false;
    std::vector<float> whiteDensity_;
    std::vector<int> whiteDensityHistogram_;
    std::vector<int> redByAngle_;
    std::vector<std::uint8_t> normalMask_;
    std::vector<std::uint8_t> perfectMask_;
    std::vector<std::uint8_t> gapMask_;
    std::vector<std::uint8_t> currentWhiteMask_;
    std::vector<std::uint8_t> previousWhiteMask_;
    std::vector<std::uint8_t> previousPerfectAngles_;
    bool previousFrameHadQte_ = false;
};
