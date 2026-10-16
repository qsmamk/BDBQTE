#include "DxgiCapture.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {
std::wstring HResultText(const wchar_t* action, HRESULT hr) {
    std::wostringstream stream;
    stream << action << L" (HRESULT = 0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr) << L")";
    return stream.str();
}
}

DxgiCapture::~DxgiCapture() {
    EndFrame();
    ReleaseAll();
}

void DxgiCapture::ReleaseAll() noexcept {
    EndFrame();
    staging_.Reset();
    duplication_.Reset();
    output1_.Reset();
    output_.Reset();
    context_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
    stagingWidth_ = 0;
    stagingHeight_ = 0;
    stagingFormat_ = DXGI_FORMAT_UNKNOWN;
    sourceWidth_ = 0;
    sourceHeight_ = 0;
    copyRegion_ = {};
}

bool DxgiCapture::Initialize(int monitorIndex, std::wstring& error, const RECT* captureRegion) {
    ReleaseAll();
    monitorIndex_ = monitorIndex;
    hasCustomCaptureRegion_ = captureRegion != nullptr;
    desiredCaptureRegion_ = captureRegion != nullptr ? *captureRegion : RECT{};
    return CreateDeviceAndDuplication(monitorIndex, error);
}

bool DxgiCapture::CreateDeviceAndDuplication(int monitorIndex, std::wstring& error) {
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        error = HResultText(L"DXGI factory creation failed", hr);
        return false;
    }

    int currentIndex = 0;
    for (UINT adapterIndex = 0; ; ++adapterIndex) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidateAdapter;
        hr = factory_->EnumAdapters1(adapterIndex, &candidateAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            error = HResultText(L"DXGI adapter enumeration failed", hr);
            return false;
        }
        for (UINT outputIndex = 0; ; ++outputIndex) {
            Microsoft::WRL::ComPtr<IDXGIOutput> candidateOutput;
            hr = candidateAdapter->EnumOutputs(outputIndex, &candidateOutput);
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                error = HResultText(L"DXGI output enumeration failed", hr);
                return false;
            }
            if (currentIndex++ == monitorIndex) {
                adapter_ = candidateAdapter;
                output_ = candidateOutput;
                break;
            }
        }
        if (output_) {
            break;
        }
    }
    if (!adapter_ || !output_) {
        error = L"The configured monitor index was not found. Check [Capture] monitor in config.ini.";
        return false;
    }

    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    hr = D3D11CreateDevice(adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requestedLevels, static_cast<UINT>(std::size(requestedLevels)), D3D11_SDK_VERSION,
        &device_, &featureLevel, &context_);
    if (hr == E_INVALIDARG) {
        constexpr D3D_FEATURE_LEVEL fallbackLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            fallbackLevels, 1, D3D11_SDK_VERSION, &device_, &featureLevel, &context_);
    }
    if (FAILED(hr)) {
        error = HResultText(L"D3D11 device creation failed. Update the graphics driver and check that the selected adapter is enabled", hr);
        return false;
    }

    DXGI_OUTPUT_DESC outputDesc{};
    hr = output_->GetDesc(&outputDesc);
    if (FAILED(hr)) {
        error = HResultText(L"Could not read the selected display output", hr);
        return false;
    }
    desktopRect_ = outputDesc.DesktopCoordinates;
    sourceWidth_ = desktopRect_.right - desktopRect_.left;
    sourceHeight_ = desktopRect_.bottom - desktopRect_.top;

    hr = output_.As(&output1_);
    if (FAILED(hr)) {
        error = HResultText(L"The selected output does not support Desktop Duplication", hr);
        return false;
    }
    hr = output1_->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        error = HResultText(L"Output duplication creation failed. Close conflicting capture applications or check display permissions", hr);
        return false;
    }

    RECT fullRegion{ 0, 0, sourceWidth_, sourceHeight_ };
    copyRegion_ = fullRegion;
    if (hasCustomCaptureRegion_) {
        copyRegion_.left = std::max(fullRegion.left, desiredCaptureRegion_.left);
        copyRegion_.top = std::max(fullRegion.top, desiredCaptureRegion_.top);
        copyRegion_.right = std::min(fullRegion.right, desiredCaptureRegion_.right);
        copyRegion_.bottom = std::min(fullRegion.bottom, desiredCaptureRegion_.bottom);
    }
    if (copyRegion_.left >= copyRegion_.right || copyRegion_.top >= copyRegion_.bottom) {
        error = L"The configured QTE capture region does not overlap the selected monitor.";
        return false;
    }
    return CreateStagingTexture(copyRegion_.right - copyRegion_.left,
        copyRegion_.bottom - copyRegion_.top, DXGI_FORMAT_B8G8R8A8_UNORM, error);
}

