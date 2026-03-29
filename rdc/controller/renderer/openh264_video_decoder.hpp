/**
 * @file openh264_video_decoder.hpp
 * @brief 声明 controller/renderer/openh264_video_decoder 相关的类型、函数与流程。
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include "decoded_video_frame.hpp"
/**
 * @brief 封装 ISVCDecoder 相关的解码流程。
 */
class ISVCDecoder;
namespace rdc::controller::renderer {
/**
 * @brief 封装 OpenH264VideoDecoder 相关的解码流程。
 */
class OpenH264VideoDecoder {
public:
    /**
     * @brief 构造 OpenH264VideoDecoder 对象。
     */
    OpenH264VideoDecoder();
    /**
     * @brief 析构 OpenH264VideoDecoder 对象并释放相关资源。
     */
    ~OpenH264VideoDecoder();
    /**
     * @brief 构造 OpenH264VideoDecoder 对象。
     */
    OpenH264VideoDecoder(const OpenH264VideoDecoder&) = delete;
    OpenH264VideoDecoder& operator=(const OpenH264VideoDecoder&) = delete;
    /**
     * @brief 解码相关流程。
     * @param bytes 输入字节缓冲区。
     * @param size 字节长度。
     * @return 返回可用结果；失败时返回空值。
     */
    std::optional<DecodedVideoFrame> Decode(const std::uint8_t* bytes, std::size_t size);
    /**
     * @brief 解码转换为。
     * @param bytes 输入字节缓冲区。
     * @param size 字节长度。
     * @param sink 用于接收结果的回调对象。
     * @return 返回是否成功或条件是否满足。
     */
    template <typename Sink>
    bool DecodeTo(const std::uint8_t* bytes, std::size_t size, Sink&& sink) {
        std::scoped_lock lock(mutex_);
        if (!DecodeLocked(bytes, size, working_frame_)) {
            return false;
        }
        std::forward<Sink>(sink)(working_frame_);
        return true;
    }
private:
    /**
     * @brief 解码Locked。
     * @param bytes 输入字节缓冲区。
     * @param size 字节长度。
     * @param output_frame outputframe。
     * @return 返回是否成功或条件是否满足。
     */
    bool DecodeLocked(const std::uint8_t* bytes, std::size_t size, DecodedVideoFrame& output_frame);
    ISVCDecoder* decoder_ = nullptr;
    std::mutex mutex_;
    DecodedVideoFrame working_frame_;
};
}  // namespace rdc::controller::renderer
