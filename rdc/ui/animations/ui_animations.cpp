/**
 * @file ui_animations.cpp
 * @brief 实现 Windows GUI 动画与过渡辅助函数。
 */

#include "ui_animations.hpp"

namespace rdc::ui::animations {

float ClampAnimationProgress(const float progress) {
    if (progress <= 0.0F) {
        return 0.0F;
    }

    if (progress >= 1.0F) {
        return 1.0F;
    }

    return progress;
}

float EaseOutCubic(const float progress) {
    const float normalized_progress = ClampAnimationProgress(progress);
    const float inverse_progress = 1.0F - normalized_progress;
    return 1.0F - inverse_progress * inverse_progress * inverse_progress;
}

}  // namespace rdc::ui::animations
