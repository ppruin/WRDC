/**
 * @file browser_controller_assets.hpp
 * @brief 声明 controller/ui/browser_controller_assets 相关的类型、函数与流程。
 */

#pragma once

#include <string_view>

namespace rdc::controller::ui {

/**
 * @brief 获取浏览器控制端HTML 页面。
 * @return 返回生成的字符串结果。
 */
std::string_view GetBrowserControllerHtml();
/**
 * @brief 获取浏览器控制端脚本内容。
 * @return 返回生成的字符串结果。
 */
std::string_view GetBrowserControllerScript();

}  // namespace rdc::controller::ui
