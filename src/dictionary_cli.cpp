#include "dictionary_cli.hpp"
#include "pinyin/dictionary_manager.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "pinyin/user_store.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <set>
#include <sstream>

namespace {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("invalid Unicode argument");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 0x20) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += static_cast<char>(ch);
    }
    return result + '"';
}
std::uint64_t number(const std::wstring& value) {
    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error("expected a positive integer");
    const auto result = std::stoull(value);
    if (!result || result > 1000000000) throw std::runtime_error("integer must be between 1 and 1000000000");
    return result;
}
void info(std::ostream& output, const pinyin::DictionaryInfo& value, bool main = false) {
    output << "{\"name\":" << quoted(value.name) << ",\"role\":" << quoted(main ? "main" : "supplementary")
        << ",\"path\":" << quoted(utf8(value.path.wstring())) << ",\"enabled\":" << (value.enabled ? "true" : "false")
        << ",\"readOnly\":" << (main ? "true" : "false") << ",\"entries\":" << value.entries
        << ",\"error\":" << quoted(value.error) << '}';
}
}
int run_dictionary_cli(int argc, wchar_t* argv[], const std::filesystem::path& bundled) {
    std::string command;
    try {
        command = argc > 1 ? utf8(argv[1]) : "help";
        if (command == "help" || command == "--help") {
            std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"help\",\"commands\":["
                "\"list\",\"validate\",\"import\",\"upsert\",\"remove\",\"enable\",\"disable\",\"export\",\"query\"],"
                "\"documentation\":\"docs/agent-dictionaries.md\",\"defaultRoot\":\"%USERPROFILE%/PrivatePinyin/dictionaries\"}\n";
            return 0;
        }
        std::map<std::wstring, std::wstring> options;
        bool replace = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring key(argv[i]);
            if (key == L"--replace") {
                if (replace) throw std::runtime_error("duplicate --replace");
                replace = true; continue;
            }
            if (key != L"--root" && key != L"--base" && key != L"--name" && key != L"--file" &&
                key != L"--text" && key != L"--pinyin" && key != L"--weight" && key != L"--limit")
                throw std::runtime_error("unknown lexicon option");
            if (++i >= argc || !*argv[i]) throw std::runtime_error("missing lexicon option value");
            if (!options.emplace(key, argv[i]).second) throw std::runtime_error("duplicate lexicon option");
        }
        std::set<std::wstring> allowed;
        if (command == "validate") allowed = {L"--file"};
        else if (command == "list") allowed = {L"--root", L"--base"};
        else if (command == "query") allowed = {L"--root", L"--base", L"--pinyin", L"--limit"};
        else if (command == "import" || command == "export") allowed = {L"--root", L"--name", L"--file"};
        else if (command == "upsert") allowed = {L"--root", L"--name", L"--text", L"--pinyin", L"--weight"};
        else if (command == "remove") allowed = {L"--root", L"--name", L"--text", L"--pinyin"};
        else if (command == "enable" || command == "disable") allowed = {L"--root", L"--name"};
        else throw std::runtime_error("unknown lexicon command; use lexicon help");
        for (const auto& option : options) if (!allowed.count(option.first))
            throw std::runtime_error("option does not apply to this lexicon command");
        if (replace && command != "import") throw std::runtime_error("--replace is only supported by import");
        auto required = [&](const wchar_t* key) -> const std::wstring& {
            const auto found = options.find(key);
            if (found == options.end()) throw std::runtime_error("required lexicon option is missing");
            return found->second;
        };
        std::ostringstream result;
        result << "{\"schema\":1,\"ok\":true,\"command\":" << quoted(command);
        if (command == "validate") {
            const auto path = std::filesystem::absolute(required(L"--file"));
            const auto file = pinyin::read_dictionary_file(path);
            result << ",\"path\":" << quoted(utf8(path.wstring())) << ",\"records\":" << file.records
                << ",\"entries\":" << file.lexicon.size() << ",\"duplicates\":" << file.records - file.lexicon.size();
        } else {
            const auto root = options.count(L"--root") ? std::filesystem::path(options.at(L"--root")) :
                pinyin::default_dictionary_directory();
            pinyin::DictionaryManager manager(root);
            result << ",\"root\":" << quoted(utf8(manager.root().wstring()));
            const auto base = options.count(L"--base") ? std::filesystem::path(options.at(L"--base")) : bundled;
            if (command == "list") {
                pinyin::DictionaryInfo main{"base", std::filesystem::absolute(base), true, 0, {}};
                try { pinyin::Lexicon lexicon; lexicon.load(base); main.entries = lexicon.size(); }
                catch (const std::exception& error) { main.error = error.what(); }
                result << ",\"dictionaries\":["; info(result, main, true);
                for (const auto& item : manager.list()) { result << ','; info(result, item); }
                result << ']';
            } else if (command == "query") {
                pinyin::Lexicon lexicon; lexicon.load(base);
                pinyin::SupplementaryDictionaries files(manager.root()); files.refresh(); lexicon = files.merged(lexicon);
                const auto limit = options.count(L"--limit") ? number(options.at(L"--limit")) : 10;
                const auto words = lexicon.lookup_composition(utf8(required(L"--pinyin")), {}, static_cast<std::size_t>(limit));
                result << ",\"candidates\":[";
                bool first = true;
                for (const auto& word : words) {
                    if (!first) result << ','; first = false;
                    result << "{\"text\":" << quoted(word.text) << ",\"pinyin\":" << quoted(word.pronunciation)
                        << ",\"weight\":" << word.weight << ",\"inputEnd\":" << word.input_end << '}';
                }
                result << "],\"warnings\":[";
                first = true;
                for (const auto& item : manager.list()) if (item.enabled && !item.error.empty()) {
                    if (!first) result << ','; first = false; info(result, item);
                }
                result << ']';
            } else {
                const auto name = utf8(required(L"--name"));
                result << ",\"name\":" << quoted(name);
                if (command == "import")
                    result << ",\"entries\":" << manager.import_file(name, required(L"--file"), replace);
                else if (command == "upsert")
                    result << ",\"entries\":" << manager.upsert(name, {utf8(required(L"--text")), utf8(required(L"--pinyin")),
                        options.count(L"--weight") ? number(options.at(L"--weight")) : 100});
                else if (command == "remove")
                    result << ",\"removed\":" << (manager.remove(name, utf8(required(L"--text")), utf8(required(L"--pinyin"))) ? "true" : "false");
                else if (command == "export") {
                    const auto target = std::filesystem::absolute(required(L"--file"));
                    manager.export_file(name, target); result << ",\"path\":" << quoted(utf8(target.wstring()));
                } else {
                    manager.set_enabled(name, command == "enable");
                    result << ",\"enabled\":" << (command == "enable" ? "true" : "false");
                }
            }
        }
        std::cout << result.str() << "}\n"; return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"schema\":1,\"ok\":false,\"command\":" << quoted(command) << ",\"error\":" << quoted(error.what()) << "}\n";
        return 1;
    }
}
int run_personal_cli(int argc, wchar_t* argv[]) {
    std::string command;
    try {
        command = argc > 1 ? utf8(argv[1]) : "help";
        if (command == "help" || command == "--help") {
            if (argc > 2) throw std::runtime_error("personal help does not accept options");
            std::cout << "{\"schema\":1,\"ok\":true,\"command\":\"help\",\"commands\":[\"validate\",\"export\",\"merge\"],"
                "\"documentation\":\"docs/personal-dictionaries.md\","
                "\"defaultUser\":\"%USERPROFILE%/PrivatePinyin/words.user.tsv\"}\n";
            return 0;
        }
        if (command != "validate" && command != "export" && command != "merge")
            throw std::runtime_error("unknown personal command; use personal help");
        std::map<std::wstring, std::wstring> options;
        bool overwrite = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring key(argv[i]);
            if (key == L"--overwrite") {
                if (overwrite) throw std::runtime_error("duplicate --overwrite");
                overwrite = true; continue;
            }
            if (key != L"--file" && key != L"--user") throw std::runtime_error("unknown personal option");
            if (++i >= argc || !*argv[i] || std::wstring(argv[i]).compare(0, 2, L"--") == 0)
                throw std::runtime_error("missing personal option value");
            if (!options.emplace(key, argv[i]).second) throw std::runtime_error("duplicate personal option");
        }
        if (!options.count(L"--file")) throw std::runtime_error("--file is required");
        if (overwrite && command != "export") throw std::runtime_error("--overwrite is only supported by export");
        if (command == "validate" && options.count(L"--user")) throw std::runtime_error("validate does not accept --user");
        const auto file = std::filesystem::absolute(options.at(L"--file"));
        std::ostringstream result;
        result << "{\"schema\":1,\"ok\":true,\"command\":" << quoted(command)
            << ",\"path\":" << quoted(utf8(file.wstring()));
        if (command == "validate") {
            const auto incoming = pinyin::read_personal_dictionary_file(file);
            result << ",\"records\":" << incoming.records << ",\"entries\":" << incoming.users.size()
                << ",\"duplicates\":" << incoming.records - incoming.users.size();
        } else {
            const auto user = options.count(L"--user") ? std::filesystem::absolute(options.at(L"--user")) : pinyin::default_user_path();
            result << ",\"user\":" << quoted(utf8(user.wstring()));
            if (command == "export") result << ",\"entries\":" << pinyin::export_user_dictionary(user, file, overwrite);
            else {
                const auto merged = pinyin::merge_user_dictionary(user, file);
                result << ",\"imported\":" << merged.imported << ",\"added\":" << merged.added
                    << ",\"updated\":" << merged.updated << ",\"unchanged\":" << merged.unchanged
                    << ",\"total\":" << merged.total << ",\"backup\":" << quoted(utf8(merged.backup.wstring()));
            }
        }
        std::cout << result.str() << "}\n"; return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"schema\":1,\"ok\":false,\"command\":" << quoted(command)
            << ",\"error\":" << quoted(error.what()) << "}\n";
        return 1;
    }
}