bool DxgiCapture::CreateStagingTexture(int width, int height, DXGI_FORMAT format, std::wstring& error) {
    staging_.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    const HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging_);
    if (FAILED(hr)) {
        error = HResultText(L"Staging texture creation failed", hr);
        return false;
    }
    stagingWidth_ = width;
    stagingHeight_ = height;
    stagingFormat_ = format;
    return true;
}

CaptureStatus DxgiCapture::BeginFrame(FrameView& frame, HRESULT& result, std::wstring& error, CaptureStage stage) {
    EndFrame();
    if (!duplication_) {
        const RECT region = desiredCaptureRegion_;
        if (!Initialize(monitorIndex_, error, hasCustomCaptureRegion_ ? &region : nullptr)) {
            result = E_FAIL;
            return CaptureStatus::Failed;
        }
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> resource;
    result = duplication_->AcquireNextFrame(0, &frameInfo, &resource);
    if (result == DXGI_ERROR_WAIT_TIMEOUT) {
        return CaptureStatus::Timeout;
    }
    if (result == DXGI_ERROR_ACCESS_LOST) {
        const RECT region = desiredCaptureRegion_;
        if (Initialize(monitorIndex_, error, hasCustomCaptureRegion_ ? &region : nullptr)) {
            return CaptureStatus::Reinitialized;
        }
        result = E_FAIL;
        return CaptureStatus::Failed;
    }
    if (FAILED(result)) {
        error = HResultText(L"AcquireNextFrame failed", result);
        return CaptureStatus::Failed;
    }

    if (stage == CaptureStage::AcquireOnly) {
        result = duplication_->ReleaseFrame();
        if (FAILED(result)) {
            error = HResultText(L"Could not release the acquired desktop frame", result);
            return CaptureStatus::Failed;
        }
        frame.desktopRect = desktopRect_;
        return CaptureStatus::Frame;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> acquiredTexture;
    result = resource.As(&acquiredTexture);
    if (FAILED(result)) {
        duplication_->ReleaseFrame();
        error = HResultText(L"Could not access the captured D3D11 texture", result);
        return CaptureStatus::Failed;
    }

    D3D11_TEXTURE2D_DESC sourceDesc{};
    acquiredTexture->GetDesc(&sourceDesc);
    if (sourceDesc.Width != static_cast<UINT>(sourceWidth_) || sourceDesc.Height != static_cast<UINT>(sourceHeight_)) {
        sourceWidth_ = static_cast<int>(sourceDesc.Width);
        sourceHeight_ = static_cast<int>(sourceDesc.Height);
        RECT fullRegion{ 0, 0, sourceWidth_, sourceHeight_ };
        copyRegion_ = fullRegion;
        if (hasCustomCaptureRegion_) {
            copyRegion_.left = std::max(fullRegion.left, desiredCaptureRegion_.left);
            copyRegion_.top = std::max(fullRegion.top, desiredCaptureRegion_.top);
            copyRegion_.right = std::min(fullRegion.right, desiredCaptureRegion_.right);
            copyRegion_.bottom = std::min(fullRegion.bottom, desiredCaptureRegion_.bottom);
        }
    }
    const int requiredWidth = copyRegion_.right - copyRegion_.left;
    const int requiredHeight = copyRegion_.bottom - copyRegion_.top;
    if (requiredWidth <= 0 || requiredHeight <= 0) {
        duplication_->ReleaseFrame();
        error = L"The configured QTE capture region does not overlap the selected monitor.";
        return CaptureStatus::Failed;
    }
    if (requiredWidth != stagingWidth_ || requiredHeight != stagingHeight_ || sourceDesc.Format != stagingFormat_) {
        if (!CreateStagingTexture(requiredWidth, requiredHeight, sourceDesc.Format, error)) {
            duplication_->ReleaseFrame();
            return CaptureStatus::Failed;
        }
    }

    if (stage == CaptureStage::InspectWithoutCopy) {
        result = duplication_->ReleaseFrame();
        if (FAILED(result)) {
            error = HResultText(L"Could not release the acquired desktop frame", result);
            return CaptureStatus::Failed;
        }
        frame.width = stagingWidth_;
        frame.height = stagingHeight_;
        frame.originX = copyRegion_.left;
        frame.originY = copyRegion_.top;
        frame.desktopRect = desktopRect_;
        return CaptureStatus::Frame;
    }

    if (stage == CaptureStage::SmallRegionCopyWithoutReadback) {
        const UINT copyWidth = std::min<UINT>(64, sourceDesc.Width);
        const UINT copyHeight = std::min<UINT>(64, sourceDesc.Height);
        const D3D11_BOX sourceBox{ 0, 0, 0, copyWidth, copyHeight, 1 };
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, acquiredTexture.Get(), 0, &sourceBox);
        result = duplication_->ReleaseFrame();
        if (FAILED(result)) {
            error = HResultText(L"Could not release the acquired desktop frame", result);
            return CaptureStatus::Failed;
        }
        frame.width = stagingWidth_;
        frame.height = stagingHeight_;
        frame.originX = copyRegion_.left;
        frame.originY = copyRegion_.top;
        frame.desktopRect = desktopRect_;
        return CaptureStatus::Frame;
    }

    const bool fullOutputCopy = copyRegion_.left == 0 && copyRegion_.top == 0
        && copyRegion_.right == static_cast<LONG>(sourceDesc.Width)
        && copyRegion_.bottom == static_cast<LONG>(sourceDesc.Height)
        && stagingWidth_ == static_cast<int>(sourceDesc.Width)
        && stagingHeight_ == static_cast<int>(sourceDesc.Height);
    if (fullOutputCopy) {
        context_->CopyResource(staging_.Get(), acquiredTexture.Get());
    } else {
        const D3D11_BOX sourceBox{ static_cast<UINT>(copyRegion_.left), static_cast<UINT>(copyRegion_.top), 0,
            static_cast<UINT>(copyRegion_.right), static_cast<UINT>(copyRegion_.bottom), 1 };
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, acquiredTexture.Get(), 0, &sourceBox);
    }
    result = duplication_->ReleaseFrame();
    if (FAILED(result)) {
        error = HResultText(L"Could not release the acquired desktop frame", result);
        return CaptureStatus::Failed;
    }

    if (stage == CaptureStage::CopyWithoutReadback || stage == CaptureStage::CopyWithoutReadbackNoFlush) {
        if (stage == CaptureStage::CopyWithoutReadback) context_->Flush();
        frame.width = stagingWidth_;
        frame.height = stagingHeight_;
        frame.originX = copyRegion_.left;
        frame.originY = copyRegion_.top;
        frame.desktopRect = desktopRect_;
        return CaptureStatus::Frame;
    }

    result = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mappedResource_);
    if (FAILED(result)) {
        error = HResultText(L"Failed to map staging texture for CPU reading", result);
        return CaptureStatus::Failed;
    }
    mapped_ = true;
    frame.pixels = static_cast<const unsigned char*>(mappedResource_.pData);
    frame.width = stagingWidth_;
    frame.height = stagingHeight_;
    frame.rowPitch = static_cast<int>(mappedResource_.RowPitch);
    frame.originX = copyRegion_.left;
    frame.originY = copyRegion_.top;
    frame.desktopRect = desktopRect_;
    return CaptureStatus::Frame;
}

void DxgiCapture::EndFrame() noexcept {
    if (mapped_ && context_ && staging_) {
        context_->Unmap(staging_.Get(), 0);
    }
    mapped_ = false;
    mappedResource_ = {};
}
