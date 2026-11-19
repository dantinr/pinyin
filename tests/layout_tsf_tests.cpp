#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/learning_dictionary.hpp"
#include <iostream>

namespace {
using namespace pinyin::testing;
struct Window {
    HWND handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"IME layout test",
        WS_POPUP | WS_VISIBLE, 24, 24, 640, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Window() { if (!handle) throw std::runtime_error("cannot create layout test window"); }
    ~Window() { DestroyWindow(handle); }
};
struct Popups { unsigned count = 0; HWND window = nullptr; };
BOOL CALLBACK collect(HWND window, LPARAM parameter) {
    wchar_t name[80]{}; GetClassNameW(window, name, 80);
    if (std::wstring(name) == L"PrivatePinyin.Candidate.4ea569f1" && IsWindowVisible(window)) {
        auto& found = *reinterpret_cast<Popups*>(parameter); ++found.count; found.window = window;
    }
    return TRUE;
}
Popups popups() {
    Popups found; EnumThreadWindows(GetCurrentThreadId(), collect, reinterpret_cast<LPARAM>(&found)); return found;
}
ComPtr<ITfCandidateListUIElement> candidate_element(Harness& ime) {
    ComPtr<ITfUIElementMgr> ui; Harness::require(ime.manager.As(&ui), "get UI manager");
    ComPtr<IEnumTfUIElements> elements; Harness::require(ui->EnumUIElements(&elements), "enumerate UI");
    for (;;) {
        ComPtr<ITfUIElement> element; ULONG fetched = 0;
        if (elements->Next(1, &element, &fetched) != S_OK || !fetched) throw std::runtime_error("missing candidate element");
        GUID id{}; element->GetGUID(&id);
        if (id == candidate_id) {
            ComPtr<ITfCandidateListUIElement> candidates; Harness::require(element.As(&candidates), "get candidate list");
            return candidates;
        }
    }
}
void layout(Harness& ime) {
    ComPtr<ITfTextLayoutSink> sink; Harness::require(ime.service.As(&sink), "get layout sink");
    Harness::require(sink->OnLayoutChange(ime.context.Get(), TF_LC_CHANGE, nullptr), "notify layout");
    Harness::pump();
}
}
int wmain(int argc, wchar_t* argv[]) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int checks = 0, result = 0;
    const auto check = [&](bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); };
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace;
        pinyin::set_ime_learning(pinyin::default_user_path(), false);
        for (const auto mode : {"clipped", "no-layout", "empty", "selection-only", "client-fallback", "native-caret"}) {
            std::cout << "Testing candidate layout: " << mode << std::endl;
            Window owner; Harness ime(argv[1]); ime.store->window = owner.handle;
            const std::string scenario(mode);
            if (scenario == "clipped") ime.store->clipped_text = true;
            if (scenario == "empty") ime.store->empty_text_extent = true;
            if (scenario == "selection-only") ime.store->selection_extent_only = true;
            if (scenario == "no-layout" || scenario == "client-fallback" || scenario == "native-caret")
                ime.store->text_extent_result = TS_E_NOLAYOUT;
            if (scenario == "client-fallback" || scenario == "native-caret") ime.store->screen_extent_result = E_NOTIMPL;
            if (scenario == "native-caret") {
                check(!!CreateCaret(owner.handle, nullptr, 2, 28) && !!SetCaretPos(80, 50), "cannot set up native caret");
            }
            ime.type("nihao");
            check(ime.store->text == L"nihao" && ime.compositions() == 1, "layout failure changed the composition");
            const auto popup = popups();
            check(popup.count == 1, "missing candidate popup for a visible editor with partial/unavailable layout");
            RECT bounds{}; GetWindowRect(popup.window, &bounds);
            POINT origin{20, 84}; ClientToScreen(owner.handle, &origin);
            if (scenario == "clipped")
                check(bounds.left == origin.x && bounds.top == origin.y + 28 + 3, "clipped text lost its usable rectangle");
            if (scenario == "selection-only")
                check(bounds.left == origin.x + 5 * 12, "selection extent was not used after full-range layout failed");
            if (scenario == "native-caret") {
                POINT caret{80, 50}; ClientToScreen(owner.handle, &caret);
                check(bounds.left == caret.x && bounds.top == caret.y + 28 + 3, "native caret fallback was not used");
                DestroyCaret();
            }
            auto element = candidate_element(ime);
            UINT count = 0; element->GetCount(&count);
            check(count > 0, "fallback lost the candidate list");
            Harness::require(element->Show(FALSE), "hide candidate UI"); layout(ime);
            check(popups().count == 0, "fallback ignored application UI suppression");
            Harness::require(element->Show(TRUE), "restore candidate UI"); Harness::pump();
            check(popups().count == 1, "fallback did not restore candidate UI");
            ShowWindow(owner.handle, SW_HIDE); layout(ime);
            check(popups().count == 0, "hidden editor retained a fallback popup");
            ShowWindow(owner.handle, SW_SHOWNOACTIVATE);
            ime.store->text_extent_result = S_OK; ime.store->empty_text_extent = false;
            ime.store->selection_extent_only = false; layout(ime);
            const auto restored = popups(); check(restored.count == 1, "layout recovery lost the popup");
            GetWindowRect(restored.window, &bounds);
            check(bounds.left == origin.x && bounds.top == origin.y + 28 + 3, "layout recovery retained the fallback position");
            check(ime.key(VK_SPACE) && ime.store->text == L"你好" && ime.compositions() == 0 && popups().count == 0,
                "candidate commit failed or left a fallback popup");
            element.Reset(); DestroyWindow(owner.handle); owner.handle = nullptr; Harness::pump();
            ime.close(); check(ime.unload_result == S_OK, "layout fallback leaked DLL references");
        }
        {
            Window owner; Harness ime(argv[1]); ime.store->window = owner.handle;
            ime.store->text_extent_result = TS_E_NOLAYOUT;
            ComPtr<ITfThreadFocusSink> focus; Harness::require(ime.service.As(&focus), "get focus sink");
            ime.store->querying_extent = [&] { Harness::require(focus->OnKillThreadFocus(), "lose focus during extent query"); };
            ime.type("n");
            check(popups().count == 0 && ime.compositions() == 0, "reentrant extent query resurrected a fallback popup");
            focus.Reset(); DestroyWindow(owner.handle); owner.handle = nullptr; Harness::pump();
            ime.close(); check(ime.unload_result == S_OK, "reentrant layout query leaked DLL references");
        }
        std::cout << "PASS: " << checks << " candidate layout checks\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (SUCCEEDED(initialized)) CoUninitialize(); return result;
}
