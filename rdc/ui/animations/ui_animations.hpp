/**
 * @file ui_animations.hpp
 * @brief 声明 Windows GUI 动画与过渡辅助函数。
 */

#pragma once

#include <cmath>

namespace rdc::ui::animations {

/**
 * @brief 将动画进度限制到 [0, 1] 区间。
 * @param progress 原始动画进度。
 * @return 返回规范化后的动画进度。
 */
float ClampAnimationProgress(float progress);

/**
 * @brief 对标准化进度应用三次缓出曲线。
 * @param progress 标准化动画进度。
 * @return 返回经过缓动后的进度值。
 */
float EaseOutCubic(float progress);

/**
 * @brief 对任意算术值做线性插值。
 * @tparam ValueType 待插值数值类型。
 * @param from 起始值。
 * @param to 目标值。
 * @param progress 插值进度。
 * @return 返回插值后的结果。
 */
template <typename ValueType>
ValueType Lerp(const ValueType from, const ValueType to, const float progress) {
    const float normalized_progress = ClampAnimationProgress(progress);
    return static_cast<ValueType>(from + (to - from) * normalized_progress);
}

/**
 * @brief 以指数平滑方式将当前值逼近目标值。
 * @tparam ValueType 待动画化数值类型。
 * @param current 当前值。
 * @param target 目标值。
 * @param delta_seconds 当前帧耗时。
 * @param response_speed 响应速度，越大越快。
 * @return 返回逼近后的结果。
 */
template <typename ValueType>
ValueType AnimateTowards(const ValueType current,
                         const ValueType target,
                         const float delta_seconds,
                         const float response_speed) {
    if (delta_seconds <= 0.0F || response_speed <= 0.0F) {
        return target;
    }

    const float interpolation = 1.0F - std::exp(-response_speed * delta_seconds);
    return Lerp(current, target, interpolation);
}

}  // namespace rdc::ui::animations
