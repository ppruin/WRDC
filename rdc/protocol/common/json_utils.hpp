/**
 * @file json_utils.hpp
 * @brief 声明 protocol/common/json_utils 相关的类型、函数与流程。
 */

#pragma once

#include <string>
#include <string_view>

namespace rdc::protocol::common {

/**
 * @brief 查找字符串。
 * @param object object。
 * @param key key。
 * @return 返回对象指针或句柄。
 */
template <typename Json>
inline const std::string* FindString(const Json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return nullptr;
    }

    return &it->template get_ref<const std::string&>();
}

/**
 * @brief 获取字符串ViewOr。
 * @param object object。
 * @param key key。
 * @param fallback 回退值。
 * @return 返回生成的字符串结果。
 */
template <typename Json>
inline std::string_view GetStringViewOr(const Json& object,
                                        const char* key,
                                        const std::string_view fallback = {}) {
    if (const auto* value = FindString(object, key); value != nullptr) {
        return *value;
    }

    return fallback;
}

/**
 * @brief 查找Mapped。
 * @param items items。
 * @param key key。
 * @return 返回对象指针或句柄。
 */
template <typename Map, typename Key>
inline auto* FindMapped(Map& items, const Key& key) {
    if (auto it = items.find(key); it != items.end()) {
        return &it->second;
    }

    return static_cast<typename Map::mapped_type*>(nullptr);
}

/**
 * @brief 查找Mapped。
 * @param items items。
 * @param key key。
 * @return 返回对象指针或句柄。
 */
template <typename Map, typename Key>
inline const auto* FindMapped(const Map& items, const Key& key) {
    if (auto it = items.find(key); it != items.end()) {
        return &it->second;
    }

    return static_cast<const typename Map::mapped_type*>(nullptr);
}

}  // namespace rdc::protocol::common
