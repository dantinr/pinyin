#include "pinyin/dictionary_sync.hpp"
#include <iostream>

int main(int count, char* arguments[]) {
    using namespace pinyin;
    try {
        if (count != 3) throw std::runtime_error("expected server URL and isolated workspace");
        const std::filesystem::path root(arguments[2]);
        const auto first = root / "device-a.tsv", second = root / "device-b.tsv";
        SyncSettings settings{true, std::string(arguments[1]) + "/dictionary.tsv", "test-token"};
        save_sync_settings(first, settings); save_sync_settings(second, settings);
        { UserStore store(first); store.save({{{"ni hao", "你好"}, 4}}, false); }
        auto result = synchronize_dictionary(first, settings);
        if (result.uploaded != 1) throw std::runtime_error("real HTTP initial upload failed");
        result = synchronize_dictionary(second, settings);
        if (result.downloaded.added != 1 || read_user_dictionary(first) != read_user_dictionary(second))
            throw std::runtime_error("real HTTP device restore failed");
        { UserStore store(second); auto words = store.load(); words[{"bei jing", "北京"}] = 2; store.save(words, false); }
        synchronize_dictionary(second, settings); synchronize_dictionary(first, settings);
        if (read_user_dictionary(first) != read_user_dictionary(second) || read_user_dictionary(first).size() != 2)
            throw std::runtime_error("real HTTP bidirectional merge failed");
        const auto repeat = synchronize_dictionary(first, settings);
        if (repeat.uploaded || repeat.downloaded.added || repeat.downloaded.updated) throw std::runtime_error("real HTTP repeated sync was not idempotent");
        auto wrong = settings; wrong.token = "wrong-token";
        if (sync_http_request("GET", wrong, {}, {}).status != 401) throw std::runtime_error("HTTP token authentication failed");
        const auto cloud = sync_http_request("GET", settings, {}, {});
        if (sync_http_request("PUT", settings, cloud.body, "*").status != 412 ||
            sync_http_request("PUT", settings, cloud.body, "\"stale\"").status != 412)
            throw std::runtime_error("HTTP conditional write protection failed");
        auto redirect = settings; redirect.url = std::string(arguments[1]) + "/redirect.tsv";
        if (sync_http_request("GET", redirect, {}, {}).status != 302) throw std::runtime_error("sync followed a redirect with credentials");
        std::cout << "PASS: real HTTP sync, restore, authentication, conflict and redirect checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
