/**
 * @file entry.cpp
 * @brief 实现 entry 相关的类型、函数与流程。
 */

#include <exception>
#include <iostream>
#include <string>

#include "mimalloc.h"
#include "protocol/common/console_logger.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace rdc::agent {
/**
 * @brief 执行当前模块的入口流程并返回退出码。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数列表。
 * @return 返回状态码或退出码。
 */
int RunMain(int argc, char** argv);
}

namespace rdc::controller {
/**
 * @brief 执行当前模块的入口流程并返回退出码。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数列表。
 * @return 返回状态码或退出码。
 */
int RunMain(int argc, char** argv);
}

namespace rdc::server {
/**
 * @brief 执行当前模块的入口流程并返回退出码。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数列表。
 * @return 返回状态码或退出码。
 */
int RunMain(int argc, char** argv);
}

namespace {

/**
 * @brief 输出用法信息。
 */
void PrintUsage() {
    rdc::protocol::common::WriteInfoLine(
        "用法:\n"
        "  rdc server [端口] [-l] [-o 日志文件] [-v|-s]\n"
        "  rdc host <ws://signal-host:port/signal> <device-id>\n"
        "  rdc controller <ws://signal-host:port/signal> <user-id> <target-device-id>");
}

/**
 * @brief 执行程序主入口。
 */
}  // namespace

int main() {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif

        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);

        rdc::protocol::common::WriteInfoLine("mimalloc 版本: " + std::to_string(mi_version()));
        rdc::protocol::common::InitializeConsoleLogger();

        if (__argc < 2) {
            PrintUsage();
            return 1;
        }

        const std::string mode = __argv[1];

        if (mode == "server") {
            return rdc::server::RunMain(__argc - 2, __argv + 2);
        }

        if (mode == "host") {
            return rdc::agent::RunMain(__argc - 2, __argv + 2);
        }

        if (mode == "controller") {
            return rdc::controller::RunMain(__argc - 2, __argv + 2);
        }

        PrintUsage();
        return 1;
    } catch (const std::exception& ex) {
        rdc::protocol::common::WriteErrorLine("致命错误: " + std::string(ex.what()));
    } catch (...) {
        rdc::protocol::common::WriteErrorLine("致命错误: 未知异常");
    }

    return 1;
}
