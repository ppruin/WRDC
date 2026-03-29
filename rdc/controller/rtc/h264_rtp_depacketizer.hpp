/**
 * @file h264_rtp_depacketizer.hpp
 * @brief 声明 controller/rtc/h264_rtp_depacketizer 相关的类型、函数与流程。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../../protocol/common/buffer_utils.hpp"

namespace rdc::controller::rtc {

/**
 * @brief 封装 H.264 RTP 解包与 Annex B NALU 重组流程。
 */
class H264RtpDepacketizer {
public:
    using AnnexBNalu = std::vector<std::uint8_t>;

    /**
     * @brief 推送一个 RTP 负载分片。
     * @param payload 协议负载数据。
     * @param size 字节长度。
     * @return 返回结果集合。
     */
    std::vector<AnnexBNalu> PushPayload(const std::uint8_t* payload, const std::size_t size) {
        std::vector<AnnexBNalu> nalus;
        ForEachAnnexBNalu(payload, size, [&nalus](AnnexBNalu nalu) {
            nalus.push_back(std::move(nalu));
        });
        return nalus;
    }

    /**
     * @brief 遍历当前访问单元中的 Annex B NALU。
     * @param payload 协议负载数据。
     * @param size 字节长度。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void ForEachAnnexBNalu(const std::uint8_t* payload, const std::size_t size, Sink&& sink) {
        if (payload == nullptr || size == 0) {
            return;
        }

        const auto nalu_type = static_cast<std::uint8_t>(payload[0] & 0x1F);
        if (nalu_type >= 1 && nalu_type <= 23) {
            sink(MakeAnnexBNalu(payload, size));
            return;
        }

        if (nalu_type == 24) {
            ParseStapA(payload, size, std::forward<Sink>(sink));
            return;
        }

        if (nalu_type == 28) {
            ParseFuA(payload, size, std::forward<Sink>(sink));
            return;
        }

        fragmented_nalu_.clear();
    }

    /**
     * @brief 重置相关流程。
     */
    void Reset() {
        fragmented_nalu_.clear();
    }

private:
    /**
     * @brief 追加AnnexB启动码。
     * @param bytes 输入字节缓冲区。
     */
    template <typename Container>
    static void AppendAnnexBStartCode(Container& bytes) {
        static constexpr std::uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
        protocol::common::AppendLiteral(bytes, kStartCode);
    }

    /**
     * @brief 构造AnnexBNalu。
     * @param payload 协议负载数据。
     * @param size 字节长度。
     * @return 返回对应结果。
     */
    static AnnexBNalu MakeAnnexBNalu(const std::uint8_t* payload, const std::size_t size) {
        AnnexBNalu bytes;
        protocol::common::ReserveForAppend(bytes, size + 4);
        AppendAnnexBStartCode(bytes);
        protocol::common::AppendBytes(bytes, payload, size);
        return bytes;
    }

    /**
     * @brief 解析StapA。
     * @param payload 协议负载数据。
     * @param size 字节长度。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void ParseStapA(const std::uint8_t* payload, const std::size_t size, Sink&& sink) {
        std::size_t offset = 1;
        while (offset + 2 <= size) {
            const auto nalu_size =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(payload[offset]) << 8) |
                                           static_cast<std::uint16_t>(payload[offset + 1]));
            offset += 2;

            if (nalu_size == 0 || offset + nalu_size > size) {
                fragmented_nalu_.clear();
                return;
            }

            sink(MakeAnnexBNalu(payload + offset, nalu_size));
            offset += nalu_size;
        }
    }

    /**
     * @brief 解析FuA。
     * @param payload 协议负载数据。
     * @param size 字节长度。
     * @param sink 用于接收结果的回调对象。
     */
    template <typename Sink>
    void ParseFuA(const std::uint8_t* payload, const std::size_t size, Sink&& sink) {
        if (size < 3) {
            fragmented_nalu_.clear();
            return;
        }

        const auto indicator = payload[0];
        const auto header = payload[1];
        const bool is_start = (header & 0x80U) != 0;
        const bool is_end = (header & 0x40U) != 0;
        const auto reconstructed_header = static_cast<std::uint8_t>((indicator & 0xE0U) | (header & 0x1FU));
        const auto* fragment_begin = payload + 2;
        const auto fragment_size = size - 2;

        if (is_start) {
            fragmented_nalu_.clear();
            protocol::common::ReserveForAppend(fragmented_nalu_, fragment_size + 5);
            AppendAnnexBStartCode(fragmented_nalu_);
            fragmented_nalu_.push_back(reconstructed_header);
            protocol::common::AppendBytes(fragmented_nalu_, fragment_begin, fragment_size);
        } else {
            if (fragmented_nalu_.empty()) {
                return;
            }

            protocol::common::AppendBytes(fragmented_nalu_, fragment_begin, fragment_size);
        }

        if (is_end && !fragmented_nalu_.empty()) {
            sink(std::move(fragmented_nalu_));
            fragmented_nalu_.clear();
        }
    }

    AnnexBNalu fragmented_nalu_;
};

}  // namespace rdc::controller::rtc
