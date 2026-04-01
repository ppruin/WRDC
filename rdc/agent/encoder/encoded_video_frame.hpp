/**
 * @file encoded_video_frame.hpp
 * @brief 声明 agent/encoder/encoded_video_frame 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>
#include <vector>

namespace rdc::agent::encoder {

/**
 * @brief 描述 EncodedVideoFrame 的帧数据结构。
 */
struct EncodedVideoFrame {
    std::vector<std::uint8_t> bytes;
    bool is_key_frame = false;
    std::int64_t sample_time_hns = 0;
    std::int64_t sample_duration_hns = 0;
};

}  // namespace rdc::agent::encoder
