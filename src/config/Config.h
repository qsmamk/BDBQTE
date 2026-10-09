#pragma once

#include <string>

struct AppConfig {
    int monitorIndex = 0;

    int centerX = 1278;
    int centerY = 708;
    int radius = 87;
    int ringThickness = 12;
    int centerRoiX = 1275;
    int centerRoiY = 700;
    int centerRoiWidth = 12;
    int centerRoiHeight = 15;

    int whiteThreshold = 190;
    int whiteChannelTolerance = 65;
    int redThreshold = 145;
    float redRatio = 1.55f;
    int angleSteps = 720;
    int radialSamples = 13;
    float normalDensityThreshold = 0.10f;
    float normalDensityDelta = 0.04f;
    float perfectDensityThreshold = 0.62f;
    float perfectDensityDelta = 0.35f;
    float minZoneAngleDegrees = 2.0f;
    float maxZoneAngleDegrees = 110.0f;
    int referencePixelTolerance = 52;
    float qteMatchThreshold = 82.0f;
    int redPixelsThreshold = 2;

    std::wstring targetKey = L"E";
    float triggerOffsetDegrees = 5.0f;
};

bool LoadConfig(const std::wstring& path, AppConfig& config, std::wstring& error);
