#include "pinyin/lexicon.hpp"
#include "pinyin/user_store.hpp"
#include "pinyin/learning_dictionary.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) throw std::runtime_error("cannot locate executable");
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) throw std::runtime_error("invalid Unicode argument/input");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

bool read_line(std::string& line) {
    const auto handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) {
        if (!std::getline(std::cin, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    }
    std::wstring value;
    wchar_t buffer[256];
    do {
        DWORD read = 0;
        if (!ReadConsoleW(handle, buffer, 256, &read, nullptr)) throw std::runtime_error("cannot read console input");
        if (read == 0) return false;
        value.append(buffer, read);
        if (value.size() > 4096) throw std::runtime_error("console line is too long");
    } while (value.back() != L'\n');
    while (!value.empty() && (value.back() == L'\n' || value.back() == L'\r')) value.pop_back();
    line = utf8(value);
    return true;
}

void usage() {
    std::cout << "Windows 中文拼音输入法：词库工具\n"
        "用法：private_pinyin [--dict 文件] [--learn | --no-learn] [--user 文件] [--query 拼音 | --sentence 拼音]\n"
        "命令行交互默认不读取或写入用户词库；--learn 显式开启本地学习。\n"
        "系统输入法默认开启本地学习；设置：--ime-learning on|off|status|clear。\n"
        "交互：输入拼音查词；输入候选编号确认；/add 拼音 词语；/clear；/help；/quit。\n"
        "多音节自造词请用分隔符，例如：/add yin'si 隐私\n"
        "--query 查询完整词条；--sentence 生成离线整句候选；两者均不读取或保存个人词库。\n"
        "支持全拼、简拼和混输，例如 nihao / nh / nhao；zh/ch/sh 也可缩写为 z/c/s。\n"
        "自造词读音仍需填写完整音节；暂不支持补全、模糊音或纠错。\n";
}

pinyin::Lexicon with_users(const pinyin::Lexicon& base, const pinyin::UserDictionary& users) {
    auto result = base;
    for (const auto& item : users) result.add({item.first.second, item.first.first, 1});
    return result;
}

void display(const std::vector<pinyin::Candidate>& candidates) {
    if (candidates.empty()) std::cout << "没有完整匹配的词条。\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& entry = candidates[i];
        std::cout << i + 1 << ". " << entry.text << "  [" << entry.pronunciation << "]";
        if (entry.selections) std::cout << "  本地选择 " << entry.selections << " 次";
        std::cout << '\n';
    }
}
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        auto dictionary = executable_directory() / L"data" / L"base.tsv";
        std::filesystem::path user_path;
        std::string query;
        bool query_mode = false;
        bool sentence_mode = false;
        bool learning = false;
        bool learning_specified = false;
        std::string ime_learning;
        for (int i = 1; i < argc; ++i) {
            const std::wstring option(argv[i]);
            if (option == L"--help" || option == L"-h") { usage(); return 0; }
            if (option == L"--learn" || option == L"--no-learn") {
                if (learning_specified) throw std::runtime_error("specify only one learning mode");
                learning_specified = true;
                learning = option == L"--learn";
            } else if (option == L"--ime-learning") {
                if (!ime_learning.empty() || ++i >= argc) throw std::runtime_error("expected one IME learning command");
                ime_learning = utf8(argv[i]);
                if (ime_learning.empty()) throw std::runtime_error("expected an IME learning command");
            } else if (option == L"--dict" || option == L"--user" || option == L"--query" || option == L"--sentence") {
                if (++i >= argc) throw std::runtime_error("missing option value");
                if (option == L"--dict") dictionary = argv[i];
                else if (option == L"--user") user_path = argv[i];
                else {
                    if (query_mode) throw std::runtime_error("specify one query or sentence input");
                    query = utf8(argv[i]); query_mode = true; sentence_mode = option == L"--sentence";
                }
            } else throw std::runtime_error("unknown option; use --help");
        }
        if (!ime_learning.empty()) {
            if (query_mode || learning_specified) throw std::runtime_error("IME settings cannot be combined with query/CLI learning");
            if (user_path.empty()) user_path = pinyin::default_user_path();
            if (ime_learning == "on" || ime_learning == "off") {
                pinyin::set_ime_learning(user_path, ime_learning == "on");
            } else if (ime_learning == "clear") {
                if (std::filesystem::exists(user_path)) { pinyin::UserStore store(user_path, 100); store.save({}); }
                std::cout << "系统输入法个人词条和选择次数已清空。\n"; return 0;
            } else if (ime_learning != "status") throw std::runtime_error("use --ime-learning on|off|status|clear");
            std::cout << "系统输入法本地学习" << (pinyin::ime_learning_enabled(user_path) ? "已开启" : "已关闭（无痕）") << "。\n";
            std::cout << "设置在下一轮拼音输入时生效；关闭后不读取或写入个人词库。\n";
            return 0;
        }
        pinyin::Lexicon base;
        base.load(dictionary);
        if (query_mode) {
            // Batch lookup is always stateless, even if --learn was also passed.
            display(sentence_mode ? base.decode(query) : base.lookup(query));
            return 0;
        }
        std::unique_ptr<pinyin::UserStore> store;
        pinyin::UserDictionary users;
        if (learning) {
            if (user_path.empty()) user_path = pinyin::default_user_path();
            store = std::make_unique<pinyin::UserStore>(user_path);
            users = store->load();
        }
        auto active = with_users(base, users);
        usage();
        std::cout << "已加载 " << base.size() << " 条基础词；学习" << (learning ? "已开启" : "已关闭（无痕）") << "。\n";
        if (store) std::cout << "用户词库：" << utf8(store->path().wstring()) << '\n';
        std::vector<pinyin::Candidate> candidates;
        for (std::string line; std::cout << "> " << std::flush, read_line(line);) {
            if (line.empty()) continue;
            try {
                if (line == "/quit") break;
                if (line == "/help") { usage(); continue; }
                if (line == "/clear") {
                    if (!store) { std::cout << "学习未开启，没有已加载的用户词库。\n"; continue; }
                    store->save({});
                    users.clear();
                    active = base;
                    candidates.clear();
                    std::cout << "个人词条和选择次数已清空。\n";
                    continue;
                }
                if (line.rfind("/add ", 0) == 0) {
                    if (!store) { std::cout << "自造词需要使用 --learn 开启本地存储。\n"; continue; }
                    const auto separator = line.find(' ', 5);
                    if (separator == std::string::npos) throw std::runtime_error("use /add yin'si 隐私");
                    pinyin::Candidate word;
                    word.pronunciation = pinyin::normalize_pronunciation(line.substr(5, separator - 5));
                    word.text = line.substr(separator + 1);
                    auto updated = users;
                    pinyin::learn(updated, word);
                    auto updated_lexicon = with_users(base, updated);
                    store->save(updated);
                    users = std::move(updated);
                    active = std::move(updated_lexicon);
                    candidates.clear();
                    std::cout << "自造词已保存。\n";
                    continue;
                }
                if (line.find_first_not_of("0123456789") == std::string::npos) {
                    const auto selected = std::stoull(line);
                    if (selected == 0 || selected > candidates.size()) throw std::runtime_error("invalid candidate number");
                    const auto word = candidates[static_cast<std::size_t>(selected - 1)];
                    if (store) {
                        auto updated = users;
                        pinyin::learn(updated, word);
                        store->save(updated);
                        users = std::move(updated);
                    }
                    std::cout << "确认：" << word.text << '\n';
                    candidates.clear();
                    continue;
                }
                candidates.clear();
                candidates = active.lookup(line, users);
                display(candidates);
            } catch (const std::exception& e) {
                candidates.clear();
                std::cerr << "错误：" << e.what() << '\n';
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "启动失败：" << e.what() << '\n';
        return 1;
    }
}
