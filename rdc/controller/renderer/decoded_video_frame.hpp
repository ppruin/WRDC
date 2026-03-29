/**
 * @file decoded_video_frame.hpp
 * @brief 声明 controller/renderer/decoded_video_frame 相关的类型、函数与流程。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../../protocol/common/buffer_utils.hpp"

namespace rdc::controller::renderer {

/**
 * @brief 描述 DecodedVideoFrame 的帧数据结构。
 */
struct DecodedVideoFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride_bytes = 0;
    std::vector<std::uint8_t> bgra_bytes;

    /**
     * @brief 重置Storage。
     * @param frame_width 帧宽度。
     * @param frame_height 帧高度。
     */
    template <std::size_t BytesPerPixel = 4>
    void ResetStorage(const std::uint32_t frame_width, const std::uint32_t frame_height) {
        width = frame_width;
        height = frame_height;
        stride_bytes = frame_width * BytesPerPixel;
        protocol::common::ResizeIfSmaller(bgra_bytes,
                                          static_cast<std::size_t>(stride_bytes) * frame_height);
    }

    /**
     * @brief 交换相关流程。
     * @param other 用于交换的另一个对象。
     */
    void Swap(DecodedVideoFrame& other) noexcept {
        using std::swap;
        swap(width, other.width);
        swap(height, other.height);
        swap(stride_bytes, other.stride_bytes);
        bgra_bytes.swap(other.bgra_bytes);
    }
};

}  // namespace rdc::controller::renderer
