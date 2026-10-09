#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "language_bar.hpp"
#include <iostream>

namespace {
using namespace pinyin;
using namespace pinyin::ime;
using namespace pinyin::testing;
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
UserDictionary words(const std::filesystem::path& path) { UserStore store(path); return store.load(); }
ComPtr<ITfLangBarItemButton> button(Harness& ime) {
    ComPtr<ITfLangBarItemMgr> manager; ComPtr<ITfLangBarItem> item;
    Harness::require(ime.manager.As(&manager), "get language bar manager");
    Harness::require(manager->GetItem(GUID_LBI_INPUTMODE, &item), "get language bar item");
    ComPtr<ITfLangBarItemButton> result; Harness::require(item.As(&result), "get language bar button"); return result;
}
std::wstring label(ITfLangBarItemButton* bar, bool tooltip = false) {
    BSTR value = nullptr;
    Harness::require(tooltip ? bar->GetTooltipString(&value) : bar->GetText(&value), "get language bar text");
    std::wstring result(value, SysStringLen(value)); SysFreeString(value); return result;
}
// A real TSF host receives keys the IME declines. This simulates the host's
// resulting text edits without injecting keys into any desktop application.
void type_app(Harness& ime, const std::string& text) {
    for (const char letter : text) {
        bool handled;
        if (letter >= 'a' && letter <= 'z') handled = ime.key(letter - 'a' + 'A');
        else if (letter >= 'A' && letter <= 'Z') handled = ime.modified_key(letter, {VK_SHIFT});
        else if (letter >= '0' && letter <= '9') handled = ime.key(letter);
        else switch (letter) {
        case ' ': handled = ime.key(VK_SPACE); break;
        case ',': handled = ime.key(VK_OEM_COMMA); break;
        case '.': handled = ime.key(VK_OEM_PERIOD); break;
        case '\'': handled = ime.key(VK_OEM_7); break;
        case '?': handled = ime.modified_key(VK_OEM_2, {VK_SHIFT}); break;
        case '!': handled = ime.modified_key('1', {VK_SHIFT}); break;
        case ':': handled = ime.modified_key(VK_OEM_1, {VK_SHIFT}); break;
        default: throw std::runtime_error("unsupported host test character");
        }
        if (!handled) { ime.store->insert_external(std::wstring(1, static_cast<wchar_t>(letter))); Harness::pump(); }
    }
}
}
int wmain(int argc, wchar_t* argv[]) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace; const auto path = default_user_path();
        {
            Harness ime(argv[1]); auto bar = button(ime);
            ime.type("hello");
            check(ime.store->text == L"hello" && ime.compositions() == 1 && label(bar.Get()) == L"中",
                "English was confirmed before its delimiter");
            BOOL eaten = FALSE;
            Harness::require(ime.keys->OnTestKeyDown(ime.context.Get(), VK_SPACE, 0, &eaten), "probe English space");
            Harness::require(ime.keys->OnTestKeyDown(ime.context.Get(), VK_SPACE, 0, &eaten), "repeat English space probe");
            check(label(bar.Get()) == L"中" && ime.store->text == L"hello", "key probing changed English state or text");
            check(ime.key(VK_SPACE) && ime.store->text == L"hello " && ime.compositions() == 0 && label(bar.Get()) == L"英",
                "English space selected Chinese or lost its space");
            check(label(bar.Get(), true).find(L"自动识别") != std::wstring::npos, "automatic English tooltip missing");
            type_app(ime, "world, how are you? I'm fine.  2026!");
            check(ime.store->text == L"hello world, how are you? I'm fine.  2026!" && ime.compositions() == 0 && words(path).empty(),
                "English sentence lost spaces, case, punctuation, digits, or learned application text");
            Harness::require(ime.keys->OnTestKeyDown(ime.context.Get(), VK_RETURN, 0, &eaten), "probe English Return");
            check(eaten && label(bar.Get()) == L"英", "Return probing ended the English segment");
            check(!ime.key(VK_RETURN) && label(bar.Get()) == L"中", "English Return was swallowed or did not restore Chinese");
            ime.store->insert_external(L"\n"); ime.type("nh"); ime.key(VK_SPACE);
            check(ime.store->text == L"hello world, how are you? I'm fine.  2026!\n你好" && words(path).at({"ni hao", "你好"}) == 1,
                "Chinese input or learning did not resume after the English sentence");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "English sentence leaked DLL references");
        }
        for (const auto& example : {std::string("Hello world!"), std::string("HELLO world!"),
                                    std::string("I am happy."), std::string("You are welcome."), std::string("don't do that!"),
                                    std::string("hello, world!")}) {
            Harness ime(argv[1]); auto bar = button(ime); const auto previous = words(path);
            type_app(ime, example);
            check(ime.store->text == std::wstring(example.begin(), example.end()) && ime.compositions() == 0 &&
                label(bar.Get()) == L"英" && words(path) == previous, "capitalized word, contraction, or initial English punctuation failed");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "capitalized English leaked DLL references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime); const auto previous = words(path);
            check(ime.key(VK_SHIFT) && ime.modified_key('H', {VK_SHIFT}) && !ime.key_up(VK_SHIFT) &&
                ime.store->text == L"H" && ime.compositions() == 1 && label(bar.Get()) == L"中",
                "initial Shift+letter was committed or toggled the mode");
            check(ime.key(VK_ESCAPE) && ime.store->text.empty() && ime.compositions() == 0,
                "Escape left the initial uppercase letter committed");
            type_app(ime, "Hello"); ime.key(VK_HOME); ime.key(VK_DELETE);
            check(ime.store->text == L"ello" && ime.compositions() == 1, "literal caret editing split the word");
            check(ime.modified_key('H', {VK_SHIFT}), "uppercase insertion at the literal caret was passed to the host");
            ime.key(VK_END); ime.key(VK_BACK); type_app(ime, "o");
            check(ime.store->text == L"Hello" && ime.compositions() == 1, "literal backspace committed the initial capital");
            ime.store->reject_writes = true;
            check(!ime.key(VK_SPACE) && ime.store->text == L"Hello" && ime.compositions() == 1 && label(bar.Get()) == L"中",
                "failed literal commit consumed the word or enabled automatic English");
            ime.store->reject_writes = false;
            check(ime.key(VK_SPACE) && ime.store->text == L"Hello " && ime.compositions() == 0 && label(bar.Get()) == L"英" &&
                words(path) == previous, "literal commit did not preserve the whole word, space, or learning state");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "literal editing leaked DLL references");
        }
        {
            set_automatic_english(path, false);
            Harness ime(argv[1]); auto bar = button(ime); const auto previous = words(path);
            type_app(ime, "You");
            check(ime.store->text == L"You" && ime.compositions() == 1, "automatic-English opt-out bypassed uppercase composition");
            ime.key(VK_SPACE);
            check(ime.store->text == L"You " && ime.compositions() == 0 && label(bar.Get()) == L"中",
                "uppercase word became Chinese or lost its space with automatic English disabled");
            type_app(ime, "Version2026");
            check(ime.store->text == L"You Version2026" && ime.compositions() == 1, "literal digits split or committed the uppercase word");
            ime.key(VK_RETURN); type_app(ime, "I'm!");
            check(ime.store->text == L"You Version2026I'm!" && ime.compositions() == 0 && words(path) == previous,
                "uppercase contraction used Chinese punctuation or became learnable");
            type_app(ime, "H"); ime.key(VK_BACK); ime.type("nh"); ime.key(VK_SPACE);
            check(ime.store->text == L"You Version2026I'm!你好", "deleting the final uppercase character did not restore Chinese input");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "disabled automatic-English literal input leaked references");
            set_automatic_english(path, true);
        }
        for (const auto& spelling : {"you", "he", "she", "can", "men", "niha", "beijin", "nh"}) {
            Harness ime(argv[1]); auto bar = button(ime); ime.type(spelling); ime.key(VK_SPACE);
            check(label(bar.Get()) == L"中" && ime.store->text != wide(std::string(spelling) + ' '),
                "English detection displaced ambiguous pinyin, completion, or abbreviation");
            ime.key(VK_ESCAPE); bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "ambiguous pinyin leaked references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime); const auto previous = words(path);
            ime.type("hello"); ime.store->reject_writes = true;
            check(!ime.key(VK_SPACE) && ime.store->text == L"hello" && ime.compositions() == 1 && label(bar.Get()) == L"中" &&
                words(path) == previous, "rejected English commit consumed input or changed mode");
            ime.store->reject_writes = false; ime.key(VK_SPACE); type_app(ime, "world");
            check(ime.store->text == L"hello world" && label(bar.Get()) == L"英" && words(path) == previous,
                "English could not recover after a rejected write");
            check(ime.key(VK_SHIFT) && ime.key_up(VK_SHIFT) && label(bar.Get()) == L"中", "solo Shift did not exit automatic English");
            ime.type("nh"); ime.key(VK_SPACE);
            check(ime.store->text == L"hello world你好", "Shift exit did not restore pinyin");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "rejected English leaked references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime);
            ime.type("hello"); ime.key(VK_SPACE); ime.keys->OnSetFocus(FALSE); Harness::pump();
            check(label(bar.Get()) == L"中" && ime.store->text == L"hello ", "focus loss retained automatic English or changed text");
            ime.type("nh"); ime.key(VK_SPACE);
            check(ime.store->text == L"hello 你好", "focus return did not restore pinyin");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "English focus loss leaked references");
        }
        for (const auto key : {VK_TAB, VK_ESCAPE, VK_LEFT, VK_HOME}) {
            Harness ime(argv[1]); auto bar = button(ime); ime.type("hello"); ime.key(VK_SPACE);
            check(!ime.key(key) && label(bar.Get()) == L"中", "English boundary/navigation key was swallowed or retained the mode");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "English boundary leaked references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime); ime.type("hello"); ime.key(VK_SPACE);
            ComPtr<TextStore> next_store; next_store.Attach(new TextStore);
            ComPtr<ITfDocumentMgr> next_document; ComPtr<ITfContext> next_context; TfEditCookie cookie = 0;
            Harness::require(ime.manager->CreateDocumentMgr(&next_document), "create next input field");
            Harness::require(next_document->CreateContext(ime.client, 0, next_store.Get(), &next_context, &cookie), "create next field context");
            Harness::require(next_document->Push(next_context.Get()), "push next field context");
            Harness::require(ime.manager->SetFocus(next_document.Get()), "focus next input field"); Harness::pump();
            check(label(bar.Get()) == L"中" && ime.store->text == L"hello ", "automatic English leaked into a different input field");
            auto original_context = ime.context; ime.context = next_context;
            ime.type("nh"); ime.key(VK_SPACE);
            check(next_store->text == L"你好" && ime.compositions() == 0, "new input field did not resume Chinese composition");
            ime.context = original_context; original_context.Reset();
            Harness::require(ime.manager->SetFocus(ime.document.Get()), "restore original input field"); Harness::pump();
            next_document->Pop(TF_POPF_ALL); next_context.Reset(); next_document.Reset(); next_store.Reset();
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "English field transition leaked references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime); ime.type("hello"); ime.key(VK_SPACE);
            check(!ime.modified_key(VK_RETURN, {VK_CONTROL}) && label(bar.Get()) == L"中", "Ctrl+Enter retained automatic English");
            ime.type("hello"); ime.key(VK_ESCAPE);
            check(label(bar.Get()) == L"中", "cancelled English entered automatic mode");
            ime.type("hello"); ime.key(VK_DOWN); ime.key(VK_UP); ime.key(VK_SPACE);
            check(label(bar.Get()) == L"中", "explicit candidate navigation was overridden by English detection");
            ime.key(VK_ESCAPE); bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "explicit Chinese selection leaked references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime); ime.type("hello"); ime.key(VK_SPACE);
            check(SUCCEEDED(bar->OnMenuSelect(static_cast<UINT>(BarCommand::automatic_english))) &&
                !read_ime_settings(path).automatic_english && label(bar.Get()) == L"中",
                "language bar opt-out did not persist or exit automatic English");
            ime.type("hello"); ime.key(VK_SPACE);
            check(label(bar.Get()) == L"中", "disabled automatic English still changed mode");
            ime.key(VK_ESCAPE); bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "English setting leaked references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime); ime.type("hello"); ime.key(VK_SPACE);
            check(label(bar.Get()) == L"中", "restart lost the automatic English opt-out");
            ime.key(VK_ESCAPE); ime.key(VK_RETURN);
            set_automatic_english(path, true); ime.store->insert_external(L" "); ime.type("hello"); ime.key(VK_SPACE);
            check(label(bar.Get()) == L"英", "active application did not refresh external English settings");
            set_chinese_punctuation(path, false); set_ime_learning(path, false);
            check(!ime.key(VK_RETURN), "English Enter was swallowed with other settings disabled");
            ime.store->insert_external(L"\n"); type_app(ime, "hello, world!");
            check(label(bar.Get()) == L"英", "punctuation/learning opt-out disabled English recognition");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "external English settings leaked references");
            set_chinese_punctuation(path, true); set_ime_learning(path, true);
        }
        for (const auto scope : {IS_PASSWORD, IS_NUMERIC_PIN}) {
            Harness ime(argv[1], scope); auto bar = button(ime); const auto previous = words(path);
            type_app(ime, "Hello world");
            check(ime.store->text == L"Hello world" && ime.compositions() == 0 && label(bar.Get()) == L"中" && words(path) == previous,
                "automatic English entered password/PIN composition or learning");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "password English leaked references");
        }
        for (const auto scope : {IS_URL, IS_EMAIL_SMTPEMAILADDRESS}) {
            Harness ime(argv[1], scope); auto bar = button(ime); type_app(ime, "hello,");
            check(label(bar.Get()) == L"中" && ime.store->text == L"hello,", "literal InputScope was overridden by automatic English");
            ime.type("nh"); ime.key(VK_SPACE); check(ime.store->text == L"hello,你好", "literal field lost explicit Chinese selection");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "literal English leaked references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF automatic English checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
