/**
 * @file control_protocol.hpp
 * @brief 声明 protocol/control/control_protocol 相关的类型、函数与流程。
 */

#pragma once

#include "../../protocol.hpp"

namespace rdc::protocol::control {

/**
 * @brief 构造Ping。
 * @param seq seq。
 * @param ts ts。
 * @return 返回生成的 JSON 对象。
 */
inline Json MakePing(const int seq, const std::int64_t ts) {
    return Json{
        {"type", "ping"},
        {"seq", seq},
        {"ts", ts}
    };
}

/**
 * @brief 构造Pong。
 * @param echo_seq echoseq。
 * @return 返回生成的 JSON 对象。
 */
inline Json MakePong(const int echo_seq) {
    return Json{
        {"type", "pong"},
        {"echoSeq", echo_seq}
    };
}

}  // namespace rdc::protocol::control
