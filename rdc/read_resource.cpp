/**
 * @file read_resource.cpp
 * @brief 实现 Windows 资源读取相关函数。
 */

#include "read_resource.h"

#include <stdexcept>

std::vector<unsigned char> LoadBinaryResource(HINSTANCE hinstance,
                                              const int resource_id,
<<<<<<< HEAD
                                              const wchar_t* const resource_type) 
{
=======
                                              const wchar_t* const resource_type) {
>>>>>>> ec6c746a58750b061c0e595b5410919ddc2500b1
    const HRSRC resource_handle = ::FindResourceW(hinstance,
                                                  MAKEINTRESOURCEW(resource_id),
                                                  resource_type);
    if (resource_handle == nullptr) {
        throw std::runtime_error("Failed to locate binary resource.");
    }

    const DWORD resource_size = ::SizeofResource(hinstance, resource_handle);
    if (resource_size == 0) {
        throw std::runtime_error("Binary resource size is zero.");
    }

    const HGLOBAL loaded_resource = ::LoadResource(hinstance, resource_handle);
    if (loaded_resource == nullptr) {
        throw std::runtime_error("Failed to load binary resource.");
    }

    const void* const locked_resource = ::LockResource(loaded_resource);
    if (locked_resource == nullptr) {
        throw std::runtime_error("Failed to lock binary resource.");
    }

    const auto* const begin = static_cast<const unsigned char*>(locked_resource);
    return std::vector<unsigned char>(begin, begin + resource_size);
}
