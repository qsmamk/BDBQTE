#include "ImageIO.h"

#include <Windows.h>
#include <ocidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>

namespace {
std::wstring HrMessage(const wchar_t* action, HRESULT hr) {
    wchar_t text[32]{};
    swprintf_s(text, L" (HRESULT = 0x%08X)", static_cast<unsigned int>(hr));
    return std::wstring(action) + text;
}
}

bool LoadImageWic(const std::wstring& path, CpuImage& image, std::wstring& error) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error = HrMessage(L"Could not create the Windows Imaging Component factory", hr);
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        error = HrMessage((L"Could not open image: " + path).c_str(), hr);
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        error = HrMessage(L"Could not read the first image frame", hr);
        return false;
    }
    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0 || width > static_cast<UINT>(std::numeric_limits<int>::max() / 4)
        || height > static_cast<UINT>(std::numeric_limits<int>::max())) {
        error = FAILED(hr) ? HrMessage(L"Could not read image dimensions", hr) : L"Image dimensions are invalid or too large";
        return false;
    }
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        error = HrMessage(L"Could not create the image format converter", hr);
        return false;
    }
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        error = HrMessage(L"Could not convert image to BGRA pixels", hr);
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.stride = image.width * 4;
    image.pixels.resize(static_cast<size_t>(image.stride) * static_cast<size_t>(image.height));
    hr = converter->CopyPixels(nullptr, static_cast<UINT>(image.stride),
        static_cast<UINT>(image.pixels.size()), image.pixels.data());
    if (FAILED(hr)) {
        image = {};
        error = HrMessage(L"Could not copy decoded image pixels", hr);
        return false;
    }
    return true;
}

bool SaveBmpWic(const std::wstring& path, const CpuImage& image, std::wstring& error) {
    using Microsoft::WRL::ComPtr;
    if (image.width <= 0 || image.height <= 0 || image.stride < image.width * 4
        || image.pixels.size() < static_cast<size_t>(image.stride) * static_cast<size_t>(image.height)) {
        error = L"Cannot save an empty or invalid image";
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error = HrMessage(L"Could not create the Windows Imaging Component factory", hr);
        return false;
    }
    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    }
    if (FAILED(hr)) {
        error = HrMessage((L"Could not create reference image: " + path).c_str(), hr);
        return false;
    }
    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatBmp, nullptr, &encoder);
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, &properties);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(properties.Get());
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA) {
        error = L"The Windows BMP encoder did not accept BGRA pixels";
        return false;
    }
    if (SUCCEEDED(hr)) {
        hr = frame->WritePixels(static_cast<UINT>(image.height), static_cast<UINT>(image.stride),
            static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()));
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }
    if (FAILED(hr)) {
        error = HrMessage(L"Could not encode reference BMP", hr);
        return false;
    }
    return true;
}
