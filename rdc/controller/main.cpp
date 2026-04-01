/**
 * @file main.cpp
 * @brief 实现 controller/main 相关的类型、函数与流程。
 */

#include "rtc/controller_client.hpp"

namespace rdc::controller {

/**
 * @brief 执行当前模块的入口流程并返回退出码。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数列表。
 * @return 返回状态码或退出码。
 */
int RunMain(int argc, char** argv) {
    if (argc < 3) {
        return 1;
    }

    ControllerClient controller(argv[0], argv[1], argv[2]);
    return controller.Run();
}

}  // namespace rdc::controller
