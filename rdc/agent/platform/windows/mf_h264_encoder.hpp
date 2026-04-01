/**
 * @file mf_h264_encoder.hpp
 * @brief 声明 agent/platform/windows/mf_h264_encoder 相关的类型、函数与流程。
 */

#pragma once

#include <cstdint>
#include <vector>

#include <wrl/client.h>

/**
 * @brief 描述 Media Foundation 编码器组件句柄。
 */
struct IMFTransform;
struct ICodecAPI;

#include "h264_encoder_types.hpp"
#include "../../encoder/encoded_video_frame.hpp"
#include "../../encoder/nv12_video_frame.hpp"

namespace rdc::agent::platform::windows {

/**
 * @brief 封装 MfH264Encoder 相关的编码流程。
 */
class MfH264Encoder {
public:
    /**
     * @brief 构造 MfH264Encoder 对象。
     * @param config 配置对象。
     */
    explicit MfH264Encoder(H264EncoderConfig config);
    /**
     * @brief 析构 MfH264Encoder 对象并释放相关资源。
     */
    ~MfH264Encoder();

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

private:
    /**
     * @brief 初始化相关流程。
     */
    void Initialize();
    /**
     * @brief 配置CodecApi。
     */
    void ConfigureCodecApi();
    /**
     * @brief 设置MediaTypes。
     */
    void SetMediaTypes();
    /**
     * @brief 启动Streaming。
     */
    void StartStreaming();
    /**
     * @brief 创建编码器Transform。
     * @return 返回对象指针或句柄。
     */
    Microsoft::WRL::ComPtr<IMFTransform> CreateEncoderTransform() const;
    /**
     * @brief 执行 PullAvailable输出 相关处理。
     * @return 返回结果集合。
     */
    std::vector<encoder::EncodedVideoFrame> PullAvailableOutput();
    /**
     * @brief 计算帧DurationHns。
     * @param fps_num 帧率分子。
     * @param fps_den 帧率分母。
     * @return 返回计算得到的数值结果。
     */
    static std::int64_t ComputeFrameDurationHns(std::uint32_t fps_num, std::uint32_t fps_den);

    H264EncoderConfig config_;
    Microsoft::WRL::ComPtr<IMFTransform> transform_;
    Microsoft::WRL::ComPtr<ICodecAPI> codec_api_;
    std::int64_t next_sample_time_hns_ = 0;
    std::int64_t frame_duration_hns_ = 0;
    bool stream_started_ = false;
    bool com_initialized_ = false;
    bool mf_started_ = false;
};

}  // namespace rdc::agent::platform::windows
