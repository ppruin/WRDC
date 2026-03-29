/**
 * @file x264_h264_encoder.cpp
 * @brief 实现 agent/platform/windows/x264_h264_encoder 相关的类型、函数与流程。
 */

#include "x264_h264_encoder.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "../../../protocol/common/buffer_utils.hpp"

namespace rdc::agent::platform::windows {

namespace {

[[nodiscard]] std::runtime_error MakeError(const std::string& message) {
    return std::runtime_error("x264 编码器: " + message);
/**
 * @brief 计算ExpectedNv12字节。
 * @param frame 视频帧对象。
 */
}

std::size_t ComputeExpectedNv12Bytes(const encoder::Nv12VideoFrame& frame) {
    const auto luma_bytes = static_cast<std::size_t>(frame.luma_stride_bytes) * frame.height;
    const auto chroma_bytes = static_cast<std::size_t>(frame.chroma_stride_bytes) * (frame.height / 2);
    return luma_bytes + chroma_bytes;
}

/**
 * @brief 构造 X264H264Encoder 对象。
 */
}  // namespace

X264H264Encoder::X264H264Encoder(H264EncoderConfig config)
    : config_(config),
      frame_duration_hns_(ComputeFrameDurationHns(config.fps_num, config.fps_den)) {
    Initialize();
}

/**
 * @brief 析构 X264H264Encoder 对象并释放相关资源。
 */
X264H264Encoder::~X264H264Encoder() {
    if (encoder_ != nullptr) {
        x264_encoder_close(encoder_);
    }

    delete params_;
}

/**
 * @brief 编码相关流程。
 * @param frame 视频帧对象。
 * @return 返回结果集合。
 */
std::vector<encoder::EncodedVideoFrame> X264H264Encoder::Encode(const encoder::Nv12VideoFrame& frame) {
    std::vector<encoder::EncodedVideoFrame> frames;
    EncodeEach(frame, [&frames](encoder::EncodedVideoFrame encoded_frame) {
        frames.push_back(std::move(encoded_frame));
    });
    return frames;
}

/**
 * @brief 输出相关流程。
 * @return 返回结果集合。
 */
std::vector<encoder::EncodedVideoFrame> X264H264Encoder::Drain() {
    std::vector<encoder::EncodedVideoFrame> frames;
    DrainEach([&frames](encoder::EncodedVideoFrame encoded_frame) {
        frames.push_back(std::move(encoded_frame));
    });
    return frames;
}

/**
 * @brief 执行 `RequestKeyframe` 对应的处理逻辑。
 */
void X264H264Encoder::RequestKeyframe() {
    force_next_keyframe_ = true;
}

/**
 * @brief 初始化相关流程。
 */
void X264H264Encoder::Initialize() {
    if (config_.width == 0 || config_.height == 0) {
        throw MakeError("帧尺寸无效");
    }

    if ((config_.width & 1U) != 0 || (config_.height & 1U) != 0) {
        throw MakeError("NV12 输入宽高必须为偶数");
    }

    params_ = new x264_param_t{};
    if (x264_param_default_preset(params_, "veryfast", "zerolatency") < 0) {
        throw MakeError("x264_param_default_preset 调用失败");
    }

    params_->i_csp = X264_CSP_NV12;
    params_->i_width = static_cast<int>(config_.width);
    params_->i_height = static_cast<int>(config_.height);
    params_->i_fps_num = static_cast<int>(config_.fps_num);
    params_->i_fps_den = static_cast<int>(config_.fps_den);
    params_->i_timebase_num = static_cast<int>(config_.fps_den);
    params_->i_timebase_den = static_cast<int>(config_.fps_num);
    params_->i_keyint_max = static_cast<int>(config_.gop_size);
    params_->i_keyint_min = static_cast<int>(config_.gop_size > 1 ? config_.gop_size / 2 : 1);
    params_->i_threads = 1;
    params_->i_log_level = X264_LOG_WARNING;
    params_->i_bframe = 0;
    params_->b_vfr_input = 0;
    params_->b_repeat_headers = 1;
    params_->b_annexb = 1;
    params_->rc.i_rc_method = X264_RC_ABR;
    params_->rc.i_bitrate = static_cast<int>(config_.bitrate / 1000);
    params_->rc.i_vbv_buffer_size = params_->rc.i_bitrate;
    params_->rc.i_vbv_max_bitrate = params_->rc.i_bitrate;

    if (x264_param_apply_profile(params_, "baseline") < 0) {
        throw MakeError("x264_param_apply_profile 调用失败");
    }

    encoder_ = x264_encoder_open(params_);
    if (encoder_ == nullptr) {
        throw MakeError("x264_encoder_open 调用失败");
    }
}

/**
 * @brief 编码Picture。
 * @param frame 视频帧对象。
 * @return 返回对应结果。
 */
encoder::EncodedVideoFrame X264H264Encoder::EncodePicture(const encoder::Nv12VideoFrame* frame) {
    x264_picture_t pic_in{};
    x264_picture_t pic_out{};
    x264_picture_init(&pic_out);

    x264_picture_t* pic_in_ptr = nullptr;
    std::int64_t sample_time_hns = next_pts_ * frame_duration_hns_;
    next_pts_ += 1;

    if (frame != nullptr) {
        if (frame->width != config_.width || frame->height != config_.height) {
            throw MakeError("NV12 帧尺寸与编码器配置不一致");
        }

        const auto expected_bytes = ComputeExpectedNv12Bytes(*frame);
        if (frame->bytes.size() < expected_bytes) {
            throw MakeError("NV12 帧缓冲区大小小于预期");
        }

        x264_picture_init(&pic_in);
        pic_in.img.i_csp = X264_CSP_NV12;
        pic_in.img.i_plane = 2;
        pic_in.img.i_stride[0] = static_cast<int>(frame->luma_stride_bytes);
        pic_in.img.i_stride[1] = static_cast<int>(frame->chroma_stride_bytes);
        pic_in.img.plane[0] = const_cast<std::uint8_t*>(frame->bytes.data());
        pic_in.img.plane[1] =
            const_cast<std::uint8_t*>(frame->bytes.data() +
                                      static_cast<std::size_t>(frame->luma_stride_bytes) * frame->height);
        pic_in.i_pts = next_pts_ - 1;
        pic_in.i_type = force_next_keyframe_ ? X264_TYPE_IDR : X264_TYPE_AUTO;
        pic_in_ptr = &pic_in;
        force_next_keyframe_ = false;
    }

    x264_nal_t* nals = nullptr;
    int nal_count = 0;
    const int bytes_produced = x264_encoder_encode(encoder_, &nals, &nal_count, pic_in_ptr, &pic_out);
    if (bytes_produced < 0) {
        throw MakeError("x264_encoder_encode 调用失败");
    }

    encoder::EncodedVideoFrame encoded_frame;
    encoded_frame.sample_time_hns = sample_time_hns;
    encoded_frame.sample_duration_hns = frame_duration_hns_;
    encoded_frame.is_key_frame = pic_out.b_keyframe != 0;

    if (bytes_produced == 0 || nals == nullptr || nal_count <= 0) {
        return encoded_frame;
    }

    protocol::common::ReserveForAppend(encoded_frame.bytes, static_cast<std::size_t>(bytes_produced));
    for (int i = 0; i < nal_count; ++i) {
        protocol::common::AppendBytes(encoded_frame.bytes,
                                      nals[i].p_payload,
                                      static_cast<std::size_t>(nals[i].i_payload));
    }

    return encoded_frame;
}

/**
 * @brief 计算帧DurationHns。
 * @param fps_num 帧率分子。
 * @param fps_den 帧率分母。
 * @return 返回计算得到的数值结果。
 */
std::int64_t X264H264Encoder::ComputeFrameDurationHns(const std::uint32_t fps_num, const std::uint32_t fps_den) {
    if (fps_num == 0 || fps_den == 0) {
        throw MakeError("帧率无效");
    }

    return (10'000'000LL * fps_den) / fps_num;
}

}  // namespace rdc::agent::platform::windows
