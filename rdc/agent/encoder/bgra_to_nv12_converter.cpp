/**
 * @file bgra_to_nv12_converter.cpp
 * @brief 实现 agent/encoder/bgra_to_nv12_converter 相关的类型、函数与流程。
 */

#include "bgra_to_nv12_converter.hpp"

#include <stdexcept>

#include "../../protocol/common/video_convert_utils.hpp"

namespace rdc::agent::encoder {

/**
 * @brief 转换相关流程。
 * @param frame 视频帧对象。
 * @return 返回对应结果。
 */
Nv12VideoFrame BgraToNv12Converter::Convert(const RawVideoFrame& frame) const {
    if (frame.pixel_format != RawVideoPixelFormat::Bgra8Unorm) {
        throw std::runtime_error("BgraToNv12Converter 仅支持 BGRA 输入");
    }

    if ((frame.width % 2) != 0 || (frame.height % 2) != 0) {
        throw std::runtime_error("当前 NV12 转换要求图像宽高为偶数");
    }

    if (frame.stride_bytes < frame.width * 4) {
        throw std::runtime_error("原始 BGRA 帧步长小于 width * 4");
    }

    Nv12VideoFrame nv12;
    nv12.width = frame.width;
    nv12.height = frame.height;
    nv12.luma_stride_bytes = frame.width;
    nv12.chroma_stride_bytes = frame.width;
    nv12.present_qpc_ticks = frame.present_qpc_ticks;
    nv12.bytes.resize(static_cast<std::size_t>(frame.width) * frame.height * 3 / 2);

    auto* y_plane = nv12.bytes.data();
    auto* uv_plane = y_plane + static_cast<std::size_t>(frame.width) * frame.height;

    const auto* src = frame.bytes.data();
    for (std::uint32_t y = 0; y < frame.height; y += 2) {
        const auto* src_row0 = src + static_cast<std::size_t>(y) * frame.stride_bytes;
        const auto* src_row1 = src + static_cast<std::size_t>(y + 1) * frame.stride_bytes;
        auto* y_row0 = y_plane + static_cast<std::size_t>(y) * nv12.luma_stride_bytes;
        auto* y_row1 = y_plane + static_cast<std::size_t>(y + 1) * nv12.luma_stride_bytes;
        auto* uv_row = uv_plane + static_cast<std::size_t>(y / 2) * nv12.chroma_stride_bytes;

        for (std::uint32_t x = 0; x < frame.width; x += 2) {
            const auto* p0 = src_row0 + static_cast<std::size_t>(x) * 4;
            const auto* p1 = p0 + 4;
            const auto* p2 = src_row1 + static_cast<std::size_t>(x) * 4;
            const auto* p3 = p2 + 4;

            const auto yuv0 = protocol::common::PackedBgrToBt601Limited<>(p0);
            const auto yuv1 = protocol::common::PackedBgrToBt601Limited<>(p1);
            const auto yuv2 = protocol::common::PackedBgrToBt601Limited<>(p2);
            const auto yuv3 = protocol::common::PackedBgrToBt601Limited<>(p3);

            y_row0[x] = protocol::common::ClampToByte<>(yuv0.y);
            y_row0[x + 1] = protocol::common::ClampToByte<>(yuv1.y);
            y_row1[x] = protocol::common::ClampToByte<>(yuv2.y);
            y_row1[x + 1] = protocol::common::ClampToByte<>(yuv3.y);

            const int u = (yuv0.u + yuv1.u + yuv2.u + yuv3.u) >> 2;
            const int v = (yuv0.v + yuv1.v + yuv2.v + yuv3.v) >> 2;
            uv_row[x] = protocol::common::ClampToByte<>(u);
            uv_row[x + 1] = protocol::common::ClampToByte<>(v);
        }
    }

    return nv12;
}

}  // namespace rdc::agent::encoder
