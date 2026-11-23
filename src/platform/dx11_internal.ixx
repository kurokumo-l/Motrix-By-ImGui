export module motrix.platform:dx11_internal;

import std;
import motrix.core;

namespace motrix::platform {

export enum class PresentResult {
    Ok,
    Occluded,
    DeviceLost,
};

export class Dx11Context {
public:
    Dx11Context();
    ~Dx11Context();

    Dx11Context(const Dx11Context&) = delete;
    Dx11Context& operator=(const Dx11Context&) = delete;

    [[nodiscard]] motrix::core::Result<void> Initialize(void* hwnd);
    void Shutdown();
    void Trim() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsDeviceLost() const noexcept;

    [[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool BeginRenderPass(float r, float g, float b, float a);
    [[nodiscard]] PresentResult PresentFrame(bool vsync);
    [[nodiscard]] bool IsOccludedStillActive();
    [[nodiscard]] bool CaptureSwapChainPng(const std::filesystem::path& outputPath);

    [[nodiscard]] void* DeviceRaw() const noexcept;
    [[nodiscard]] void* ContextRaw() const noexcept;
    [[nodiscard]] std::uint64_t UploadRgbaTexture(std::uint64_t existingId,
                                                  int width,
                                                  int height,
                                                  std::span<const std::uint8_t> rgba);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motrix::platform
