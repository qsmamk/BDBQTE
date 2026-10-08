#pragma once

#include <string>
#include <vector>

struct CpuImage {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<unsigned char> pixels;
};

bool LoadImageWic(const std::wstring& path, CpuImage& image, std::wstring& error);
bool SaveBmpWic(const std::wstring& path, const CpuImage& image, std::wstring& error);
