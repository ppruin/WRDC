/**
 * @file buffer_utils.hpp
 * @brief 声明 protocol/common/buffer_utils 相关的类型、函数与流程。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace rdc::protocol::common {

/**
 * @brief 为For追加预留容量。
 * @param bytes 输入字节缓冲区。
 * @param extra_bytes 需要额外追加的字节数。
 */
template <typename Container>
inline void ReserveForAppend(Container& bytes, const std::size_t extra_bytes) {
    const auto required = bytes.size() + extra_bytes;
    if (required > bytes.capacity()) {
        bytes.reserve(required);
    }
}

/**
 * @brief 追加Range。
 * @param bytes 输入字节缓冲区。
 * @param first 起始迭代器。
 * @param last 结束迭代器。
 */
template <typename Container, typename Iterator>
inline void AppendRange(Container& bytes, const Iterator first, const Iterator last) {
    bytes.insert(bytes.end(), first, last);
}

/**
 * @brief 追加字节。
 * @param bytes 输入字节缓冲区。
 * @param data 输入数据或缓冲区指针。
 * @param size 字节长度。
 */
template <typename Container>
inline void AppendBytes(Container& bytes, const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    ReserveForAppend(bytes, size);
    AppendRange(bytes, data, data + size);
}

/**
 * @brief 追加Literal。
 * @param bytes 输入字节缓冲区。
 */
template <typename Container, std::size_t N>
inline void AppendLiteral(Container& bytes, const std::uint8_t (&literal)[N]) {
    ReserveForAppend(bytes, N);
    AppendRange(bytes, std::begin(literal), std::end(literal));
}

/**
 * @brief 清理And预留。
 * @param bytes 输入字节缓冲区。
 * @param capacity 目标容量。
 */
template <typename Container>
inline void ClearAndReserve(Container& bytes, const std::size_t capacity) {
    bytes.clear();
    if (bytes.capacity() < capacity) {
        bytes.reserve(capacity);
    }
}

/**
 * @brief 调整IfSmaller大小。
 * @param items items。
 * @param size 字节长度。
 */
template <typename Container>
inline void ResizeIfSmaller(Container& items, const std::size_t size) {
    if (items.size() < size) {
        items.resize(size);
    }
}

/**
 * @brief 复制Rows。
 * @param source source。
 * @param source_stride_bytes sourcestridebytes。
 * @param destination destination。
 * @param destination_stride_bytes destinationstridebytes。
 * @param width width。
 * @param height height。
 */
template <std::size_t BytesPerPixel, typename SourceByte, typename DestinationByte>
inline void CopyRows(const SourceByte* source,
                     const std::size_t source_stride_bytes,
                     DestinationByte* destination,
                     const std::size_t destination_stride_bytes,
                     const std::uint32_t width,
                     const std::uint32_t height) {
    const auto row_bytes = static_cast<std::size_t>(width) * BytesPerPixel;
    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * destination_stride_bytes,
                    source + static_cast<std::size_t>(row) * source_stride_bytes,
                    row_bytes);
    }
}

/**
 * @brief 重置All。
 * @param objects objects。
 */
template <typename... Objects>
inline void ResetAll(Objects&... objects) {
    (objects.reset(), ...);
}

/**
 * @brief 执行 ForEachValue 相关处理。
 * @param range range。
 * @param func 需要执行的回调对象。
 */
template <typename Range, typename Func>
inline void ForEachValue(Range&& range, Func&& func) {
    for (auto&& value : range) {
        func(std::forward<decltype(value)>(value));
    }
}

}  // namespace rdc::protocol::common
