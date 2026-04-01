/**
 * @file read_resource.h
 * @brief 声明 Windows 资源读取相关函数。
 */

#pragma once

#include <vector>
#include <windows.h>

/**
 * @brief 从指定模块的资源段读取二进制数据。
 * @param hinstance 模块实例句柄。
 * @param resource_id 资源编号。
 * @param resource_type 资源类型。
 * @return 返回资源对应的二进制字节流。
 * @throws std::runtime_error 当资源查找或加载失败时抛出异常。
 */
std::vector<unsigned char> LoadBinaryResource(HINSTANCE hinstance,
                                              int resource_id,
                                              const wchar_t* resource_type);
