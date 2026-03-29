/**
 * @file protocol.hpp
 * @brief 声明 protocol 相关的类型、函数与流程。
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace rdc::protocol {

using Json = nlohmann::json;

inline constexpr std::string_view kRegisterDevice = "register_device";
inline constexpr std::string_view kRegisterController = "register_controller";
inline constexpr std::string_view kHeartbeat = "heartbeat";
inline constexpr std::string_view kListDevices = "list_devices";
inline constexpr std::string_view kCreateSession = "create_session";
inline constexpr std::string_view kAcceptSession = "accept_session";
inline constexpr std::string_view kRejectSession = "reject_session";
inline constexpr std::string_view kSignal = "signal";
inline constexpr std::string_view kCloseSession = "close_session";

/**
 * @brief 定义 ClientRole 的枚举取值。
 */
enum class ClientRole {
    Host,
    Controller
};

/**
 * @brief 将输入值转换为字符串表示。
 * @param role 角色枚举值。
 * @return 返回生成的字符串结果。
 */
inline std::string ToString(const ClientRole role) {
    switch (role) {
    case ClientRole::Host:
        return "host";
    case ClientRole::Controller:
        return "controller";
    }

    return "unknown";
}

/**
 * @brief 解析角色。
 * @param value 输入值。
 * @return 返回可用结果；失败时返回空值。
 */
inline std::optional<ClientRole> ParseRole(const std::string_view value) {
    if (value == "host") {
        return ClientRole::Host;
    }

    if (value == "controller") {
        return ClientRole::Controller;
    }

    return std::nullopt;
}

/**
 * @brief 构造消息封装。
 * @param type 消息类型。
 * @return 返回生成的 JSON 对象。
 */
inline Json MakeEnvelope(const std::string_view type) {
    return Json{
        {"type", type}
    };
}

/**
 * @brief 构造错误。
 * @param code 错误码。
 * @param message 待处理的消息对象。
 * @return 返回生成的 JSON 对象。
 */
inline Json MakeError(const std::string_view code, const std::string_view message) {
    return Json{
        {"type", "error"},
        {"code", code},
        {"message", message}
    };
}

}  // namespace rdc::protocol
