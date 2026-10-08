#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msctf.h>
#include <inputscope.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace pinyin::ime {
using Microsoft::WRL::ComPtr;
inline constexpr CLSID service_id{0x4ea569f1,0xaf1c,0x4aa6,{0x8a,0xb8,0xa6,0xcc,0xae,0xc3,0x1e,0x15}};
inline constexpr GUID profile_id{0xf277d354,0x3ec1,0x461e,{0x86,0xad,0xbb,0x56,0x36,0x31,0x17,0x7b}};
inline constexpr GUID attribute_id{0x6328716d,0x9554,0x4943,{0xbf,0x34,0x8e,0x17,0xc0,0x7e,0x4e,0x35}};
inline constexpr GUID candidate_id{0x2b1afa08,0x867d,0x4662,{0x93,0x6c,0x6b,0xbe,0x01,0xa8,0x48,0x79}};
inline constexpr LANGID language_id = 0x0804;
inline constexpr wchar_t description[] = L"隐私拼音（开发版）";
extern HINSTANCE module;
extern std::atomic<long> objects;
extern std::atomic<long> server_locks;

struct ModuleObject {
    ModuleObject() noexcept { ++objects; }
    ~ModuleObject() { --objects; }
};
template<class Function> HRESULT protect(Function&& action) noexcept {
    try { return action(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
inline std::filesystem::path module_path() {
    std::wstring path(32768, L'\0');
    auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("cannot locate IME module");
    path.resize(count); return path;
}
inline std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("invalid UTF-8");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
HRESULT create_service(REFIID iid, void** result);
HRESULT create_attribute_enumerator(IEnumTfDisplayAttributeInfo** result);
HRESULT create_attribute(ITfDisplayAttributeInfo** result);
}
