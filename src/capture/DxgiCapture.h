#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>

struct FrameView {
    const unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    int rowPitch = 0;
    // Pixel coordinates in the monitor output represented by pixels[0, 0].
    int originX = 0;
    int originY = 0;
    RECT desktopRect{};
};

enum class CaptureStatus {
    Frame,
    Timeout,
    Reinitialized,
    Failed,
};

enum class CaptureStage {
    Full,
    AcquireOnly,
    InspectWithoutCopy,
    SmallRegionCopyWithoutReadback,
    CopyWithoutReadback,
    CopyWithoutReadbackNoFlush,
};

class DxgiCapture {
public:
    DxgiCapture() = default;
    ~DxgiCapture();
    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;

    bool Initialize(int monitorIndex, std::wstring& error, const RECT* captureRegion = nullptr);
    CaptureStatus BeginFrame(FrameView& frame, HRESULT& result, std::wstring& error,
        CaptureStage stage = CaptureStage::Full);
    void EndFrame() noexcept;

private:
    bool CreateDeviceAndDuplication(int monitorIndex, std::wstring& error);
    bool CreateStagingTexture(int width, int height, DXGI_FORMAT format, std::wstring& error);
    void ReleaseAll() noexcept;

    int monitorIndex_ = 0;
    bool hasCustomCaptureRegion_ = false;
    RECT desiredCaptureRegion_{};
    RECT copyRegion_{};
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
    bool mapped_ = false;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<IDXGIOutput> output_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    D3D11_MAPPED_SUBRESOURCE mappedResource_{};
    RECT desktopRect_{};
    int stagingWidth_ = 0;
    int stagingHeight_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;
};
