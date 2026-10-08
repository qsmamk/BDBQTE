#include "QteDetector.h"

#include "PixelUtils.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
LARGE_INTEGER QpcNow() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value;
}

float ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) noexcept {
    static const double frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<float>((static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0) / frequency);
}

float NormalizeDegrees(float degrees) noexcept {
    while (degrees < 0.0f) degrees += 360.0f;
    while (degrees >= 360.0f) degrees -= 360.0f;
    return degrees;
}

bool FindLargestArc(const std::vector<std::uint8_t>& mask, float minimumDegrees, float maximumDegrees, AngleRange& arc) {
    const int count = static_cast<int>(mask.size());
    if (count == 0 || std::none_of(mask.begin(), mask.end(), [](std::uint8_t value) { return value != 0; })) {
        return false;
    }

    int falseIndex = -1;
    for (int i = 0; i < count; ++i) {
        if (mask[static_cast<size_t>(i)] == 0) {
            falseIndex = i;
            break;
        }
    }
    // A full ring is usually the underlying ring itself, not a judgement arc.
    if (falseIndex < 0) {
        return false;
    }

    int bestStart = 0;
    int bestLength = 0;
    int runStart = 0;
    int runLength = 0;
    for (int offset = 1; offset <= count; ++offset) {
        const int index = (falseIndex + offset) % count;
        if (mask[static_cast<size_t>(index)] != 0) {
            if (runLength == 0) runStart = index;
            ++runLength;
            if (runLength > bestLength) {
                bestStart = runStart;
                bestLength = runLength;
            }
        } else {
            runLength = 0;
        }
    }
    const float span = static_cast<float>(bestLength) * 360.0f / static_cast<float>(count);
    if (span < minimumDegrees || span > maximumDegrees) {
        return false;
    }
    arc.valid = true;
    arc.startDegrees = NormalizeDegrees(static_cast<float>(bestStart) * 360.0f / static_cast<float>(count));
    arc.spanDegrees = span;
    arc.endDegrees = NormalizeDegrees(arc.startDegrees + span);
    return true;
}

bool AngleIndexInArc(int index, const AngleRange& arc, int steps) noexcept {
    if (!arc.valid) return false;
    float angle = static_cast<float>(index) * 360.0f / static_cast<float>(steps);
    const float distance = NormalizeDegrees(angle - arc.startDegrees);
    return distance <= arc.spanDegrees;
}

bool AngleInArc(float angle, const AngleRange& arc) noexcept {
    if (!arc.valid) return false;
    const float distance = NormalizeDegrees(angle - arc.startDegrees);
    return distance <= arc.spanDegrees;
}

void CloseSingleSampleGaps(std::vector<std::uint8_t>& mask, std::vector<std::uint8_t>& gapMask) {
    const int count = static_cast<int>(mask.size());
    if (count < 3) return;
    for (int i = 0; i < count; ++i) {
        const int previous = (i + count - 1) % count;
        const int next = (i + 1) % count;
        gapMask[static_cast<size_t>(i)] = mask[static_cast<size_t>(i)] == 0
            && mask[static_cast<size_t>(previous)] != 0
            && mask[static_cast<size_t>(next)] != 0 ? 1 : 0;
    }
    for (int i = 0; i < count; ++i) {
        if (gapMask[static_cast<size_t>(i)] != 0) mask[static_cast<size_t>(i)] = 1;
    }
}
}

QteDetector::QteDetector(const AppConfig& config)
    : config_(config),
      whiteDensity_(static_cast<size_t>(config.angleSteps)),
      whiteDensityHistogram_(static_cast<size_t>(config.radialSamples + 1)),
      redByAngle_(static_cast<size_t>(config.angleSteps)),
      normalMask_(static_cast<size_t>(config.angleSteps)),
      perfectMask_(static_cast<size_t>(config.angleSteps)),
      gapMask_(static_cast<size_t>(config.angleSteps)),
      currentWhiteMask_(static_cast<size_t>(config.angleSteps) * static_cast<size_t>(config.radialSamples)),
      previousWhiteMask_(currentWhiteMask_.size()),
      previousPerfectAngles_(static_cast<size_t>(config.angleSteps)) {
}

bool QteDetector::LoadReference(const std::wstring& path, std::wstring& error) {
    CpuImage loaded;
    if (!LoadImageWic(path, loaded, error)) {
        return false;
    }
    if (loaded.width != config_.centerRoiWidth || loaded.height != config_.centerRoiHeight) {
        error = L"reference.bmp dimensions do not match center_roi_width/center_roi_height in config.ini";
        return false;
    }
    reference_ = std::move(loaded);
    referenceLoaded_ = true;
    return true;
}

