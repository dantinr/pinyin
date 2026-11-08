#include "pinyin/local_dictionary_sync.hpp"
#include "pinyin/user_store.hpp"
#include "sync_storage.hpp"
#include <charconv>
#include <memory>
#include <sstream>

namespace pinyin {
std::filesystem::path local_sync_file(const std::filesystem::path& user, const wchar_t* suffix) {
    auto file = user; file += std::wstring(L".local-sync.") + suffix; return file;
}
bool local_sync_enabled(const std::filesystem::path& user) {
    return !user.empty() && sync_storage::read(local_sync_file(user, L"enabled")) == "v1\n";
}
void set_local_sync_enabled(const std::filesystem::path& user, bool enabled) {
    if (user.empty()) throw std::runtime_error("user dictionary path is unavailable");
    const auto path = local_sync_file(user, L"enabled");
    if (enabled) sync_storage::write(path, "v1\n");
    else std::filesystem::remove(path);
}
LocalSyncStatus read_local_sync_status(const std::filesystem::path& user) {
    std::istringstream input(sync_storage::read(local_sync_file(user, L"status")));
    LocalSyncStatus status; std::string version, attempt;
    if (!std::getline(input, version)) return status;
    if (version != "v1" || !std::getline(input, attempt) ||
        !std::getline(input, status.success) || !std::getline(input, status.message))
        throw std::runtime_error("invalid local dictionary sync status");
    const auto parsed = std::from_chars(attempt.data(), attempt.data() + attempt.size(), status.attempt_ms);
    if (parsed.ec != std::errc{} || parsed.ptr != attempt.data() + attempt.size())
        throw std::runtime_error("invalid local dictionary sync timestamp");
    return status;
}
namespace {
void save_status(const std::filesystem::path& user, const LocalSyncStatus& status) {
    sync_storage::write(local_sync_file(user, L"status"), "v1\n" + std::to_string(status.attempt_ms) +
        '\n' + status.success + '\n' + status.message + '\n');
}
std::string timestamp() {
    SYSTEMTIME now{}; GetLocalTime(&now); char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond);
    return text;
}
}
LocalSyncOutcome synchronize_local_dictionary(const std::filesystem::path& base,
    const std::filesystem::path& user, std::uint64_t now_ms) {
    LocalSyncStatus status;
    std::unique_ptr<UserStore> running;
    if (!local_sync_enabled(user)) return LocalSyncOutcome::skipped;
    try { running = std::make_unique<UserStore>(local_sync_file(user, L"running")); }
    catch (const std::exception&) { return LocalSyncOutcome::skipped; }
    try {
        // Also serializes one-shot requests from different versions/processes.
        status = read_local_sync_status(user);
        if (status.attempt_ms && now_ms >= status.attempt_ms && now_ms - status.attempt_ms < local_sync_interval_ms)
            return LocalSyncOutcome::skipped;
        status.attempt_ms = now_ms; status.message = "正在合并个人词库…";
        save_status(user, status);
        const auto merged = merge_user_into_main(base, user, true);
        status.success = timestamp();
        status.message = "已同步 " + std::to_string(merged.imported) + " 条，新增 " +
            std::to_string(merged.added) + " 条，清空个人记录 " + std::to_string(merged.cleared) + " 条";
        save_status(user, status); return LocalSyncOutcome::success;
    } catch (const std::exception& error) {
        // A failed merge keeps personal records, or (if clearing already
        // succeeded) the verified main copy and the personal backup.
        status.attempt_ms = now_ms;
        status.message = std::string("本地同步失败：") + error.what();
        try { save_status(user, status); } catch (...) {}
        return LocalSyncOutcome::failure;
    }
}
void remove_local_sync_startup() {
    HKEY key = nullptr;
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return;
    if (opened != ERROR_SUCCESS) throw std::runtime_error("cannot open local sync startup settings");
    const auto result = RegDeleteValueW(key, L"PrivatePinyinLocalSync"); RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        throw std::runtime_error("cannot remove local sync startup entry");
}
void launch_local_sync_worker(const std::filesystem::path& user, bool enabled) {
    if (!enabled) { remove_local_sync_startup(); return; }
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw std::runtime_error("cannot locate local sync worker");
    path.resize(size);
    const auto worker = std::filesystem::path(path).parent_path() / L"private_pinyin_sync.exe";
    if (!std::filesystem::is_regular_file(worker)) throw std::runtime_error("private_pinyin_sync.exe is missing beside settings");
    const auto command = L"\"" + worker.wstring() + L"\" --local-background --user \"" +
        std::filesystem::absolute(user).wstring() + L"\"";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) throw std::runtime_error("cannot enable local sync at login");
    const auto saved = RegSetValueExW(key, L"PrivatePinyinLocalSync", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (saved != ERROR_SUCCESS) throw std::runtime_error("cannot save local sync startup entry");
    auto mutable_command = command;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(worker.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, worker.parent_path().c_str(), &startup, &process)) throw sync_storage::failure("cannot launch local sync worker");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
}
