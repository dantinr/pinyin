#include "pinyin/user_data.hpp"
#include "pinyin/dictionary_manager.hpp"
#include "pinyin/user_store.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace pinyin {
namespace {
std::filesystem::path environment(const wchar_t* name) {
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    const auto copied = GetEnvironmentVariableW(name, value.data(), size);
    if (!copied || copied >= size) throw std::runtime_error("cannot read user data environment");
    value.resize(copied); return value;
}
std::string utf8(const std::wstring& value) {
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("cannot encode migration source");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    // Called under the migration lock; publish only complete receipts.
    auto temporary = path; temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!(output << bytes) || !(output.flush())) throw std::runtime_error("cannot save migration receipt");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot publish migration receipt");
}
std::string physical_identity(const std::filesystem::path& source) {
    const auto file = CreateFileW(source.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot identify old dictionary");
    const auto size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED);
    std::wstring path(size, L'\0');
    const auto copied = size ? GetFinalPathNameByHandleW(file, path.data(), size, FILE_NAME_NORMALIZED) : 0;
    CloseHandle(file);
    if (!copied || copied >= size) throw std::runtime_error("cannot resolve old dictionary");
    path.resize(copied);
    // A logical path may open a package-private file; receipts must identify
    // the opened file, otherwise the shared original would never be imported.
    CharLowerBuffW(path.data(), static_cast<DWORD>(path.size()));
    return utf8(path);
}
std::string filename(const std::string& identity) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : identity) { hash ^= ch; hash *= 1099511628211ull; }
    std::ostringstream value; value << std::hex << std::setw(16) << std::setfill('0') << hash;
    return value.str();
}
constexpr const wchar_t* setting_names[] = {
    L"words.user.tsv.ime-learning.disabled", L"words.user.tsv.punctuation.disabled",
    L"words.user.tsv.automatic-english.disabled"
};
bool has_data(const std::filesystem::path& root) {
    std::error_code error;
    if (std::filesystem::is_regular_file(root / L"words.user.tsv", error) ||
        std::filesystem::is_directory(root / L"dictionaries", error)) return true;
    for (auto name : setting_names) if (std::filesystem::is_regular_file(root / name, error)) return true;
    return false;
}
std::vector<std::filesystem::path> old_roots(const std::filesystem::path& appdata) {
    std::vector<std::filesystem::path> roots;
    const auto shared = appdata / L"PrivatePinyin";
    if (has_data(shared)) roots.push_back(shared);
    const auto packages = appdata / L"Packages";
    if (std::filesystem::is_directory(packages)) {
        std::vector<std::filesystem::path> copies;
        for (const auto& item : std::filesystem::directory_iterator(packages,
            std::filesystem::directory_options::skip_permission_denied)) {
            const auto root = item.path() / L"LocalCache" / L"Local" / L"PrivatePinyin";
            if (has_data(root)) copies.push_back(root);
        }
        std::sort(copies.begin(), copies.end());
        roots.insert(roots.end(), copies.begin(), copies.end());
    }
    return roots;
}
}
void migrate_legacy_user_data(const std::filesystem::path& user, const std::filesystem::path& appdata) {
    if (appdata.empty()) return;
    const auto roots = old_roots(appdata);
    if (roots.empty()) return; // Fresh lookup must not create personal storage.
    const auto destination = user.parent_path();
    const auto state = destination / L"migration";
    UserStore lock(state / L"state", 1000);
    const bool had_shared_data = has_data(destination);
    const auto settings_receipt = state / L"settings.done";
    if (read(settings_receipt) != "v1\n") {
        // The current host's legacy settings win on the initial move. Once a
        // shared store exists, migration must not undo the user's newer choices.
        if (!had_shared_data) for (auto name : setting_names) {
            const auto source = roots.front() / name;
            if (std::filesystem::is_regular_file(source) && !CopyFileW(source.c_str(), (destination / name).c_str(), TRUE) &&
                GetLastError() != ERROR_FILE_EXISTS) throw std::runtime_error("cannot migrate IME setting");
        }
        write(settings_receipt, "v1\n");
    }
    std::string errors;
    auto import = [&](const std::filesystem::path& source, const std::string& name) {
        if (!std::filesystem::is_regular_file(source)) return;
        try {
            const auto identity = physical_identity(source);
            const auto key = filename(identity);
            const auto receipt = state / (key + ".done");
            const auto expected = "v1\n" + identity + '\n';
            const auto previous = read(receipt);
            if (previous == expected) return;
            if (!previous.empty()) throw std::runtime_error("migration receipt is damaged or conflicts with another source");
            const auto snapshot = state / (key + ".tsv");
            if (!CopyFileW(source.c_str(), snapshot.c_str(), FALSE)) throw std::runtime_error("cannot preserve old dictionary");
            if (name.empty()) merge_user_dictionary(user, snapshot);
            else {
                DictionaryManager manager(destination / L"dictionaries");
                const auto target = manager.root() / (name + ".tsv");
                const bool existed = std::filesystem::exists(target);
                manager.import_file(name, snapshot);
                auto disabled = source; disabled += L".disabled";
                if (!existed && std::filesystem::is_regular_file(disabled)) manager.set_enabled(name, false);
            }
            write(receipt, expected);
        } catch (const std::exception& error) {
            // Keep valid sources available when another old file needs repair.
            // No receipt is recorded for a failed import, so activation retries.
            errors += utf8(source.wstring()) + "\t" + error.what() + '\n';
        }
    };
    for (const auto& root : roots) {
        import(root / L"words.user.tsv", {});
        const auto dictionaries = root / L"dictionaries";
        if (!std::filesystem::is_directory(dictionaries)) continue;
        for (const auto& item : std::filesystem::directory_iterator(dictionaries)) {
            auto extension = item.path().extension().wstring();
            for (auto& ch : extension) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
            const auto name = item.path().stem().u8string();
            if (extension == L".tsv" && valid_dictionary_name(name)) import(item.path(), name);
        }
    }
    const auto log = state / L"errors.txt";
    if (errors.empty()) std::filesystem::remove(log);
    else write(log, errors);
}
std::filesystem::path default_user_path() {
    const auto profile = environment(L"USERPROFILE");
    if (profile.empty() || !profile.is_absolute()) throw std::runtime_error("USERPROFILE is unavailable; specify --user");
    const auto user = profile / L"PrivatePinyin" / L"words.user.tsv";
    // Migration is supplementary to normal operation. Permission/lock failures
    // leave old data intact and can be retried when a host activates again.
    try { migrate_legacy_user_data(user, environment(L"LOCALAPPDATA")); }
    catch (const std::exception&) {}
    return user;
}
}
