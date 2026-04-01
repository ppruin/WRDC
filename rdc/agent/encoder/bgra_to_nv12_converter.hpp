/**
 * @file bgra_to_nv12_converter.hpp
 * @brief 声明 agent/encoder/bgra_to_nv12_converter 相关的类型、函数与流程。
 */

#pragma once

#include "nv12_video_frame.hpp"
#include "raw_video_frame.hpp"

namespace rdc::agent::encoder {

/**
 * @brief 封装 BGRA 到 NV12 的像素格式转换流程。
 */
class BgraToNv12Converter {
public:
    /**
     * @brief 转换相关流程。
     * @param frame 视频帧对象。
     * @return 返回对应结果。
     */
    Nv12VideoFrame Convert(const RawVideoFrame& frame) const;
};

}  // namespace rdc::agent::encoder
