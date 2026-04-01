/**
 * @file entry.cpp
 * @brief 实现 entry 相关的类型、函数与流程。
 */

#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

#include "mimalloc.h"
#include "protocol/common/console_logger.hpp"
#include "ui/gui_main.hpp"

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

#ifdef _WIN32
/**
 * @brief 在调试构建中确保当前进程已连接到可见控制台。
 */
void PrepareDebugConsoleForGuiMode() {
#ifdef _DEBUG
    if (GetConsoleWindow() == nullptr) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
            AllocConsole();
        }

        FILE* rebound_stream = nullptr;
        _wfreopen_s(&rebound_stream, L"CONIN$", L"r", stdin);
        _wfreopen_s(&rebound_stream, L"CONOUT$", L"w", stdout);
        _wfreopen_s(&rebound_stream, L"CONOUT$", L"w", stderr);
        std::ios::sync_with_stdio(true);
        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    if (const HWND console_window = GetConsoleWindow(); console_window != nullptr) {
        ShowWindow(console_window, SW_SHOW);
    }
#endif
}
#endif

/**
 * @brief 输出用法信息。
 */
void PrintUsage() {
    std::cout
        << 
        "用法:\n"
        "  rdc gui\n"
        "  rdc server [端口] [-l] [-o 日志文件] [-v|-s]\n"
        "  rdc host <ws://signal-host:port/signal> <device-id>\n"
        "  rdc controller <ws://signal-host:port/signal> <user-id> <target-device-id>\n";
}

/**
 * @brief 执行程序主入口。
 */
}  // namespace

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif

        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);

        rdc::protocol::common::InitializeConsoleLogger();

#ifdef _WIN32
        if (argc < 2) 
        {
#ifdef _DEBUG
            PrepareDebugConsoleForGuiMode();
            rdc::protocol::common::WriteInfoLine("调试模式已启用控制台日志输出");
#else
            if (const HWND console_window = GetConsoleWindow(); console_window != nullptr)
                ShowWindow(console_window, SW_HIDE);
#endif

            return rdc::ui::RunMain();
        }
#else
        if (argc < 2) {
            PrintUsage();
            return 1;
        }
#endif

        const std::string mode = argv[1];

        if (mode == "gui") {
#ifdef _WIN32
            PrepareDebugConsoleForGuiMode();
#endif
            return rdc::ui::RunMain();
        }

        if (mode == "server") {
            return rdc::server::RunMain(argc - 2, argv + 2);
        }

        if (mode == "host") {
            return rdc::agent::RunMain(argc - 2, argv + 2);
        }

        if (mode == "controller") {
            return rdc::controller::RunMain(argc - 2, argv + 2);
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
