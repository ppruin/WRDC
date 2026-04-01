/**
 * @file rtc_handle_utils.hpp
 * @brief 声明 protocol/common/rtc_handle_utils 相关的类型、函数与流程。
 */

#pragma once

#include <rtc/rtc.h>

namespace rdc::protocol::common {

/**
 * @brief 绑定通道类对象的回调。
 * @param handle_id handleid。
 * @param user_pointer userpointer。
 */
template <auto OpenCallback, auto ClosedCallback, auto ErrorCallback, auto MessageCallback>
inline void BindChannelLikeCallbacks(const int handle_id, void* user_pointer) {
    rtcSetUserPointer(handle_id, user_pointer);
    rtcSetOpenCallback(handle_id, OpenCallback);
    rtcSetClosedCallback(handle_id, ClosedCallback);
    rtcSetErrorCallback(handle_id, ErrorCallback);
    rtcSetMessageCallback(handle_id, MessageCallback);
}

/**
 * @brief 解绑通道类对象的回调。
 * @param handle_id handleid。
 */
inline void UnbindChannelLikeCallbacks(const int handle_id) {
    rtcSetOpenCallback(handle_id, nullptr);
    rtcSetClosedCallback(handle_id, nullptr);
    rtcSetErrorCallback(handle_id, nullptr);
    rtcSetMessageCallback(handle_id, nullptr);
    rtcSetUserPointer(handle_id, nullptr);
}

/**
 * @brief 绑定对等连接回调。
 * @param peer_connection_id 对等连接标识。
 * @param user_pointer userpointer。
 */
template <auto LocalDescriptionCallback,
          auto LocalCandidateCallback,
          auto StateChangeCallback,
          auto DataChannelCallback,
          auto TrackCallback>
inline void BindPeerConnectionCallbacks(const int peer_connection_id, void* user_pointer) {
    rtcSetUserPointer(peer_connection_id, user_pointer);
    rtcSetLocalDescriptionCallback(peer_connection_id, LocalDescriptionCallback);
    rtcSetLocalCandidateCallback(peer_connection_id, LocalCandidateCallback);
    rtcSetStateChangeCallback(peer_connection_id, StateChangeCallback);
    rtcSetDataChannelCallback(peer_connection_id, DataChannelCallback);
    rtcSetTrackCallback(peer_connection_id, TrackCallback);
}

/**
 * @brief 解绑对等连接回调。
 * @param peer_connection_id 对等连接标识。
 */
inline void UnbindPeerConnectionCallbacks(const int peer_connection_id) {
    rtcSetLocalDescriptionCallback(peer_connection_id, nullptr);
    rtcSetLocalCandidateCallback(peer_connection_id, nullptr);
    rtcSetStateChangeCallback(peer_connection_id, nullptr);
    rtcSetDataChannelCallback(peer_connection_id, nullptr);
    rtcSetTrackCallback(peer_connection_id, nullptr);
    rtcSetUserPointer(peer_connection_id, nullptr);
}

}  // namespace rdc::protocol::common
