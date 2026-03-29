/**
 * @file nv12_video_frame.hpp
 * @brief 声明 agent/encoder/nv12_video_frame 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>
#include <vector>

namespace rdc::agent::encoder {

/**
 * @brief 描述 Nv12VideoFrame 的帧数据结构。
 */
struct Nv12VideoFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t luma_stride_bytes = 0;
    std::uint32_t chroma_stride_bytes = 0;
    std::int64_t present_qpc_ticks = 0;
    std::vector<std::uint8_t> bytes;
};

}  // namespace rdc::agent::encoder
