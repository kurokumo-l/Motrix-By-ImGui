export module motrix.ui:anim;

import std;

namespace motrix::ui {

export struct CachedTextureSlot {
    std::uint64_t texId = 0;
    int texW = 0;
    int texH = 0;
    bool dark = false;
    const void* owner = nullptr;
};

export class AnimState {
public:
    void BeginFrame(float dt) noexcept;
    void Set(std::uint32_t id, float value);
    [[nodiscard]] float
    Float(std::uint32_t id, float target, float speed = 18.0f, float initial = -1.0f);
    void PruneStale(std::uint64_t maxIdleFrames = 120u) noexcept;
    [[nodiscard]] bool AnyActive() const noexcept { return anyActive_; }
    [[nodiscard]] std::uint64_t FrameCount() const noexcept { return frameCount_; }
    [[nodiscard]] std::size_t EntryCount() const noexcept { return floats_.size(); }
    [[nodiscard]] CachedTextureSlot& UfoCache() noexcept { return ufoCache_; }
    [[nodiscard]] CachedTextureSlot& AppIconCache() noexcept { return appIconCache_; }
    void SetDeferredOverlay(std::function<void()> fn) { deferredOverlay_ = std::move(fn); }
    void FlushDeferredOverlay();
    void SetTooltipsEnabled(bool enabled) noexcept { tooltipsEnabled_ = enabled; }
    [[nodiscard]] bool TooltipsEnabled() const noexcept { return tooltipsEnabled_; }

private:
    struct FloatEntry {
        float value = 0.0f;
        std::uint64_t lastFrame = 0;
    };

    float dt_ = 1.0f / 60.0f;
    bool anyActive_ = false;
    bool tooltipsEnabled_ = true;
    std::uint64_t frameCount_ = 0;
    std::unordered_map<std::uint32_t, FloatEntry> floats_;
    CachedTextureSlot ufoCache_{};
    CachedTextureSlot appIconCache_{};
    std::function<void()> deferredOverlay_;
};

export struct AnimTargetSpec {
    std::uint32_t id = 0;
    float target = 0.0f;
    float speed = 18.0f;
    float initial = -1.0f;
};

export [[nodiscard]] float AnimStep(AnimState* anim, const AnimTargetSpec& spec);

export [[nodiscard]] float SmoothOpenProgress(AnimState& anim, std::uint32_t id, bool justOpened);

export [[nodiscard]] constexpr std::uint32_t HashId(std::string_view key) noexcept {
    std::uint32_t h = 2166136261u;
    for (unsigned char ch : key) {
        h ^= ch;
        h *= 16777619u;
    }
    return h;
}

}  // namespace motrix::ui
