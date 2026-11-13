module motrix.ui;

import std;

namespace motrix::ui {

void AnimState::BeginFrame(float dt) noexcept {
    dt_ = std::clamp(dt, 0.0f, 0.05f);
    anyActive_ = false;
}

float AnimState::Float(std::uint32_t id, float target, float speed, float initial) {
    auto it = floats_.find(id);
    if (it == floats_.end()) {
        const float start = (initial >= 0.0f) ? initial : target;
        floats_.emplace(id, start);
        if (std::fabs(start - target) > 0.002f) {
            anyActive_ = true;
        }
        return start;
    }
    if (std::fabs(it->second - target) <= 0.002f) {
        it->second = target;
        return target;
    }
    const float factor = 1.0f - std::exp(-speed * dt_);
    it->second += (target - it->second) * factor;
    if (std::fabs(it->second - target) <= 0.002f) {
        it->second = target;
    } else {
        anyActive_ = true;
    }
    return it->second;
}

}  // namespace motrix::ui
