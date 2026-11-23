module;

#include "stb_image_write.h"
#include <d3d11.h>
#include <dxgi1_3.h>

module motrix.platform;

import std;
import motrix.core;
import :dx11_internal;

namespace motrix::platform {

namespace {

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* raw = static_cast<const std::uint8_t*>(data);
    bytes->insert(bytes->end(), raw, raw + size);
}

}  // namespace

struct Dx11Context::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;
    std::vector<ComPtr<ID3D11ShaderResourceView>> userTextures;
    bool occluded = false;
    bool deviceLost = false;

    void CreateRenderTarget() {
        ComPtr<ID3D11Texture2D> backBuffer;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf())))
            && backBuffer) {
            device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv.ReleaseAndGetAddressOf());
        }
    }
};

Dx11Context::Dx11Context() : impl_(std::make_unique<Impl>()) {
}
Dx11Context::~Dx11Context() {
    Shutdown();
}

motrix::core::Result<void> Dx11Context::Initialize(void* hwndPtr) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL kLevels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = ::D3D11CreateDeviceAndSwapChain(nullptr,
                                                 D3D_DRIVER_TYPE_HARDWARE,
                                                 nullptr,
                                                 0,
                                                 kLevels,
                                                 2,
                                                 D3D11_SDK_VERSION,
                                                 &sd,
                                                 impl_->swapChain.ReleaseAndGetAddressOf(),
                                                 impl_->device.ReleaseAndGetAddressOf(),
                                                 &featureLevel,
                                                 impl_->context.ReleaseAndGetAddressOf());

    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = ::D3D11CreateDeviceAndSwapChain(nullptr,
                                             D3D_DRIVER_TYPE_WARP,
                                             nullptr,
                                             0,
                                             kLevels,
                                             2,
                                             D3D11_SDK_VERSION,
                                             &sd,
                                             impl_->swapChain.ReleaseAndGetAddressOf(),
                                             impl_->device.ReleaseAndGetAddressOf(),
                                             &featureLevel,
                                             impl_->context.ReleaseAndGetAddressOf());
    }

    if (FAILED(hr)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to create D3D11 device and swap chain"));
    }

    ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(impl_->swapChain->GetParent(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())))
        && factory) {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    impl_->CreateRenderTarget();
    return {};
}

void Dx11Context::Shutdown() {
    impl_->userTextures.clear();
    impl_->rtv.Reset();
    impl_->swapChain.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->occluded = false;
    impl_->deviceLost = false;
}

void Dx11Context::Trim() noexcept {
    if (!impl_->device) {
        return;
    }
    if (impl_->context) {
        impl_->context->ClearState();
        impl_->context->Flush();
    }
    ComPtr<IDXGIDevice3> dxgiDev3;
    if (SUCCEEDED(impl_->device->QueryInterface(IID_PPV_ARGS(dxgiDev3.ReleaseAndGetAddressOf())))
        && dxgiDev3) {
        dxgiDev3->Trim();
    }
}

bool Dx11Context::IsInitialized() const noexcept {
    return impl_ && static_cast<bool>(impl_->device);
}

bool Dx11Context::IsDeviceLost() const noexcept {
    return impl_ && impl_->deviceLost;
}

bool Dx11Context::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_ || !impl_->swapChain || width == 0 || height == 0) {
        return true;
    }
    impl_->rtv.Reset();
    const HRESULT hr = impl_->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            impl_->deviceLost = true;
        }
        return false;
    }
    impl_->CreateRenderTarget();
    return static_cast<bool>(impl_->rtv);
}

bool Dx11Context::BeginRenderPass(float r, float g, float b, float a) {
    if (!impl_ || !impl_->context || !impl_->rtv) {
        return false;
    }
    ID3D11RenderTargetView* target = impl_->rtv.Get();
    const float clearColor[4] = {r, g, b, a};
    impl_->context->OMSetRenderTargets(1, &target, nullptr);
    impl_->context->ClearRenderTargetView(target, clearColor);
    return true;
}

PresentResult Dx11Context::PresentFrame(bool vsync) {
    if (!impl_ || !impl_->swapChain || impl_->deviceLost) {
        return PresentResult::DeviceLost;
    }
    const HRESULT hr = impl_->swapChain->Present(vsync ? 1u : 0u, 0u);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        impl_->occluded = false;
        impl_->deviceLost = true;
        return PresentResult::DeviceLost;
    }
    impl_->occluded = (hr == DXGI_STATUS_OCCLUDED);
    return impl_->occluded ? PresentResult::Occluded : PresentResult::Ok;
}

bool Dx11Context::IsOccludedStillActive() {
    if (!impl_->occluded || !impl_->swapChain) {
        return false;
    }
    impl_->occluded = (impl_->swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED);
    return impl_->occluded;
}

bool Dx11Context::CaptureSwapChainPng(const std::filesystem::path& outputPath) {
    if (!impl_->swapChain || !impl_->device || !impl_->context) {
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(impl_->swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf())))
        || !backBuffer) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(impl_->device->CreateTexture2D(&stagingDesc, nullptr, staging.ReleaseAndGetAddressOf()))
        || !staging) {
        return false;
    }

    impl_->context->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(impl_->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }

    const int w = static_cast<int>(desc.Width);
    const int h = static_cast<int>(desc.Height);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* rowSrc = src + static_cast<std::size_t>(y) * mapped.RowPitch;
        std::uint8_t* rowDst =
            rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
        for (int x = 0; x < w; ++x) {
            rowDst[x * 4 + 0] = rowSrc[x * 4 + 0];
            rowDst[x * 4 + 1] = rowSrc[x * 4 + 1];
            rowDst[x * 4 + 2] = rowSrc[x * 4 + 2];
            rowDst[x * 4 + 3] = 255;
        }
    }
    impl_->context->Unmap(staging.Get(), 0);

    std::vector<std::uint8_t> pngBytes;
    if (stbi_write_png_to_func(&PngWriteCallback, &pngBytes, w, h, 4, rgba.data(), w * 4) == 0) {
        return false;
    }
    return motrix::core::WriteFileAtomic(outputPath, pngBytes).has_value();
}

void* Dx11Context::DeviceRaw() const noexcept {
    return impl_->device.Get();
}

void* Dx11Context::ContextRaw() const noexcept {
    return impl_->context.Get();
}

std::uint64_t Dx11Context::UploadRgbaTexture(std::uint64_t existingId,
                                             int width,
                                             int height,
                                             std::span<const std::uint8_t> rgba) {
    if (!impl_->device || width <= 0 || height <= 0) {
        return 0;
    }
    const auto needed = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() < needed) {
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{rgba.data(), static_cast<UINT>(width) * 4u, 0u};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(impl_->device->CreateTexture2D(&desc, &initData, tex.ReleaseAndGetAddressOf())) || !tex) {
        return 0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(
            impl_->device->CreateShaderResourceView(tex.Get(), &srvDesc, srv.ReleaseAndGetAddressOf()))
        || !srv) {
        return 0;
    }

    const auto outId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(srv.Get()));
    if (existingId != 0) {
        for (auto& slot : impl_->userTextures) {
            if (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(slot.Get())) == existingId) {
                slot = std::move(srv);
                return outId;
            }
        }
    }
    impl_->userTextures.push_back(std::move(srv));
    return outId;
}

}  // namespace motrix::platform
