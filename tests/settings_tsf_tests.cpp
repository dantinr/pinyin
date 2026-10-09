#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "language_bar.hpp"
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
std::wstring label(ITfLangBarItemButton* bar, bool tooltip = false) {
    BSTR value = nullptr; const auto hr = tooltip ? bar->GetTooltipString(&value) : bar->GetText(&value);
    std::wstring result = value ? value : L""; SysFreeString(value);
    check(SUCCEEDED(hr), "cannot read mode label"); return result;
}
pinyin::ime::ComPtr<ITfLangBarItemButton> button(pinyin::testing::Harness& ime) {
    pinyin::ime::ComPtr<ITfLangBarItemMgr> manager; pinyin::ime::ComPtr<ITfLangBarItem> item;
    pinyin::testing::Harness::require(ime.manager.As(&manager), "get language bar manager");
    pinyin::testing::Harness::require(manager->GetItem(GUID_LBI_INPUTMODE, &item), "get registered input-mode button");
    pinyin::ime::ComPtr<ITfLangBarItemButton> result;
    pinyin::testing::Harness::require(item.As(&result), "get input-mode button"); return result;
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin; using namespace pinyin::ime; using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace; const auto path = default_user_path();
        {
            Harness ime(argv[1]); auto bar = button(ime);
            TF_LANGBARITEMINFO info{}; bar->GetInfo(&info);
            check(info.guidItem == GUID_LBI_INPUTMODE && info.clsidService == service_id, "wrong mode item identity");
            check(label(bar.Get()) == L"中", "activation did not show Chinese mode");
            HICON icon = nullptr; check(SUCCEEDED(bar->GetIcon(&icon)) && icon, "Chinese mode icon missing"); DestroyIcon(icon);
            check(SUCCEEDED(bar->OnClick(TF_LBI_CLK_LEFT, {}, nullptr)) && label(bar.Get()) == L"英" && !ime.key('N'),
                "mode click did not change label and keyboard handling");
            ime.key(VK_SHIFT); ime.key_up(VK_SHIFT);
            check(label(bar.Get()) == L"中", "Shift did not refresh mode label");
            check(SUCCEEDED(bar->OnMenuSelect(static_cast<UINT>(BarCommand::learning))) && !ime_learning_enabled(path) &&
                label(bar.Get(), true).find(L"无痕") != std::wstring::npos, "learning menu did not match persistent setting");
            check(SUCCEEDED(bar->OnMenuSelect(static_cast<UINT>(BarCommand::punctuation))) && !read_ime_settings(path).chinese_punctuation,
                "punctuation menu did not persist");
            check(!ime.key(VK_OEM_COMMA), "disabled punctuation intercepted idle English comma");
            ime.type("nh"); ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"你好," && ime.compositions() == 0 && !std::filesystem::exists(path),
                "English punctuation lost candidate confirmation or learned while off");
            // Keep a stale TSF client reference across deactivation. It must no
            // longer access the service or launch commands.
            ime.service->Deactivate(); DWORD status = 0; bar->GetStatus(&status);
            check((status & TF_LBI_STATUS_DISABLED) && bar->OnMenuSelect(static_cast<UINT>(BarCommand::mode)) == S_FALSE,
                "deactivated button retained live service callbacks");
            ComPtr<ITfLangBarItemMgr> manager; ime.manager.As(&manager); ComPtr<ITfLangBarItem> removed;
            manager->GetItem(GUID_LBI_INPUTMODE, &removed);
            check(!removed, "deactivation left a language bar item installed");
            removed.Reset(); manager.Reset(); bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "mode item leaked DLL references");
        }
        {
            Harness ime(argv[1]); auto bar = button(ime);
            check(!ime.key(VK_OEM_PERIOD) && label(bar.Get(), true).find(L"无痕") != std::wstring::npos,
                "restart lost saved settings");
            // Simulate another application's settings window while this DLL is
            // already active: refresh before the next keystroke.
            set_chinese_punctuation(path, true); set_ime_learning(path, true);
            ime.type("bj"); ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"北京，", "running application did not see external punctuation change");
            { UserStore store(path); check(store.load().count({"bei jing", "北京"}) == 1, "running application did not see external learning change"); }
            { UserStore store(path); auto words = store.load(); words[{"ni hao", "专属测试词"}] = 9; store.save(words); }
            ime.type("nh");
            set_ime_learning(path, false); ime.key(VK_SPACE);
            check(ime.store->text == L"北京，你好", "turning learning off retained a personal-only pending candidate");
            { UserStore store(path); check(store.load().at({"ni hao", "专属测试词"}) == 9, "disabled learning modified personal words"); }
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "external settings refresh leaked DLL references");
        }
        {
            Harness ime(argv[1], IS_PASSWORD); auto bar = button(ime);
            check(!ime.key('N') && !ime.key(VK_OEM_COMMA) && ime.store->text.empty(), "mode UI bypassed password direct input");
            bar.Reset(); ime.close(); check(ime.unload_result == S_OK, "password mode item leaked references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " TSF mode/settings checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
