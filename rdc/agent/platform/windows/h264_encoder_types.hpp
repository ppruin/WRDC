/**
 * @file h264_encoder_types.hpp
 * @brief 声明 H.264 编码后端与配置类型。
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace rdc::agent::platform::windows {

/**
 * @brief 定义 H.264 编码后端的取值。
 */
enum class H264EncoderBackend {
    Auto,
    MediaFoundation,
    X264,
};

/**
 * @brief 描述 H264EncoderConfig 的配置项。
 */
struct H264EncoderConfig {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps_num = 30;
    std::uint32_t fps_den = 1;
    std::uint32_t bitrate = 4'000'000;
    std::uint32_t gop_size = 60;
    H264EncoderBackend backend = H264EncoderBackend::Auto;
};

/**
 * @brief 将编码后端转换为可读字符串。
 * @param backend 编码后端枚举值。
 * @return 返回字符串视图。
 */
inline constexpr std::string_view ToString(const H264EncoderBackend backend) {
    switch (backend) {
    case H264EncoderBackend::Auto:
        return "auto";
    case H264EncoderBackend::MediaFoundation:
        return "media_foundation";
    case H264EncoderBackend::X264:
        return "x264";
    default:
        return "unknown";
    }
}

}  // namespace rdc::agent::platform::windows
