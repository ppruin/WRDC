/**
 * @file video_convert_utils.hpp
 * @brief 声明 protocol/common/video_convert_utils 相关的类型、函数与流程。
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace rdc::protocol::common {

/**
 * @brief 描述一次 YUV 采样结果。
 */
struct YuvSample {
    int y = 0;
    int u = 0;
    int v = 0;
};

/**
 * @brief 将数值限制到单字节范围内。
 * @param value 输入值。
 * @return 返回计算得到的数值结果。
 */
template <typename Byte = std::uint8_t>
constexpr Byte ClampToByte(const int value) {
    return static_cast<Byte>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

/**
 * @brief 将打包 BGR 像素转换为 BT.601 有限范围 YUV。
 * @param pixel pixel。
 * @return 返回计算得到的数值结果。
 */
template <std::size_t BlueIndex = 0, std::size_t GreenIndex = 1, std::size_t RedIndex = 2>
inline YuvSample PackedBgrToBt601Limited(const std::uint8_t* pixel) {
    const int b = pixel[BlueIndex];
    const int g = pixel[GreenIndex];
    const int r = pixel[RedIndex];

    return YuvSample{
        .y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16,
        .u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128,
        .v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128,
    };
}

/**
 * @brief 执行 StorePackedBGRA 相关处理。
 * @param pixel pixel。
 * @param blue blue。
 * @param green green。
 * @param red red。
 * @param alpha alpha。
 */
template <std::size_t BlueIndex = 0,
          std::size_t GreenIndex = 1,
          std::size_t RedIndex = 2,
          std::size_t AlphaIndex = 3>
inline void StorePackedBgra(std::uint8_t* pixel,
                            const int blue,
                            const int green,
                            const int red,
                            const std::uint8_t alpha = 255) {
    pixel[BlueIndex] = ClampToByte<>(blue);
    pixel[GreenIndex] = ClampToByte<>(green);
    pixel[RedIndex] = ClampToByte<>(red);
    pixel[AlphaIndex] = alpha;
}

/**
 * @brief 转换I420帧转换为BGRA。
 * @param y_plane yplane。
 * @param stride_y stridey。
 * @param u_plane uplane。
 * @param v_plane vplane。
 * @param stride_uv strideuv。
 * @param width width。
 * @param height height。
 * @param bgra_bytes bgrabytes。
 * @param bgra_stride_bytes bgrastridebytes。
 */
template <std::size_t BlueIndex = 0,
          std::size_t GreenIndex = 1,
          std::size_t RedIndex = 2,
          std::size_t AlphaIndex = 3>
inline void ConvertI420FrameToBgra(const std::uint8_t* y_plane,
                                   const int stride_y,
                                   const std::uint8_t* u_plane,
                                   const std::uint8_t* v_plane,
                                   const int stride_uv,
                                   const std::uint32_t width,
                                   const std::uint32_t height,
                                   std::uint8_t* bgra_bytes,
                                   const std::uint32_t bgra_stride_bytes) {
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* y_row = y_plane + static_cast<std::size_t>(y) * stride_y;
        const auto* u_row = u_plane + static_cast<std::size_t>(y / 2) * stride_uv;
        const auto* v_row = v_plane + static_cast<std::size_t>(y / 2) * stride_uv;
        auto* bgra_row = bgra_bytes + static_cast<std::size_t>(y) * bgra_stride_bytes;

        for (std::uint32_t x = 0; x < width; ++x) {
            const int y_value = static_cast<int>(y_row[x]);
            const int u_value = static_cast<int>(u_row[x / 2]);
            const int v_value = static_cast<int>(v_row[x / 2]);

            const int c = y_value - 16;
            const int d = u_value - 128;
            const int e = v_value - 128;
            const int red = (298 * c + 409 * e + 128) >> 8;
            const int green = (298 * c - 100 * d - 208 * e + 128) >> 8;
            const int blue = (298 * c + 516 * d + 128) >> 8;

            StorePackedBgra<BlueIndex, GreenIndex, RedIndex, AlphaIndex>(
                bgra_row + static_cast<std::size_t>(x) * 4,
                blue,
                green,
                red);
        }
    }
}

}  // namespace rdc::protocol::common
