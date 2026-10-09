#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "pinyin/learning_dictionary.hpp"

namespace pinyin {
inline HRESULT open_user_directory(HWND owner = nullptr) noexcept {
    try {
        const auto directory = default_user_path().parent_path();
        std::filesystem::create_directories(directory);
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", directory.c_str(),
            nullptr, nullptr, SW_SHOWNORMAL));
        return result > 32 ? S_OK : E_FAIL;
    } catch (...) { return E_FAIL; }
}
}
