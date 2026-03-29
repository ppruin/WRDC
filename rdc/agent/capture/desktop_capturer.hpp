/**
 * @file desktop_capturer.hpp
 * @brief 声明 agent/capture/desktop_capturer 相关的类型、函数与流程。
 */

#pragma once

#include <chrono>
#include <optional>

#include "desktop_frame.hpp"

namespace rdc::agent::capture {

/**
 * @brief 封装 DesktopCapturer 相关的采集或读取流程。
 */
class DesktopCapturer {
public:
    /**
     * @brief 析构 DesktopCapturer 对象并释放相关资源。
     */
    virtual ~DesktopCapturer() = default;

    /**
     * @brief 采集Next帧。
     * @param timeout 等待超时时间。
     * @return 返回可用结果；失败时返回空值。
     */
    virtual std::optional<DesktopFrame> CaptureNextFrame(std::chrono::milliseconds timeout) = 0;
};

}  // namespace rdc::agent::capture
