/**
 * @file server_config.hpp
 * @brief 声明 server/transport/server_config 相关的类型、函数与流程。
 */

#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace rdc {

/**
 * @brief 定义 ServerLogVerbosity 的枚举取值。
 */
enum class ServerLogVerbosity {
    Simple,
    Verbose
};

/**
 * @brief 描述 ServerConfig 的配置项。
 */
struct ServerConfig {
    std::uint16_t signal_port = 5000;
    std::string bind_host = "0.0.0.0";
    bool save_logs = false;
    std::string log_file_path;
    ServerLogVerbosity log_verbosity = ServerLogVerbosity::Simple;
    bool enable_tls = false;
    std::string tls_cert_path;
    std::string tls_key_path;
    std::string tls_ca_path;
};

/**
 * @brief 加载服务端配置从环境变量。
 * @return 返回对应结果。
 */
inline ServerConfig LoadServerConfigFromEnv() {
    ServerConfig config;

    if (const char* raw_port = std::getenv("RDC_SIGNAL_PORT"); raw_port != nullptr) {
        std::uint16_t parsed_port = 0;
        const auto* begin = raw_port;
        const auto* end = raw_port + std::char_traits<char>::length(raw_port);
        const auto [ptr, ec] = std::from_chars(begin, end, parsed_port);

        if (ec == std::errc{} && ptr == end && parsed_port != 0U) {
            config.signal_port = parsed_port;
        }
    }

    if (const char* raw_bind_host = std::getenv("RDC_SIGNAL_BIND_HOST"); raw_bind_host != nullptr && *raw_bind_host != '\0') {
        config.bind_host = raw_bind_host;
    }

    if (const char* raw_cert = std::getenv("RDC_SIGNAL_CERT"); raw_cert != nullptr && *raw_cert != '\0') {
        config.tls_cert_path = raw_cert;
    }

    if (const char* raw_key = std::getenv("RDC_SIGNAL_KEY"); raw_key != nullptr && *raw_key != '\0') {
        config.tls_key_path = raw_key;
    }

    if (const char* raw_ca = std::getenv("RDC_SIGNAL_CA"); raw_ca != nullptr && *raw_ca != '\0') {
        config.tls_ca_path = raw_ca;
    }

    config.enable_tls = !config.tls_cert_path.empty() && !config.tls_key_path.empty();
    return config;
}

}  // namespace rdc
