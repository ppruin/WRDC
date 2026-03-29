/**
 * @file raw_video_frame.hpp
 * @brief 声明 agent/encoder/raw_video_frame 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rdc::agent::encoder {

/**
 * @brief 定义 RawVideoPixelFormat 的枚举取值。
 */
enum class RawVideoPixelFormat {
    Bgra8Unorm
};

/**
 * @brief 描述 RawVideoFrame 的帧数据结构。
 */
struct RawVideoFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride_bytes = 0;
    RawVideoPixelFormat pixel_format = RawVideoPixelFormat::Bgra8Unorm;
    std::int64_t present_qpc_ticks = 0;
    std::vector<std::uint8_t> bytes;
};

/**
 * @brief 将输入值转换为字符串表示。
 * @param pixel_format 像素格式。
 * @return 返回生成的字符串结果。
 */
inline std::string ToString(const RawVideoPixelFormat pixel_format) {
    switch (pixel_format) {
    case RawVideoPixelFormat::Bgra8Unorm:
        return "bgra8_unorm";
    default:
        return "unknown";
    }
}

}  // namespace rdc::agent::encoder
