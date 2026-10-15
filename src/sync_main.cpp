#include "pinyin/dictionary_sync.hpp"
#include "pinyin/user_data.hpp"
#include "sync_storage.hpp"
#include <shellapi.h>
#include <sstream>

namespace {
std::string timestamp() {
    SYSTEMTIME time{}; GetLocalTime(&time); char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return text;
}
int synchronize(const std::filesystem::path& user) {
    pinyin::SyncStatus status;
    try {
        status = pinyin::read_sync_status(user); status.attempt = timestamp();
        const auto settings = pinyin::read_sync_settings(user);
        if (!settings.enabled) return 2;
        status.message = "正在同步…"; pinyin::save_sync_status(user, status);
        const auto result = pinyin::synchronize_dictionary(user, settings);
        status.success = timestamp();
        status.message = "同步成功：上传 " + std::to_string(result.uploaded) + " 条，新增 " +
            std::to_string(result.downloaded.added) + " 条，更新 " + std::to_string(result.downloaded.updated) + " 条";
        pinyin::save_sync_status(user, status); return 0;
    } catch (const std::exception& error) {
        status.message = std::string("同步失败：") + error.what();
        try { pinyin::save_sync_status(user, status); } catch (...) {}
        return 1;
    }
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        int count = 0; auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) return 1;
        struct Release { wchar_t** arguments; ~Release() { LocalFree(arguments); } } release{arguments};
        bool once = false, disable = false;
        std::filesystem::path user;
        for (int index = 1; index < count; ++index) {
            const std::wstring argument(arguments[index]);
            if (argument == L"--once") once = true;
            else if (argument == L"--background") {}
            else if (argument == L"--disable") disable = true;
            else if (argument == L"--user" && index + 1 < count) user = arguments[++index];
            else return 1;
        }
        if (user.empty()) user = pinyin::default_user_path();
        user = std::filesystem::absolute(user);
        if (disable) {
            auto settings = pinyin::read_sync_settings(user); settings.enabled = false;
            pinyin::save_sync_settings(user, settings); pinyin::remove_sync_startup(); return 0;
        }
        if (once) return synchronize(user);
        if (!pinyin::read_sync_settings(user).enabled) return 0;
        pinyin::UserStore running(pinyin::sync_file(user, L"running"));
        ULONGLONG next = 0;
        std::string previous_request;
        auto previous_settings = std::filesystem::file_time_type::min();
        for (;;) {
            const auto settings = pinyin::read_sync_settings(user);
            if (!settings.enabled) return 0;
            const auto request = pinyin::sync_storage::read(pinyin::sync_file(user, L"request"));
            const auto changed = std::filesystem::last_write_time(pinyin::sync_file(user, L"settings"));
            if (GetTickCount64() >= next || request != previous_request || changed != previous_settings) {
                previous_request = request; previous_settings = changed;
                synchronize(user); next = GetTickCount64() + 10 * 60 * 1000;
            }
            Sleep(2000);
        }
    } catch (...) { return 1; }
}
