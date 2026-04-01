/**
 * @file h264_video_encoder.hpp
 * @brief 声明可切换的 H.264 编码器包装器。
 */

#pragma once

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "h264_encoder_types.hpp"
#include "../../encoder/encoded_video_frame.hpp"
#include "../../encoder/nv12_video_frame.hpp"

namespace rdc::agent::platform::windows {

/**
 * @brief 封装可切换的 H.264 编码后端。
 */
class H264VideoEncoder {
public:
    class Impl;

    /**
     * @brief 构造 H264VideoEncoder 对象。
     * @param config 配置对象。
     */
    explicit H264VideoEncoder(H264EncoderConfig config);

    /**
     * @brief 析构 H264VideoEncoder 对象并释放相关资源。
     */
    ~H264VideoEncoder();

    H264VideoEncoder(const H264VideoEncoder&) = delete;
    H264VideoEncoder& operator=(const H264VideoEncoder&) = delete;

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
     * @brief 请求下一帧输出关键帧。
     */
    void RequestKeyframe();

    /**
     * @brief 获取当前实际使用的编码后端。
     * @return 返回编码后端枚举值。
     */
    H264EncoderBackend ActiveBackend() const;

    /**
     * @brief 获取当前实际使用的编码后端名称。
     * @return 返回字符串视图。
     */
    std::string_view ActiveBackendName() const;

    /**
     * @brief 编码Each。
     * @param frame 视频帧对象。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void EncodeEach(const encoder::Nv12VideoFrame& frame, Sink&& sink) {
        auto encoded_frames = Encode(frame);
        for (auto& encoded_frame : encoded_frames) {
            if (!encoded_frame.bytes.empty()) {
                sink(std::move(encoded_frame));
            }
        }
    }

    /**
     * @brief 输出Each。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void DrainEach(Sink&& sink) {
        auto encoded_frames = Drain();
        for (auto& encoded_frame : encoded_frames) {
            if (!encoded_frame.bytes.empty()) {
                sink(std::move(encoded_frame));
            }
        }
    }

private:
    void ResetImpl(H264EncoderBackend backend);
    void FallbackToX264();

    H264EncoderConfig config_{};
    H264EncoderBackend active_backend_ = H264EncoderBackend::Auto;
    bool allow_runtime_fallback_ = false;
    bool force_next_keyframe_ = false;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rdc::agent::platform::windows
