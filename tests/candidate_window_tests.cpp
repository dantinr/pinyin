#include "candidate_window.hpp"
#include <iostream>

namespace pinyin::ime {
HINSTANCE module = GetModuleHandleW(nullptr);
std::atomic<long> objects{0};
std::atomic<long> server_locks{0};
}
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
struct Owner {
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Private Pinyin lifecycle test",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, pinyin::ime::module, nullptr);
    Owner() { if (!window) throw std::runtime_error("cannot create test owner"); }
    ~Owner() { if (window) DestroyWindow(window); }
};
struct Search { HWND owner, result = nullptr; unsigned count = 0; };
BOOL CALLBACK collect(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<Search*>(parameter);
    wchar_t name[80]{}; GetClassNameW(window, name, 80);
    if (std::wstring(name) == L"PrivatePinyin.Candidate.4ea569f1" && GetWindow(window, GW_OWNER) == search.owner) {
        search.result = window; ++search.count;
    }
    return TRUE;
}
Search candidates(HWND owner) {
    Search search{owner}; EnumThreadWindows(GetCurrentThreadId(), collect, reinterpret_cast<LPARAM>(&search)); return search;
}
}
int main() {
    using namespace pinyin;
    using namespace pinyin::ime;
    try {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Lexicon lexicon;
        for (int i = 0; i < 12; ++i) lexicon.add({"词" + std::to_string(i), "ni hao", static_cast<std::uint64_t>(100 - i)});
        InputSession input; for (auto ch : std::string("nihao")) input.handle(InputKey::letter, ch, lexicon);
        RECT caret{24, 24, 28, 44};
        Owner first, second;
        {
            CandidateWindow popup, other;
            popup.show(input, caret, first.window, [](std::size_t) {}, L"无痕");
            const auto shown = candidates(first.window);
            check(shown.count == 1 && popup.visible(), "candidate popup was not created");
            RECT area{}; GetClientRect(shown.result, &area);
            const auto dpi = GetDpiForWindow(shown.result);
            const int selected_y = MulDiv(10, dpi, 96) + MulDiv(28, dpi, 96) + MulDiv(28, dpi, 96) / 2;
            HDC dc = GetDC(shown.result);
            const auto pixel = GetPixel(dc, area.right / 2, selected_y); ReleaseDC(shown.result, dc);
            check(pixel == GetSysColor(COLOR_HIGHLIGHT), "first paint did not render the selected row");
            popup.hide();
            check(!popup.visible() && !IsWindow(shown.result) && candidates(first.window).count == 0,
                "ending input retained a native candidate window");
            popup.hide(); check(candidates(first.window).count == 0, "repeated hide recreated a window");
            popup.show(input, caret, first.window, [](std::size_t) {}, L"无痕");
            other.show(input, caret, second.window, [](std::size_t) {}, L"无痕");
            popup.hide();
            check(other.visible() && candidates(second.window).count == 1, "closing one popup affected a different service");
            other.hide();
            popup.show(input, caret, first.window, [](std::size_t) {}, L"无痕");
            const auto owned = candidates(first.window).result;
            DestroyWindow(first.window); first.window = nullptr;
            check(!IsWindow(owned) && !popup.visible(), "owner destruction left a stale candidate HWND");
            popup.show(input, caret, second.window, [](std::size_t) {}, L"无痕");
            check(popup.visible() && candidates(second.window).count == 1, "destroyed owner prevented popup recreation");
            InputSession empty;
            popup.show(empty, caret, second.window, [](std::size_t) {}, L"无痕");
            check(candidates(second.window).count == 0, "empty input displayed a blank popup");
            bool clicked = false;
            popup.show(input, caret, second.window, [&](std::size_t index) { clicked = index == 0; popup.hide(); }, L"无痕");
            const auto clicked_window = candidates(second.window).result;
            const auto clicked_dpi = GetDpiForWindow(clicked_window);
            const int clicked_y = MulDiv(10, clicked_dpi, 96) + MulDiv(28, clicked_dpi, 96) + MulDiv(28, clicked_dpi, 96) / 2;
            SendMessageW(clicked_window, WM_LBUTTONDOWN, 0, MAKELPARAM(30, clicked_y));
            check(clicked && !popup.visible() && candidates(second.window).count == 0,
                "mouse commit retained a popup or invalidated its callback");
            for (int i = 0; i < 30; ++i) {
                popup.show(input, caret, second.window, [](std::size_t) {}, L"无痕"); popup.hide();
            }
            check(candidates(second.window).count == 0, "repeated compositions accumulated hidden windows");
        }
        WNDCLASSEXW registered{sizeof(registered)};
        check(!GetClassInfoExW(module, L"PrivatePinyin.Candidate.4ea569f1", &registered), "candidate class was not released");
        std::cout << "PASS: " << checks << " candidate window lifecycle/paint checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
