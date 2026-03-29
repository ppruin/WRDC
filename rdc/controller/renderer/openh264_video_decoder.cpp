/**
 * @file openh264_video_decoder.cpp
 * @brief 实现 controller/renderer/openh264_video_decoder 相关的类型、函数与流程。
 */

#include "openh264_video_decoder.hpp"

#include <wels/codec_api.h>

#include <limits>
#include <stdexcept>
#include <string>

#include "../../protocol/common/video_convert_utils.hpp"

namespace rdc::controller::renderer {

namespace {

[[nodiscard]] std::runtime_error MakeError(const std::string& message) {
    return std::runtime_error("OpenH264 解码器: " + message);
/**
 * @brief 转换Decoded帧。
 * @param frame 视频帧对象。
 * @param planes 平面数据指针数组。
 * @param buffer_info 解码缓冲区描述信息。
 */
}

bool ConvertDecodedFrame(DecodedVideoFrame& frame, unsigned char* const* planes, const SBufferInfo& buffer_info) {
    if (buffer_info.iBufferStatus != 1) {
        return false;
    }

    const auto& sys = buffer_info.UsrData.sSystemBuffer;
    if (sys.iFormat != videoFormatI420) {
        throw MakeError("当前仅支持 I420 解码输出");
    }

    if (planes[0] == nullptr || planes[1] == nullptr || planes[2] == nullptr) {
        throw MakeError("解码输出的 YUV 平面为空");
    }

    frame.ResetStorage<>(static_cast<std::uint32_t>(sys.iWidth), static_cast<std::uint32_t>(sys.iHeight));
    protocol::common::ConvertI420FrameToBgra<>(
        planes[0],
        sys.iStride[0],
        planes[1],
        planes[2],
        sys.iStride[1],
        frame.width,
        frame.height,
        frame.bgra_bytes.data(),
        frame.stride_bytes);

    return true;
}

/**
 * @brief 打开H264视频解码器。
 */
}  // namespace

OpenH264VideoDecoder::OpenH264VideoDecoder() {
    if (WelsCreateDecoder(&decoder_) != 0 || decoder_ == nullptr) {
        throw MakeError("创建解码器实例失败");
    }

    SDecodingParam params{};
    params.sVideoProperty.size = sizeof(params.sVideoProperty);
    params.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    params.uiTargetDqLayer = std::numeric_limits<unsigned char>::max();
    params.eEcActiveIdc = ERROR_CON_FRAME_COPY;
    params.bParseOnly = false;

    if (decoder_->Initialize(&params) != cmResultSuccess) {
        WelsDestroyDecoder(decoder_);
        decoder_ = nullptr;
        throw MakeError("初始化解码器失败");
    }
}

/**
 * @brief 析构 OpenH264VideoDecoder 对象并释放相关资源。
 */
OpenH264VideoDecoder::~OpenH264VideoDecoder() {
    if (decoder_ != nullptr) {
        decoder_->Uninitialize();
        WelsDestroyDecoder(decoder_);
    }
}

/**
 * @brief 解码Locked。
 * @param bytes 输入字节缓冲区。
 * @param size 字节长度。
 * @param output_frame outputframe。
 * @return 返回是否成功或条件是否满足。
 */
bool OpenH264VideoDecoder::DecodeLocked(const std::uint8_t* bytes,
                                        const std::size_t size,
                                        DecodedVideoFrame& output_frame) {
    if (bytes == nullptr || size == 0) {
        return false;
    }

    unsigned char* planes[3]{};
    SBufferInfo buffer_info{};
    const auto result = decoder_->DecodeFrameNoDelay(bytes, static_cast<int>(size), planes, &buffer_info);
    if (result != cmResultSuccess && buffer_info.iBufferStatus != 1) {
        throw MakeError("解码 H.264 样本失败，返回码=" + std::to_string(result));
    }

    if (ConvertDecodedFrame(output_frame, planes, buffer_info)) {
        return true;
    }

    unsigned char* flush_planes[3]{};
    SBufferInfo flush_buffer_info{};
    const auto flush_result = decoder_->DecodeFrame2(nullptr, 0, flush_planes, &flush_buffer_info);
    if (flush_result != cmResultSuccess && flush_buffer_info.iBufferStatus != 1) {
        throw MakeError("刷新解码缓冲失败，返回码=" + std::to_string(flush_result));
    }

    return ConvertDecodedFrame(output_frame, flush_planes, flush_buffer_info);
}

/**
 * @brief 解码相关流程。
 * @param bytes 输入字节缓冲区。
 * @param size 字节长度。
 * @return 返回可用结果；失败时返回空值。
 */
std::optional<DecodedVideoFrame> OpenH264VideoDecoder::Decode(const std::uint8_t* bytes, const std::size_t size) {
    std::scoped_lock lock(mutex_);
    if (!DecodeLocked(bytes, size, working_frame_)) {
        return std::nullopt;
    }

    DecodedVideoFrame frame;
    frame.Swap(working_frame_);
    return frame;
}

}  // namespace rdc::controller::renderer
