module motrix.ui;

import std;

namespace motrix::ui {

void AnimState::BeginFrame(float dt) noexcept {
    dt_ = std::clamp(dt, 0.0f, 0.05f);
    anyActive_ = false;
    ++frameCount_;
    deferredOverlay_ = nullptr;
    if ((frameCount_ & 63u) == 0u) {
        PruneStale();
    }
}

void AnimState::FlushDeferredOverlay() {
    if (!deferredOverlay_) {
        return;
    }
    auto fn = std::move(deferredOverlay_);
    deferredOverlay_ = nullptr;
    fn();
}

void AnimState::Set(std::uint32_t id, float value) {
    floats_[id] = FloatEntry{value, frameCount_};
}

float AnimState::Float(std::uint32_t id, float target, float speed, float initial) {
    auto it = floats_.find(id);
    if (it == floats_.end()) {
        const float start = (initial >= 0.0f) ? initial : target;
        floats_.emplace(id, FloatEntry{start, frameCount_});
        if (std::fabs(start - target) > 0.002f) {
            anyActive_ = true;
        }
        return start;
    }
    it->second.lastFrame = frameCount_;
    if (std::fabs(it->second.value - target) <= 0.002f) {
        it->second.value = target;
        return target;
    }
    const float factor = 1.0f - std::exp(-speed * dt_);
    it->second.value += (target - it->second.value) * factor;
    if (std::fabs(it->second.value - target) <= 0.002f) {
        it->second.value = target;
    } else {
        anyActive_ = true;
    }
    return it->second.value;
}

void AnimState::PruneStale(std::uint64_t maxIdleFrames) noexcept {
    std::erase_if(floats_, [&](const auto& item) noexcept {
        return frameCount_ >= item.second.lastFrame
               && (frameCount_ - item.second.lastFrame) > maxIdleFrames;
    });
    // Release peak hash-table bucket capacity once all stale entries clear out.
    if (floats_.empty() && floats_.bucket_count() > 64u) {
        std::unordered_map<std::uint32_t, FloatEntry> empty;
        floats_.swap(empty);
    }
}

float AnimStep(AnimState* anim, const AnimTargetSpec& spec) {
    if (anim == nullptr) {
        return spec.target;
    }
    return anim->Float(spec.id, spec.target, spec.speed, spec.initial);
}

float SmoothOpenProgress(AnimState& anim, std::uint32_t id, bool justOpened) {
    if (justOpened) {
        anim.Set(id, (anim.FrameCount() <= 2) ? 1.0f : 0.0f);
    }
    return anim.Float(id, 1.0f, 24.0f, 1.0f);
}

}  // namespace motrix::ui
