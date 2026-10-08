#pragma once

#include <string>

struct AppConfig {
    int monitorIndex = 0;

    int centerX = 1295;
    int centerY = 710;
    int radius = 100;
    int ringThickness = 22;
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
    float normalDensityThreshold = 0.12f;
    float normalDensityDelta = 0.08f;
    float perfectDensityThreshold = 0.62f;
    float perfectDensityDelta = 0.35f;
    float minZoneAngleDegrees = 2.0f;
    float maxZoneAngleDegrees = 110.0f;
    int referencePixelTolerance = 52;
    float qteMatchThreshold = 82.0f;
    int whiteLossThreshold = 4;
    int redPixelsThreshold = 2;

    bool showWindow = true;
    bool saveFrames = false;
    bool verbose = false;
    int logLevel = 1;
    bool plotOverlay = true;

    bool testMode = false;
    std::wstring testFramesDirectory = L"test_frames";
    int testFrameIntervalMs = 0;
    bool loopTestFrames = false;
};

bool LoadConfig(const std::wstring& path, AppConfig& config, std::wstring& error);