bool QteDetector::IsFramePointValid(const FrameView& frame, int x, int y) const noexcept {
    return frame.pixels != nullptr && x >= 0 && y >= 0 && x < frame.width && y < frame.height;
}

bool QteDetector::IsCenterRoiValid(const FrameView& frame) const noexcept {
    const auto right = static_cast<std::int64_t>(config_.centerRoiX) + config_.centerRoiWidth;
    const auto bottom = static_cast<std::int64_t>(config_.centerRoiY) + config_.centerRoiHeight;
    return frame.pixels != nullptr && config_.centerRoiX >= 0 && config_.centerRoiY >= 0
        && right <= frame.width && bottom <= frame.height;
}

bool QteDetector::ValidateFrame(const FrameView& frame, std::wstring& error) const {
    if (frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0
        || static_cast<std::int64_t>(frame.rowPitch) < static_cast<std::int64_t>(frame.width) * 4) {
        error = L"The captured frame has invalid dimensions or row pitch";
        return false;
    }
    if (!IsCenterRoiValid(frame)) {
        error = L"The center ROI is outside this monitor or test image. Adjust center_roi_x/y/width/height in config.ini.";
        return false;
    }
    const std::int64_t margin = static_cast<std::int64_t>(config_.radius)
        + (static_cast<std::int64_t>(config_.ringThickness) + 1) / 2 + 1;
    if (static_cast<std::int64_t>(config_.centerX) - margin < 0 || static_cast<std::int64_t>(config_.centerY) - margin < 0
        || static_cast<std::int64_t>(config_.centerX) + margin >= frame.width
        || static_cast<std::int64_t>(config_.centerY) + margin >= frame.height) {
        error = L"The configured ring is outside this monitor or test image. Adjust center_x, center_y, radius, or ring_thickness in config.ini.";
        return false;
    }
    return true;
}

bool QteDetector::SaveReferenceFromFrame(const FrameView& frame, const std::wstring& path, std::wstring& error) {
    if (!IsCenterRoiValid(frame)) {
        error = L"The configured center ROI is outside the captured image. Check center_roi_x/y/width/height in config.ini.";
        return false;
    }
    reference_.width = config_.centerRoiWidth;
    reference_.height = config_.centerRoiHeight;
    reference_.stride = reference_.width * 4;
    reference_.pixels.resize(static_cast<size_t>(reference_.stride) * static_cast<size_t>(reference_.height));
    for (int y = 0; y < reference_.height; ++y) {
        const auto* source = frame.pixels + static_cast<size_t>(config_.centerRoiY + y) * static_cast<size_t>(frame.rowPitch)
            + static_cast<size_t>(config_.centerRoiX) * 4;
        auto* destination = reference_.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(reference_.stride);
        std::copy_n(source, reference_.stride, destination);
    }
    if (!SaveBmpWic(path, reference_, error)) {
        return false;
    }
    referenceLoaded_ = true;
    previousFrameHadQte_ = false;
    return true;
}

