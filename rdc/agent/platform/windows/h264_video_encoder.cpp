/**
 * @file h264_video_encoder.cpp
 * @brief 实现可切换的 H.264 编码器包装器。
 */

#include "h264_video_encoder.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "mf_h264_encoder.hpp"
#include "x264_h264_encoder.hpp"
#include "../../../protocol/common/console_logger.hpp"

namespace rdc::agent::platform::windows {

class H264VideoEncoder::Impl {
public:
    virtual ~Impl() = default;
    virtual std::vector<encoder::EncodedVideoFrame> Encode(const encoder::Nv12VideoFrame& frame) = 0;
    virtual std::vector<encoder::EncodedVideoFrame> Drain() = 0;
    virtual void RequestKeyframe() = 0;
};

namespace {

class X264VideoEncoderImpl final : public H264VideoEncoder::Impl {
public:
    explicit X264VideoEncoderImpl(H264EncoderConfig config)
        : encoder_(std::move(config)) {
    }

    std::vector<encoder::EncodedVideoFrame> Encode(const encoder::Nv12VideoFrame& frame) override {
        return encoder_.Encode(frame);
    }

    std::vector<encoder::EncodedVideoFrame> Drain() override {
        return encoder_.Drain();
    }

    void RequestKeyframe() override {
        encoder_.RequestKeyframe();
    }

private:
    X264H264Encoder encoder_;
};

class MfVideoEncoderImpl final : public H264VideoEncoder::Impl {
public:
    explicit MfVideoEncoderImpl(H264EncoderConfig config)
        : encoder_(std::move(config)) {
    }

    std::vector<encoder::EncodedVideoFrame> Encode(const encoder::Nv12VideoFrame& frame) override {
        return encoder_.Encode(frame);
    }

    std::vector<encoder::EncodedVideoFrame> Drain() override {
        return encoder_.Drain();
    }

    void RequestKeyframe() override {
        encoder_.RequestKeyframe();
    }

private:
    MfH264Encoder encoder_;
};

std::unique_ptr<H264VideoEncoder::Impl> CreateEncoderImpl(const H264EncoderConfig& config,
                                                          const H264EncoderBackend backend) {
    switch (backend) {
    case H264EncoderBackend::MediaFoundation:
        return std::make_unique<MfVideoEncoderImpl>(config);
    case H264EncoderBackend::X264:
        return std::make_unique<X264VideoEncoderImpl>(config);
    case H264EncoderBackend::Auto:
    default:
        throw std::runtime_error("invalid explicit H.264 backend");
    }
}

void LogFallbackMessage(const std::string_view stage,
                        const H264EncoderBackend backend,
                        const std::string_view message) {
    protocol::common::WriteInfoLine("H.264 编码器 backend=" + std::string(ToString(backend)) +
                                    " 在 " + std::string(stage) +
                                    " 阶段失败，回退到 x264: " + std::string(message));
}

}  // namespace

H264VideoEncoder::H264VideoEncoder(H264EncoderConfig config)
    : config_(config),
      allow_runtime_fallback_(config.backend == H264EncoderBackend::Auto) {
    if (config_.backend == H264EncoderBackend::Auto) {
        try {
            ResetImpl(H264EncoderBackend::MediaFoundation);
            return;
        } catch (const std::exception& ex) {
            LogFallbackMessage("初始化", H264EncoderBackend::MediaFoundation, ex.what());
        }

        ResetImpl(H264EncoderBackend::X264);
        return;
    }

    ResetImpl(config_.backend);
}

H264VideoEncoder::~H264VideoEncoder() = default;

std::vector<encoder::EncodedVideoFrame> H264VideoEncoder::Encode(const encoder::Nv12VideoFrame& frame) {
    try {
        if (force_next_keyframe_) {
            impl_->RequestKeyframe();
        }

        auto encoded_frames = impl_->Encode(frame);
        force_next_keyframe_ = false;
        return encoded_frames;
    } catch (const std::exception& ex) {
        if (!allow_runtime_fallback_ || active_backend_ != H264EncoderBackend::MediaFoundation) {
            throw;
        }

        LogFallbackMessage("编码", active_backend_, ex.what());
        FallbackToX264();
        if (force_next_keyframe_) {
            impl_->RequestKeyframe();
        }

        auto encoded_frames = impl_->Encode(frame);
        force_next_keyframe_ = false;
        return encoded_frames;
    }
}

std::vector<encoder::EncodedVideoFrame> H264VideoEncoder::Drain() {
    try {
        return impl_->Drain();
    } catch (const std::exception& ex) {
        if (!allow_runtime_fallback_ || active_backend_ != H264EncoderBackend::MediaFoundation) {
            throw;
        }

        LogFallbackMessage("Drain", active_backend_, ex.what());
        FallbackToX264();
        return {};
    }
}

void H264VideoEncoder::RequestKeyframe() {
    force_next_keyframe_ = true;

    try {
        impl_->RequestKeyframe();
    } catch (const std::exception& ex) {
        if (!allow_runtime_fallback_ || active_backend_ != H264EncoderBackend::MediaFoundation) {
            throw;
        }

        LogFallbackMessage("关键帧请求", active_backend_, ex.what());
        FallbackToX264();
        impl_->RequestKeyframe();
    }
}

H264EncoderBackend H264VideoEncoder::ActiveBackend() const {
    return active_backend_;
}

std::string_view H264VideoEncoder::ActiveBackendName() const {
    return ToString(active_backend_);
}

void H264VideoEncoder::ResetImpl(const H264EncoderBackend backend) {
    auto backend_config = config_;
    backend_config.backend = backend;
    impl_ = CreateEncoderImpl(backend_config, backend);
    active_backend_ = backend;
}

void H264VideoEncoder::FallbackToX264() {
    if (active_backend_ == H264EncoderBackend::X264) {
        return;
    }

    if (!allow_runtime_fallback_) {
        throw std::runtime_error("runtime encoder fallback is disabled");
    }

    auto failed_impl = std::move(impl_);
    auto backend_config = config_;
    backend_config.backend = H264EncoderBackend::X264;
    impl_ = CreateEncoderImpl(backend_config, H264EncoderBackend::X264);
    active_backend_ = H264EncoderBackend::X264;

    // 某些 MF 编码器在故障后同步析构会长时间阻塞，这里保留失败实例以保障主链路可继续工作。
    (void)failed_impl.release();
}

}  // namespace rdc::agent::platform::windows
