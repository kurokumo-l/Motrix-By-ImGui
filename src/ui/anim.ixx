export module motrix.ui:anim;

import std;

namespace motrix::ui {

export class AnimState {
public:
    void BeginFrame(float dt) noexcept;
    [[nodiscard]] float
    Float(std::uint32_t id, float target, float speed = 18.0f, float initial = -1.0f);
    [[nodiscard]] bool AnyActive() const noexcept { return anyActive_; }

private:
    float dt_ = 1.0f / 60.0f;
    bool anyActive_ = false;
    std::unordered_map<std::uint32_t, float> floats_;
};

export [[nodiscard]] constexpr std::uint32_t HashId(std::string_view key) noexcept {
    std::uint32_t h = 2166136261u;
    for (unsigned char ch : key) {
        h ^= ch;
        h *= 16777619u;
    }
    return h;
}

}  // namespace motrix::ui
