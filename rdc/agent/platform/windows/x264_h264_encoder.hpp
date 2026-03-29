/**
 * @file x264_h264_encoder.hpp
 * @brief 声明 agent/platform/windows/x264_h264_encoder 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <x264.h>

#include "../../encoder/encoded_video_frame.hpp"
#include "../../encoder/nv12_video_frame.hpp"

/**
 * @brief 描述 x264 编码参数句柄。
 */
struct x264_param_t;
/**
 * @brief 描述 x264 编码器实例句柄。
 */
struct x264_t;

namespace rdc::agent::platform::windows {

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
};

/**
 * @brief 封装 X264H264Encoder 相关的编码流程。
 */
class X264H264Encoder {
public:
    /**
     * @brief 构造 X264H264Encoder 对象。
     * @param config 配置对象。
     */
    explicit X264H264Encoder(H264EncoderConfig config);
    /**
     * @brief 析构 X264H264Encoder 对象并释放相关资源。
     */
    ~X264H264Encoder();

    /**
     * @brief 构造 X264H264Encoder 对象。
     */
    X264H264Encoder(const X264H264Encoder&) = delete;
    X264H264Encoder& operator=(const X264H264Encoder&) = delete;

    /**
     * @brief 编码相关流程。
     * @param frame 视频帧对象。
     * @return 返回结果集合。
     */
    std::vector<encoder::EncodedVideoFrame> Encode(const encoder::Nv12VideoFrame& frame);
    /**
     * @brief 输出相关流程。
     * @return 返回结果集合。
     */
    std::vector<encoder::EncodedVideoFrame> Drain();
    /**
     * @brief 执行 `RequestKeyframe` 对应的处理逻辑。
     */
    void RequestKeyframe();

    /**
     * @brief 编码Each。
     * @param frame 视频帧对象。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void EncodeEach(const encoder::Nv12VideoFrame& frame, Sink&& sink) {
        auto encoded_frame = EncodePicture(&frame);
        if (!encoded_frame.bytes.empty()) {
            sink(std::move(encoded_frame));
        }
    }

    /**
     * @brief 输出Each。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void DrainEach(Sink&& sink) {
        while (encoder_ != nullptr && x264_encoder_delayed_frames(encoder_) > 0) {
            auto encoded_frame = EncodePicture(nullptr);
            if (!encoded_frame.bytes.empty()) {
                sink(std::move(encoded_frame));
            }
        }
    }

private:
    /**
     * @brief 初始化相关流程。
     */
    void Initialize();
    /**
     * @brief 编码Picture。
     * @param frame 视频帧对象。
     * @return 返回对应结果。
     */
    encoder::EncodedVideoFrame EncodePicture(const encoder::Nv12VideoFrame* frame);
    /**
     * @brief 计算帧DurationHns。
     * @param fps_num 帧率分子。
     * @param fps_den 帧率分母。
     * @return 返回计算得到的数值结果。
     */
    static std::int64_t ComputeFrameDurationHns(std::uint32_t fps_num, std::uint32_t fps_den);

    H264EncoderConfig config_;
    x264_param_t* params_ = nullptr;
    x264_t* encoder_ = nullptr;
    std::int64_t frame_duration_hns_ = 0;
    std::int64_t next_pts_ = 0;
    bool force_next_keyframe_ = false;
};

}  // namespace rdc::agent::platform::windows
