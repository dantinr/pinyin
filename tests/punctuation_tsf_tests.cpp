#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/learning_dictionary.hpp"
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
pinyin::UserDictionary words(const std::filesystem::path& path) { pinyin::UserStore store(path); return store.load(); }
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace; const auto user_path = pinyin::default_user_path();
        {
            Harness ime(argv[1]);
            check(ime.key(VK_OEM_COMMA) && ime.store->text == L"，" && ime.compositions() == 0,
                "standalone comma did not insert without starting a composition");
            ime.key(VK_OEM_PERIOD);
            check(ime.store->text == L"，。" && words(user_path).empty(), "standalone punctuation was learned");
            ime.type("nh"); check(ime.key(VK_OEM_COMMA), "punctuation did not confirm abbreviation");
            check(ime.store->text == L"，。你好，" && ime.compositions() == 0 &&
                words(user_path).size() == 1 && words(user_path).at({"ni hao", "你好"}) == 1,
                "punctuation commit learned its symbol or failed to end composition");
            ime.type("nihao"); ime.key(VK_DOWN); ime.modified_key('1', {VK_SHIFT});
            check(ime.store->text == L"，。你好，拟好！" && words(user_path).at({"ni hao", "拟好"}) == 1,
                "shifted punctuation selected a digit instead of highlighted word");
            ime.type("v"); ime.key(VK_OEM_PERIOD);
            check(ime.store->text == L"，。你好，拟好！v。" && words(user_path).size() == 2,
                "unknown raw text was discarded or learned");
            ime.close(); check(ime.unload_result == S_OK, "punctuation service leaked DLL references");
        }
        {
            Harness ime(argv[1]);
            BOOL eaten = FALSE;
            ime.modified_key(VK_OEM_7, {VK_SHIFT}); // Opening double quote.
            ime.type("nh"); ime.modified_key(VK_OEM_7, {VK_SHIFT});
            check(ime.store->text == L"“你好”" && ime.compositions() == 0, "quoted Chinese word did not close its quote");
            ime.keys->OnTestKeyDown(ime.context.Get(), VK_OEM_7, 0, &eaten);
            ime.keys->OnTestKeyDown(ime.context.Get(), VK_OEM_7, 0, &eaten);
            ime.key(VK_OEM_7); ime.type("xi'an"); ime.key(VK_SPACE); ime.key(VK_OEM_7);
            check(ime.store->text == L"“你好”‘西安’", "quote probing or pinyin separator corrupted quote state");
            ime.modified_key('9', {VK_SHIFT}); ime.type("nh"); ime.modified_key('0', {VK_SHIFT});
            check(ime.store->text == L"“你好”‘西安’（你好）", "shifted parentheses lost pending pinyin");
            const auto before = ime.store->text;
            ime.modified_key(VK_OEM_MINUS, {VK_SHIFT}); ime.modified_key('6', {VK_SHIFT}); ime.key(VK_OEM_5);
            check(ime.store->text == before + L"——……、", "multi-character punctuation or enumeration comma failed");
            ime.key(VK_SHIFT); ime.key_up(VK_SHIFT);
            check(!ime.key(VK_OEM_COMMA) && !ime.modified_key('1', {VK_SHIFT}), "English mode swallowed punctuation");
            ime.store->insert_external(L",!");
            ime.key(VK_SHIFT); ime.key_up(VK_SHIFT);
            ime.modified_key(VK_OEM_7, {VK_SHIFT});
            check(ime.store->text == before + L"——……、,!“", "English punctuation or quote mode reset failed");
            const auto unchanged = ime.store->text;
            check(!ime.modified_key(VK_OEM_COMMA, {VK_CONTROL}) && !ime.modified_key(VK_OEM_PERIOD, {VK_MENU}) &&
                !ime.modified_key(VK_OEM_COMMA, {VK_CAPITAL}) && ime.store->text == unchanged,
                "modifier shortcuts or CapsLock were intercepted");
            ime.close(); check(ime.unload_result == S_OK, "quote service leaked DLL references");
        }
        {
            Harness ime(argv[1]); const auto previous = words(user_path);
            ime.type("https"); ime.modified_key(VK_OEM_1, {VK_SHIFT});
            ime.key(VK_OEM_2); ime.key(VK_OEM_2); ime.type("example"); ime.key(VK_OEM_PERIOD);
            ime.type("com"); ime.modified_key(VK_OEM_2, {VK_SHIFT});
            check(ime.store->text == L"https://example.com?" && ime.compositions() == 0 && words(user_path) == previous,
                "URL prefix/domain/query converted or learned as Chinese");
            ime.store->insert_external(L" 中文"); ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"https://example.com? 中文，", "URL mode leaked into Chinese text");
            ime.store->insert_external(L" https://example.com/" + std::wstring(4096, L'x'));
            ime.modified_key(VK_OEM_2, {VK_SHIFT});
            check(ime.store->text.back() == L'?', "long URL suffix lost literal punctuation");
            ime.close(); check(ime.unload_result == S_OK, "URL service leaked DLL references");
        }
        {
            Harness ime(argv[1]); ime.store->insert_external(L"3"); ime.key(VK_OEM_PERIOD);
            ime.store->insert_external(L"14");
            check(ime.store->text == L"3.14", "decimal number was corrupted");
            ime.type("nh"); ime.key(VK_OEM_PERIOD);
            check(ime.store->text == L"3.14你好。", "decimal context prevented following Chinese input");
            ime.store->insert_external(L" 12"); ime.modified_key(VK_OEM_1, {VK_SHIFT}); ime.store->insert_external(L"30");
            check(ime.store->text == L"3.14你好。 12:30", "time colon was corrupted");
            ime.close(); check(ime.unload_result == S_OK, "number service leaked DLL references");
        }
        for (const auto scope : {IS_URL, IS_EMAIL_SMTPEMAILADDRESS, IS_NUMBER}) {
            Harness ime(argv[1], scope); const auto previous = words(user_path);
            ime.type("example"); ime.key(VK_OEM_PERIOD); ime.type("com"); ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"example.com," && words(user_path) == previous, "literal InputScope punctuation converted or learned");
            // Space still allows explicit Chinese selection in these fields.
            ime.type("nh"); ime.key(VK_SPACE);
            check(ime.store->text == L"example.com,你好", "literal InputScope blocked explicit Chinese input");
            ime.close(); check(ime.unload_result == S_OK, "literal context leaked DLL references");
        }
        for (const auto scope : {IS_PASSWORD, IS_NUMERIC_PIN}) {
            Harness ime(argv[1], scope); const auto previous = words(user_path);
            check(!ime.key(VK_OEM_COMMA) && !ime.modified_key('1', {VK_SHIFT}) &&
                ime.store->text.empty() && words(user_path) == previous, "password/PIN punctuation was intercepted or learned");
            ime.close(); check(ime.unload_result == S_OK, "sensitive context leaked DLL references");
        }
        {
            Harness ime(argv[1]); const auto previous = words(user_path);
            ime.type("nh"); ime.store->reject_writes = true;
            check(!ime.key(VK_OEM_COMMA) && ime.store->text == L"nh" && words(user_path) == previous,
                "failed punctuation write consumed text or learned a word");
            ime.store->reject_writes = false; ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"你好，" && ime.compositions() == 0, "punctuation did not recover after rejected write");
            ime.store->reject_writes = true;
            check(!ime.modified_key(VK_OEM_7, {VK_SHIFT}), "failed quote write was swallowed");
            ime.store->reject_writes = false; ime.modified_key(VK_OEM_7, {VK_SHIFT});
            check(ime.store->text == L"你好，“", "failed quote write advanced quote state");
            ime.close(); check(ime.unload_result == S_OK, "rejected edit context leaked DLL references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF punctuation checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
