#include "pinyin/dictionary_manager.hpp"
#include "pinyin/main_dictionary.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "pinyin/user_store.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>

namespace pinyin {
namespace {
constexpr const char* header = "# private-pinyin supplementary dictionary v1";
std::runtime_error windows_error(const char* message) {
    return std::runtime_error(std::string(message) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
void ordinary_file(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        throw std::runtime_error("dictionary must be a regular file, not a link or directory");
}
bool enabled(const std::filesystem::path& path) {
    auto marker = path; marker += L".disabled";
    const auto attributes = GetFileAttributesW(marker.c_str());
    const auto error = GetLastError();
    return attributes == INVALID_FILE_ATTRIBUTES && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}
std::vector<std::pair<std::string, std::filesystem::path>> scan(const std::filesystem::path& root) {
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    if (root.empty() || !std::filesystem::exists(root)) return files;
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        auto extension = item.path().extension().wstring();
        for (auto& ch : extension) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        if (extension != L".tsv") continue;
        const auto stem = item.path().stem().wstring();
        std::string name;
        for (auto ch : stem) name += ch < 128 ? static_cast<char>(ch) : '?';
        if (valid_dictionary_name(name)) files.emplace_back(name, item.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}
std::string serialize(const DictionaryFile& file, const char* marker = header) {
    std::ostringstream output; output << marker << '\n';
    for (const auto& line : file.comments) if (line != header && line != marker) output << line << '\n';
    std::map<UserKey, Entry> entries;
    for (const auto& word : file.lexicon.entries()) entries[{word.pronunciation, word.text}] = word;
    for (const auto& item : entries)
        output << item.second.text << '\t' << item.second.pronunciation << '\t' << item.second.weight << '\n';
    return output.str();
}
void write_atomic(const std::filesystem::path& path, const std::string& bytes, bool replace = true) {
    ordinary_file(path);
    std::filesystem::create_directories(path.parent_path());
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = path; temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw windows_error("cannot create dictionary temporary file");
    }
    if (file == INVALID_HANDLE_VALUE) throw windows_error("cannot allocate dictionary temporary file");
    try {
        for (std::size_t offset = 0; offset < bytes.size();) {
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset,
                static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024)), &written, nullptr) || !written)
                throw windows_error("cannot write dictionary");
            offset += written;
        }
        if (!FlushFileBuffers(file)) throw windows_error("cannot flush dictionary");
        const auto closed = CloseHandle(file); file = INVALID_HANDLE_VALUE;
        if (!closed) throw windows_error("cannot close dictionary");
        const auto deadline = GetTickCount64() + 1000;
        for (;;) {
            if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) break;
            const auto error = GetLastError();
            if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) ||
                GetTickCount64() >= deadline) {
                SetLastError(error); throw windows_error("cannot replace dictionary");
            }
            Sleep(5);
        }
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        DeleteFileW(temporary.c_str()); throw;
    }
}
DictionaryFile existing(const std::filesystem::path& path) {
    return std::filesystem::exists(path) ? read_dictionary_file(path) : DictionaryFile{};
}
}
std::filesystem::path default_dictionary_directory() {
    return default_user_path().parent_path() / L"dictionaries";
}
bool valid_dictionary_name(const std::string& name) noexcept {
    if (name.empty() || name.size() > 64 || name.front() < 'a' || name.front() > 'z') return false;
    for (auto ch : name) if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '-') return false;
    if (name == "base" || name == "user" || name == "con" || name == "prn" || name == "aux" || name == "nul") return false;
    if (name.size() == 4 && (name.compare(0, 3, "com") == 0 || name.compare(0, 3, "lpt") == 0) &&
        name[3] >= '1' && name[3] <= '9') return false;
    return true;
}
DictionaryFile read_dictionary_file(const std::filesystem::path& path) {
    ordinary_file(path);
    DictionaryFile result; std::ostringstream contents;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read dictionary");
    bool first = true;
    for (std::string line; std::getline(input, line);) {
        if (first && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        contents << line << '\n';
        if (line.empty()) continue;
        if (line.front() == '#') result.comments.push_back(line);
        else ++result.records;
    }
    if (input.bad()) throw std::runtime_error("cannot finish reading dictionary");
    std::istringstream snapshot(contents.str()); result.lexicon.load(snapshot, true);
    return result;
}
DictionaryManager::DictionaryManager(std::filesystem::path root)
    : root_(std::filesystem::weakly_canonical(std::filesystem::absolute(std::move(root)))) {}
std::filesystem::path DictionaryManager::path(const std::string& name) const {
    if (!valid_dictionary_name(name)) throw std::runtime_error("use a lowercase dictionary name; base/user and Windows device names are reserved");
    auto result = root_ / (name + ".tsv"); ordinary_file(result); return result;
}
std::vector<DictionaryInfo> DictionaryManager::list() const {
    std::vector<DictionaryInfo> result;
    for (const auto& item : scan(root_)) {
        DictionaryInfo info{item.first, item.second, enabled(item.second), 0, {}};
        try { info.entries = read_dictionary_file(item.second).lexicon.size(); }
        catch (const std::exception& error) { info.error = error.what(); }
        result.push_back(std::move(info));
    }
    return result;
}
std::size_t DictionaryManager::import_file(const std::string& name, const std::filesystem::path& source, bool replace) {
    const auto target = path(name);
    UserStore lock(root_ / L".lexicon-manager", 1000);
    auto incoming = read_dictionary_file(source);
    auto file = replace ? DictionaryFile{} : existing(target);
    for (const auto& word : incoming.lexicon.entries()) file.lexicon.add(word);
    for (const auto& comment : incoming.comments)
        if (std::find(file.comments.begin(), file.comments.end(), comment) == file.comments.end()) file.comments.push_back(comment);
    write_atomic(target, serialize(file)); return file.lexicon.size();
}
std::size_t DictionaryManager::upsert(const std::string& name, Entry word) {
    Lexicon validated; validated.add(std::move(word)); const auto replacement = validated.entries().front();
    const auto target = path(name);
    UserStore lock(root_ / L".lexicon-manager", 1000);
    auto file = existing(target); Lexicon updated;
    for (const auto& entry : file.lexicon.entries())
        if (entry.text != replacement.text || entry.pronunciation != replacement.pronunciation) updated.add(entry);
    updated.add(replacement); file.lexicon = std::move(updated);
    write_atomic(target, serialize(file)); return file.lexicon.size();
}
bool DictionaryManager::remove(const std::string& name, const std::string& text, const std::string& pronunciation) {
    validate_text(text); const auto reading = normalize_pronunciation(pronunciation);
    const auto target = path(name); UserStore lock(root_ / L".lexicon-manager", 1000);
    auto file = read_dictionary_file(target); Lexicon updated;
    for (const auto& entry : file.lexicon.entries())
        if (entry.text != text || entry.pronunciation != reading) updated.add(entry);
    if (updated.size() == file.lexicon.size()) return false;
    file.lexicon = std::move(updated); write_atomic(target, serialize(file)); return true;
}
void DictionaryManager::set_enabled(const std::string& name, bool active) {
    const auto target = path(name); UserStore lock(root_ / L".lexicon-manager", 1000);
    if (active) read_dictionary_file(target);
    else if (!std::filesystem::exists(target)) throw std::runtime_error("dictionary does not exist");
    auto marker = target; marker += L".disabled"; ordinary_file(marker);
    if (active) std::filesystem::remove(marker);
    else write_atomic(marker, {});
}
void DictionaryManager::export_file(const std::string& name, const std::filesystem::path& target) const {
    const auto source = path(name);
    UserStore lock(root_ / L".lexicon-manager", 1000);
    write_atomic(std::filesystem::absolute(target), serialize(read_dictionary_file(source)), false);
}
bool SupplementaryDictionaries::refresh() {
    try {
        std::map<std::string, Cached> updates; std::set<std::string> present;
        for (const auto& item : scan(root_)) {
            present.insert(item.first);
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            if (!GetFileAttributesExW(item.second.c_str(), GetFileExInfoStandard, &attributes))
                throw windows_error("cannot inspect dictionary");
            const auto modified = (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                attributes.ftLastWriteTime.dwLowDateTime;
            const auto bytes = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
            const bool active = enabled(item.second);
            const auto found = files_.find(item.first);
            if (found != files_.end() && found->second.modified == modified && found->second.bytes == bytes &&
                found->second.enabled == active) continue;
            Cached file; file.modified = modified; file.bytes = bytes; file.enabled = active;
            if (active) {
                try { file.lexicon = read_dictionary_file(item.second).lexicon; }
                catch (const std::exception&) {} // One damaged file cannot stop typing or other dictionaries.
            }
            updates[item.first] = std::move(file);
        }
        if (updates.empty() && present.size() == files_.size()) return false;
        for (auto item = files_.begin(); item != files_.end();) {
            if (!present.count(item->first) || updates.count(item->first)) item = files_.erase(item);
            else ++item;
        }
        files_.merge(updates); return true;
    } catch (const std::exception&) { return false; } // Keep the cache if the directory is temporarily unreadable.
}
Lexicon SupplementaryDictionaries::merged(const Lexicon& base) const {
    auto result = base;
    for (const auto& file : files_) if (file.second.enabled)
        for (const auto& word : file.second.lexicon.entries()) result.add(word);
    return result;
}
std::filesystem::path main_dictionary_path(const std::filesystem::path& user_path) {
    return user_path.empty() ? std::filesystem::path{} : user_path.parent_path() / L"main.tsv";
}
MainDictionaryMerge merge_user_into_main(const std::filesystem::path& base_path,
    const std::filesystem::path& user_path) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    MainDictionaryMerge result; result.path = main_dictionary_path(std::filesystem::absolute(user_path));
    ordinary_file(result.path);
    for (const auto& source : {user_path, base_path}) {
        std::error_code error;
        if (std::filesystem::weakly_canonical(source) == std::filesystem::weakly_canonical(result.path) ||
            std::filesystem::equivalent(source, result.path, error))
            throw std::runtime_error("main dictionary must differ from its source files");
    }
    const auto users = read_user_dictionary(user_path);
    result.imported = users.size();
    UserStore lock(result.path, 1000);
    auto combined = read_dictionary_file(base_path);
    const bool has_main = std::filesystem::exists(result.path);
    if (has_main) {
        const auto current = read_dictionary_file(result.path);
        for (const auto& word : current.lexicon.entries()) combined.lexicon.add(word);
        for (const auto& comment : current.comments)
            if (std::find(combined.comments.begin(), combined.comments.end(), comment) == combined.comments.end())
                combined.comments.push_back(comment);
    }
    std::set<UserKey> known;
    for (const auto& word : combined.lexicon.entries()) known.emplace(word.pronunciation, word.text);
    for (const auto& word : users) {
        if (known.emplace(word.first).second) {
            combined.lexicon.add({word.first.second, word.first.first, 12000}); ++result.added;
        } else ++result.unchanged;
    }
    result.total = combined.lexicon.size();
    if (!result.added) return result;
    const auto backups = result.path.parent_path() / L"backups";
    std::filesystem::create_directories(backups);
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t timestamp[32]{};
    swprintf_s(timestamp, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond);
    static std::atomic<unsigned long> sequence{0};
    const auto original = has_main ? result.path : base_path;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto backup = backups / (L"main." + std::wstring(timestamp) + L"." +
            std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence) + L".tsv");
        if (CopyFileW(original.c_str(), backup.c_str(), TRUE)) { result.backup = backup; break; }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw windows_error("cannot back up main dictionary");
    }
    if (result.backup.empty()) throw std::runtime_error("cannot allocate main dictionary backup");
    write_atomic(result.path, serialize(combined,
        "# private-pinyin main dictionary v1: text<TAB>pinyin<TAB>positive weight"));
    return result;
}
bool MainDictionary::refresh() {
    if (path_.empty()) return false;
    try {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &attributes)) {
            const auto error = GetLastError();
            if ((error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) && loaded_) {
                lexicon_ = Lexicon{}; loaded_ = false; return true;
            }
            return false;
        }
        if (attributes.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return false;
        const auto modified = (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
            attributes.ftLastWriteTime.dwLowDateTime;
        const auto bytes = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
        if (loaded_ && modified_ == modified && bytes_ == bytes) return false;
        auto file = read_dictionary_file(path_);
        lexicon_ = std::move(file.lexicon); modified_ = modified; bytes_ = bytes; loaded_ = true;
        return true;
    } catch (const std::exception&) { return false; }
}
Lexicon MainDictionary::merged(const Lexicon& base) const {
    auto result = base;
    for (const auto& word : lexicon_.entries()) result.add(word);
    return result;
}
}
