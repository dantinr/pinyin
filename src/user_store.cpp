#include "pinyin/user_store.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pinyin {
namespace {
std::runtime_error windows_error(const char* action) {
    return std::runtime_error(std::string(action) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
std::uint64_t parse_count(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid selection count");
    const auto count = std::stoull(value);
    if (count == 0 || count > 1000000000) throw std::runtime_error("selection count out of range");
    return count;
}
}

UserStore::UserStore(std::filesystem::path path, unsigned lock_timeout_ms) : path_(std::filesystem::absolute(std::move(path))) {
    std::filesystem::create_directories(path_.parent_path());
    auto lock_path = path_;
    lock_path += L".lock";
    const auto deadline = GetTickCount64() + std::min(lock_timeout_ms, 1000u);
    HANDLE handle = INVALID_HANDLE_VALUE;
    do {
        handle = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) || GetTickCount64() >= deadline) {
            SetLastError(error); break;
        }
        Sleep(5);
    } while (true);
    if (handle == INVALID_HANDLE_VALUE) throw windows_error("cannot lock user dictionary; another session may be using it");
    lock_ = handle;
}

UserStore::~UserStore() {
    if (lock_) CloseHandle(static_cast<HANDLE>(lock_));
}

UserDictionary UserStore::load() const {
    UserDictionary users;
    if (!std::filesystem::exists(path_)) return users;
    std::ifstream file(path_, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open user dictionary");
    std::size_t number = 0;
    for (std::string line; std::getline(file, line);) {
        ++number;
        if (number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        try {
            const auto first = line.find('\t');
            const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
            if (first == std::string::npos || second == std::string::npos ||
                line.find('\t', second + 1) != std::string::npos)
                throw std::runtime_error("expected exactly three TAB-separated fields");
            auto text = line.substr(0, first);
            validate_text(text);
            auto pronunciation = normalize_pronunciation(line.substr(first + 1, second - first - 1));
            const auto count = parse_count(line.substr(second + 1));
            if (!users.emplace(UserKey{std::move(pronunciation), std::move(text)}, count).second)
                throw std::runtime_error("duplicate user record");
        } catch (const std::exception& e) {
            throw std::runtime_error("user dictionary line " + std::to_string(number) + ": " + e.what());
        }
    }
    if (file.bad()) throw std::runtime_error("failed while reading user dictionary");
    return users;
}

void UserStore::save(const UserDictionary& users) const {
    std::ostringstream contents;
    contents << "# private-pinyin user dictionary v1: text<TAB>pinyin<TAB>selection_count\n";
    for (const auto& item : users) {
        validate_text(item.first.second);
        if (normalize_pronunciation(item.first.first) != item.first.first)
            throw std::runtime_error("user pronunciation is not canonical");
        if (item.second == 0 || item.second > 1000000000)
            throw std::runtime_error("invalid user selection count");
        contents << item.first.second << '\t' << item.first.first << '\t' << item.second << '\n';
    }
    const auto bytes = contents.str();
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = path_;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw windows_error("cannot create temporary dictionary");
    }
    if (file == INVALID_HANDLE_VALUE) throw windows_error("cannot allocate temporary dictionary");
    try {
        for (std::size_t offset = 0; offset < bytes.size();) {
            const auto size = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, size, &written, nullptr))
                throw windows_error("cannot write user dictionary");
            if (written == 0) throw std::runtime_error("user dictionary write made no progress");
            offset += written;
        }
        if (!FlushFileBuffers(file)) throw windows_error("cannot flush user dictionary");
        if (!CloseHandle(file)) {
            file = INVALID_HANDLE_VALUE;
            throw windows_error("cannot close user dictionary");
        }
        file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw windows_error("cannot replace user dictionary");
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        DeleteFileW(temporary.c_str());
        throw;
    }
}

} // namespace pinyin