QteStats QteDetector::Process(const FrameView& frame) {
    QteStats stats{};
    stats.referenceLoaded = referenceLoaded_;
    const LARGE_INTEGER totalStart = QpcNow();

    const LARGE_INTEGER qteStart = QpcNow();
    if (referenceLoaded_ && IsCenterRoiValid(frame)) {
        int matchingPixels = 0;
        const int pixelCount = config_.centerRoiWidth * config_.centerRoiHeight;
        for (int y = 0; y < config_.centerRoiHeight; ++y) {
            const auto* currentRow = reinterpret_cast<const BgraPixel*>(frame.pixels
                + static_cast<size_t>(config_.centerRoiY + y) * static_cast<size_t>(frame.rowPitch)
                + static_cast<size_t>(config_.centerRoiX) * 4);
            const auto* referenceRow = reinterpret_cast<const BgraPixel*>(reference_.pixels.data()
                + static_cast<size_t>(y) * static_cast<size_t>(reference_.stride));
            for (int x = 0; x < config_.centerRoiWidth; ++x) {
                const int redDifference = std::abs(static_cast<int>(currentRow[x].r) - referenceRow[x].r);
                const int greenDifference = std::abs(static_cast<int>(currentRow[x].g) - referenceRow[x].g);
                const int blueDifference = std::abs(static_cast<int>(currentRow[x].b) - referenceRow[x].b);
                if (std::max({ redDifference, greenDifference, blueDifference }) <= config_.referencePixelTolerance) {
                    ++matchingPixels;
                }
            }
        }
        stats.qteMatchPercent = 100.0f * static_cast<float>(matchingPixels) / static_cast<float>(pixelCount);
        stats.qteDetected = stats.qteMatchPercent >= config_.qteMatchThreshold;
    }
    stats.qteTimeMs = ElapsedMs(qteStart, QpcNow());

    if (!stats.qteDetected) {
        qteState_ = QteState::WaitingQte;
        lockedPerfectZone_ = {};
        triggered_ = false;
        previousFrameHadQte_ = false;
        std::fill(previousPerfectAngles_.begin(), previousPerfectAngles_.end(), static_cast<std::uint8_t>(0));
    } else if (qteState_ == QteState::WaitingQte) {
        qteState_ = QteState::DetectingPerfect;
    }

    const LARGE_INTEGER roiStart = QpcNow();
    const float halfThickness = static_cast<float>(config_.ringThickness) * 0.5f;
    int whiteTotal = 0;
    int redTotal = 0;
    std::fill(redByAngle_.begin(), redByAngle_.end(), 0);
    for (int angleIndex = 0; angleIndex < config_.angleSteps; ++angleIndex) {
        const double angle = (2.0 * std::numbers::pi * static_cast<double>(angleIndex)) / static_cast<double>(config_.angleSteps);
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        int whiteCount = 0;
        int validSamples = 0;
        for (int radialIndex = 0; radialIndex < config_.radialSamples; ++radialIndex) {
            const float radialOffset = config_.radialSamples == 1 ? 0.0f
                : (-halfThickness + (2.0f * halfThickness * static_cast<float>(radialIndex)
                    / static_cast<float>(config_.radialSamples - 1)));
            const float sampleRadius = static_cast<float>(config_.radius) + radialOffset;
            const int x = config_.centerX + static_cast<int>(std::lround(sampleRadius * cosine));
            const int y = config_.centerY - static_cast<int>(std::lround(sampleRadius * sine));
            const size_t maskIndex = static_cast<size_t>(angleIndex) * static_cast<size_t>(config_.radialSamples)
                + static_cast<size_t>(radialIndex);
            bool isWhite = false;
            bool isRed = false;
            if (IsFramePointValid(frame, x, y)) {
                const auto* pixel = reinterpret_cast<const BgraPixel*>(frame.pixels
                    + static_cast<size_t>(y) * static_cast<size_t>(frame.rowPitch) + static_cast<size_t>(x) * 4);
                isRed = IsRedPixel(*pixel, config_.redThreshold, config_.redRatio);
                isWhite = !isRed && IsWhitePixel(*pixel, config_.whiteThreshold, config_.whiteChannelTolerance);
                ++validSamples;
            }
            currentWhiteMask_[maskIndex] = isWhite ? 1 : 0;
            if (isWhite) {
                ++whiteCount;
                ++whiteTotal;
            }
            if (isRed) {
                ++redByAngle_[static_cast<size_t>(angleIndex)];
                ++redTotal;
            }
        }
        whiteDensity_[static_cast<size_t>(angleIndex)] = validSamples > 0
            ? static_cast<float>(whiteCount) / static_cast<float>(validSamples) : 0.0f;
    }
    stats.whitePixelCount = whiteTotal;
    stats.redPixelCount = redTotal;

    std::fill(whiteDensityHistogram_.begin(), whiteDensityHistogram_.end(), 0);
    for (const float density : whiteDensity_) {
        const int bucket = std::clamp(static_cast<int>(std::lround(density * static_cast<float>(config_.radialSamples))),
            0, config_.radialSamples);
        ++whiteDensityHistogram_[static_cast<size_t>(bucket)];
    }
    int cumulativeAngles = 0;
    const int medianBucket = config_.angleSteps / 2;
    float medianRingDensity = 0.0f;
    for (int bucket = 0; bucket <= config_.radialSamples; ++bucket) {
        cumulativeAngles += whiteDensityHistogram_[static_cast<size_t>(bucket)];
        if (cumulativeAngles > medianBucket) {
            medianRingDensity = static_cast<float>(bucket) / static_cast<float>(config_.radialSamples);
            break;
        }
    }
    // The median models a continuous ring/background line; target regions add white density above it.
    const float normalCutoff = std::max(config_.normalDensityThreshold, medianRingDensity + config_.normalDensityDelta);
    const float perfectCutoff = std::max(config_.perfectDensityThreshold, medianRingDensity + config_.perfectDensityDelta);
    for (int i = 0; i < config_.angleSteps; ++i) {
        normalMask_[static_cast<size_t>(i)] = whiteDensity_[static_cast<size_t>(i)] >= normalCutoff ? 1 : 0;
        perfectMask_[static_cast<size_t>(i)] = whiteDensity_[static_cast<size_t>(i)] >= perfectCutoff ? 1 : 0;
    }
    CloseSingleSampleGaps(normalMask_, gapMask_);
    CloseSingleSampleGaps(perfectMask_, gapMask_);
    FindLargestArc(normalMask_, config_.minZoneAngleDegrees, config_.maxZoneAngleDegrees, stats.normalZone);
    FindLargestArc(perfectMask_, config_.minZoneAngleDegrees, config_.maxZoneAngleDegrees, stats.perfectZone);
    // Center recognition anchors the ring scan to an actual QTE, avoiding UI/background noise.
    if (!stats.qteDetected) {
        stats.normalZone = {};
        stats.perfectZone = {};
    }

    // Preserve the first reliable perfect arc for the entire QTE, even when the pointer covers it.
    if (stats.qteDetected && qteState_ == QteState::DetectingPerfect && stats.perfectZone.valid) {
        lockedPerfectZone_ = stats.perfectZone;
        qteState_ = QteState::PerfectLocked;
    }

    double directionX = 0.0;
    double directionY = 0.0;
    for (int angleIndex = 0; angleIndex < config_.angleSteps; ++angleIndex) {
        const int count = redByAngle_[static_cast<size_t>(angleIndex)];
        if (count <= 0) continue;
        const double angle = (2.0 * std::numbers::pi * static_cast<double>(angleIndex)) / static_cast<double>(config_.angleSteps);
        directionX += static_cast<double>(count) * std::cos(angle);
        directionY += static_cast<double>(count) * std::sin(angle);
        const bool inCurrentTarget = AngleIndexInArc(angleIndex, stats.perfectZone, config_.angleSteps);
        const bool inPreviousTarget = previousFrameHadQte_ && previousPerfectAngles_[static_cast<size_t>(angleIndex)] != 0;
        if (inCurrentTarget || inPreviousTarget) {
            stats.targetRedPixelCount += count;
        }
    }
    stats.pointerDetected = stats.qteDetected && redTotal >= config_.redPixelsThreshold
        && std::hypot(directionX, directionY) > 0.0;
    if (stats.pointerDetected) {
        stats.pointerAngleDegrees = NormalizeDegrees(static_cast<float>(std::atan2(directionY, directionX)
            * 180.0 / std::numbers::pi));
    }

    if (stats.qteDetected && previousFrameHadQte_) {
        for (int angleIndex = 0; angleIndex < config_.angleSteps; ++angleIndex) {
            if (!previousPerfectAngles_[static_cast<size_t>(angleIndex)]) continue;
            for (int radialIndex = 0; radialIndex < config_.radialSamples; ++radialIndex) {
                const size_t index = static_cast<size_t>(angleIndex) * static_cast<size_t>(config_.radialSamples)
                    + static_cast<size_t>(radialIndex);
                if (previousWhiteMask_[index] != 0 && currentWhiteMask_[index] == 0) {
                    ++stats.whiteLostCount;
                }
            }
        }
    }
    stats.perfectLocked = stats.qteDetected && lockedPerfectZone_.valid;
    stats.lockedPerfectZone = stats.perfectLocked ? lockedPerfectZone_ : AngleRange{};
    stats.triggerCandidate = stats.perfectLocked && stats.pointerDetected
        && AngleInArc(stats.pointerAngleDegrees, lockedPerfectZone_);
    if (stats.triggerCandidate && !triggered_) {
        triggered_ = true;
        qteState_ = QteState::Triggered;
    } else if (triggered_ && qteState_ == QteState::Triggered) {
        qteState_ = QteState::WaitQteEnd;
    }
    stats.triggered = triggered_;
    stats.qteState = qteState_;

    std::copy(currentWhiteMask_.begin(), currentWhiteMask_.end(), previousWhiteMask_.begin());
    for (int i = 0; i < config_.angleSteps; ++i) {
        previousPerfectAngles_[static_cast<size_t>(i)] = stats.qteDetected
            && AngleIndexInArc(i, stats.perfectZone, config_.angleSteps) ? 1 : 0;
    }
    previousFrameHadQte_ = stats.qteDetected;

    stats.roiTimeMs = ElapsedMs(roiStart, QpcNow());
    stats.visionTimeMs = ElapsedMs(totalStart, QpcNow());
    (void)redTotal;
    return stats;
}
