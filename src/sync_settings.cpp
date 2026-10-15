#include "pinyin/dictionary_sync.hpp"
#include "sync_storage.hpp"
#include <wincrypt.h>
#include <sstream>

namespace pinyin {
namespace {
std::string protect(const std::string& text, bool encrypt) {
    if (text.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))}, output{};
    const auto ok = encrypt ? CryptProtectData(&input, L"Private Pinyin sync token", nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &output) : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) throw sync_storage::failure("cannot protect or read sync token for this Windows user");
    std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData); return result;
}
std::string hex(const std::string& bytes) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (const unsigned char value : bytes) { result += digits[value >> 4]; result += digits[value & 15]; }
    return result;
}
std::string unhex(const std::string& text) {
    if (text.size() % 2) throw std::runtime_error("invalid encrypted sync token");
    std::string result;
    const std::string digits = "0123456789abcdef";
    for (std::size_t offset = 0; offset < text.size(); offset += 2) {
        const auto high = digits.find(text[offset]), low = digits.find(text[offset + 1]);
        if (high == std::string::npos || low == std::string::npos) throw std::runtime_error("invalid encrypted sync token");
        result += static_cast<char>(high * 16 + low);
    }
    return result;
}
std::filesystem::path worker_path() {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw sync_storage::failure("cannot locate sync worker");
    path.resize(size); return std::filesystem::path(path).parent_path() / L"private_pinyin_sync.exe";
}
}
std::filesystem::path sync_file(const std::filesystem::path& user, const wchar_t* suffix) {
    auto path = user; path += std::wstring(L".sync.") + suffix; return path;
}
SyncSettings read_sync_settings(const std::filesystem::path& user) {
    const auto path = sync_file(user, L"settings");
    const auto bytes = sync_storage::read(path);
    if (bytes.empty() && !std::filesystem::exists(path)) return {};
    std::istringstream stream(bytes);
    std::string header, enabled, url, token, extra;
    if (!std::getline(stream, header) || header != "private-pinyin-sync-settings-v1" ||
        !std::getline(stream, enabled) || (enabled != "0" && enabled != "1") ||
        !std::getline(stream, url) || !std::getline(stream, token) || std::getline(stream, extra))
        throw std::runtime_error("invalid sync settings; save settings again");
    SyncSettings settings{enabled == "1", url, protect(unhex(token), false)};
    validate_sync_settings(settings); return settings;
}
void save_sync_settings(const std::filesystem::path& user, const SyncSettings& settings) {
    validate_sync_settings(settings);
    const auto path = sync_file(user, L"settings");
    UserStore lock(path, 1000);
    sync_storage::write(path, "private-pinyin-sync-settings-v1\n" + std::string(settings.enabled ? "1\n" : "0\n") +
        settings.url + '\n' + hex(protect(settings.token, true)) + '\n');
}
SyncStatus read_sync_status(const std::filesystem::path& user) {
    std::istringstream stream(sync_storage::read(sync_file(user, L"status")));
    SyncStatus status;
    std::getline(stream, status.attempt); std::getline(stream, status.success); std::getline(stream, status.message);
    return status;
}
void save_sync_status(const std::filesystem::path& user, const SyncStatus& status) {
    sync_storage::write(sync_file(user, L"status"), status.attempt + '\n' + status.success + '\n' + status.message + '\n');
}
void remove_sync_startup() {
    HKEY key = nullptr;
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return;
    if (opened != ERROR_SUCCESS) throw std::runtime_error("cannot open sync startup settings");
    const auto result = RegDeleteValueW(key, L"PrivatePinyinSync"); RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) throw std::runtime_error("cannot remove sync startup entry");
}
void launch_sync_worker(const std::filesystem::path& user, bool enabled) {
    if (!enabled) { remove_sync_startup(); return; }
    const auto worker = worker_path();
    if (!std::filesystem::is_regular_file(worker)) throw std::runtime_error("private_pinyin_sync.exe is missing beside the settings program");
    const auto command = L"\"" + worker.wstring() + L"\" --background --user \"" + std::filesystem::absolute(user).wstring() + L"\"";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) throw std::runtime_error("cannot enable sync at Windows login");
    const auto saved = RegSetValueExW(key, L"PrivatePinyinSync", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (saved != ERROR_SUCCESS) throw std::runtime_error("cannot save sync startup entry");
    sync_storage::write(sync_file(user, L"request"), std::to_string(GetTickCount64()) + "\n");
    auto mutable_command = command;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(worker.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, worker.parent_path().c_str(), &startup, &process)) throw sync_storage::failure("cannot start sync worker");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
}
