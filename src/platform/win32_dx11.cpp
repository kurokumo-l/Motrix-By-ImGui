module;

#include "stb_image_write.h"
#include <d3d11.h>
#include <dxgi.h>

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
    bool occluded = false;

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
    if (!impl_) {
        return;
    }
    impl_->rtv.Reset();
    impl_->swapChain.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
}

void Dx11Context::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_->swapChain || width == 0 || height == 0) {
        return;
    }
    impl_->rtv.Reset();
    impl_->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    impl_->CreateRenderTarget();
}

void Dx11Context::BeginRenderPass(float r, float g, float b, float a) {
    ID3D11RenderTargetView* target = impl_->rtv.Get();
    const float clearColor[4] = {r, g, b, a};
    impl_->context->OMSetRenderTargets(1, &target, nullptr);
    impl_->context->ClearRenderTargetView(target, clearColor);
}

bool Dx11Context::PresentFrame(bool vsync) {
    const HRESULT hr = impl_->swapChain->Present(vsync ? 1u : 0u, 0u);
    impl_->occluded = (hr == DXGI_STATUS_OCCLUDED);
    return impl_->occluded;
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

}  // namespace motrix::platform
