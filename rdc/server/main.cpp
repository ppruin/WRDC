/**
 * @file main.cpp
 * @brief 实现 server/main 相关的类型、函数与流程。
 */

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "signaling_gateway/signaling_server.hpp"
#include "transport/server_config.hpp"

namespace rdc::server {

namespace {

/**
 * @brief 输出服务端用法信息。
 */
void PrintServerUsage() {
    std::cout
        << "服务端用法:\n"
        << "  rdc server [端口]\n"
        << "  rdc server [端口] -l [-o 日志文件] [-v|-s]\n"
        << "  rdc server -p 端口 [-l] [-o 日志文件] [-v|-s]\n"
        << "  rdc server -p 端口 -c 证书.pem -k 私钥.pem [-a CA.pem]\n"
        << "\n"
        << "参数说明:\n"
        << "  [端口]      可省略，默认使用 5000\n"
        << "  -p <端口>   显式指定监听端口\n"
        << "  -l          将日志保存到文件\n"
        << "  -o <路径>   指定日志文件路径，会自动启用日志保存\n"
        << "  -v          详细日志\n"
        << "  -s          简略日志\n"
        << "  -c <路径>   HTTPS 证书文件路径 (PEM)\n"
        << "  -k <路径>   HTTPS 私钥文件路径 (PEM)\n"
        << "  -a <路径>   可选的 CA 证书文件路径\n"
        << "  -h          显示帮助\n";
}

/**
 * @brief 执行 Try解析Port 相关处理。
 * @param raw 原始字符串值。
 * @param port port。
 * @return 返回是否成功或条件是否满足。
 */
bool TryParsePort(const std::string_view raw, std::uint16_t& port) {
    if (raw.empty()) {
        return false;
    }

    const auto* begin = raw.data();
    const auto* end = raw.data() + raw.size();
    auto parsed = std::uint16_t{};
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end || parsed == 0U) {
        return false;
    }

    port = parsed;
    return true;
}

/**
 * @brief 执行 ValidateTls配置 相关处理。
 * @param config 配置对象。
 * @return 返回是否成功或条件是否满足。
 */
bool ValidateTlsConfig(const ServerConfig& config) {
    return config.tls_cert_path.empty() == config.tls_key_path.empty();
}

/**
 * @brief 定位Existing文件路径。
 * @param path path。
 * @param label label。
 * @return 返回是否成功或条件是否满足。
 */
bool ResolveExistingFilePath(std::string& path, const std::string_view label) {
    if (path.empty()) {
        return true;
    }

    std::error_code ec;
    auto file_path = std::filesystem::path(path);
    if (file_path.is_relative()) {
        file_path = std::filesystem::absolute(file_path, ec);
        if (ec) {
            std::cerr << "错误: 无法解析" << label << "路径: " << path << '\n';
            return false;
        }
    }

    if (!std::filesystem::exists(file_path, ec) || ec) {
        std::cerr << "错误: " << label << "文件不存在: " << file_path.string() << '\n';
        return false;
    }

    if (!std::filesystem::is_regular_file(file_path, ec) || ec) {
        std::cerr << "错误: " << label << "路径不是有效文件: " << file_path.string() << '\n';
        return false;
    }

    path = file_path.string();
    return true;
}

/**
 * @brief 执行当前模块的入口流程并返回退出码。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数列表。
 */
}  // namespace

int RunMain(int argc, char** argv) {
    auto config = LoadServerConfigFromEnv();
    bool positional_port_set = false;

    for (int index = 0; index < argc; ++index) {
        const std::string_view arg = argv[index];

        if (arg == "-h" || arg == "--help") {
            PrintServerUsage();
            return 0;
        }

        if (arg == "-l") {
            config.save_logs = true;
            continue;
        }

        if (arg == "-o") {
            if (index + 1 >= argc) {
                std::cerr << "错误: -o 需要提供日志文件路径\n";
                PrintServerUsage();
                return 1;
            }

            config.save_logs = true;
            config.log_file_path = argv[++index];
            continue;
        }

        if (arg == "-v") {
            config.log_verbosity = ServerLogVerbosity::Verbose;
            continue;
        }

        if (arg == "-s") {
            config.log_verbosity = ServerLogVerbosity::Simple;
            continue;
        }

        if (arg == "-c") {
            if (index + 1 >= argc) {
                std::cerr << "错误: -c 需要提供证书文件路径\n";
                PrintServerUsage();
                return 1;
            }

            config.tls_cert_path = argv[++index];
            continue;
        }

        if (arg == "-k") {
            if (index + 1 >= argc) {
                std::cerr << "错误: -k 需要提供私钥文件路径\n";
                PrintServerUsage();
                return 1;
            }

            config.tls_key_path = argv[++index];
            continue;
        }

        if (arg == "-a") {
            if (index + 1 >= argc) {
                std::cerr << "错误: -a 需要提供 CA 证书文件路径\n";
                PrintServerUsage();
                return 1;
            }

            config.tls_ca_path = argv[++index];
            continue;
        }

        if (arg == "-p") {
            if (index + 1 >= argc) {
                std::cerr << "错误: -p 需要提供端口号\n";
                PrintServerUsage();
                return 1;
            }

            std::uint16_t parsed_port = 0;
            if (!TryParsePort(argv[++index], parsed_port)) {
                std::cerr << "错误: 端口号无效\n";
                PrintServerUsage();
                return 1;
            }

            config.signal_port = parsed_port;
            positional_port_set = true;
            continue;
        }

        std::uint16_t parsed_port = 0;
        if (!positional_port_set && TryParsePort(arg, parsed_port)) {
            config.signal_port = parsed_port;
            positional_port_set = true;
            continue;
        }

        std::cerr << "错误: 不支持的参数 " << std::string(arg) << '\n';
        PrintServerUsage();
        return 1;
    }

    if (!ValidateTlsConfig(config)) {
        std::cerr << "错误: HTTPS 模式需要同时提供证书文件 (-c) 和私钥文件 (-k)\n";
        PrintServerUsage();
        return 1;
    }

    if (!ResolveExistingFilePath(config.tls_cert_path, "TLS 证书")) {
        return 1;
    }

    if (!ResolveExistingFilePath(config.tls_key_path, "TLS 私钥")) {
        return 1;
    }

    if (!ResolveExistingFilePath(config.tls_ca_path, "TLS CA 证书")) {
        return 1;
    }

    config.enable_tls = !config.tls_cert_path.empty() && !config.tls_key_path.empty();
    SignalingServer server(config);
    return server.Run();
}

}  // namespace rdc::server
