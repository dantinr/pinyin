#include "tsf_text_store.hpp"
#include <memory>

using namespace pinyin::testing;
namespace {
std::unique_ptr<Harness> demo;
HFONT font = nullptr;
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    try {
        if (message == WM_SETFOCUS && demo) PostMessageW(window, WM_APP, 0, 0);
        if (message == WM_APP && demo) {
            demo->activate();
            SetWindowTextW(window, L"Private Pinyin — TSF 开发测试");
            return 0;
        }
        if (message == WM_CHAR && demo) {
            if (w >= 0x20 && w != 0x7f) demo->store->insert_external(std::wstring(1, static_cast<wchar_t>(w)));
            return 0;
        }
        if (message == WM_KILLFOCUS && demo) demo->keys->OnSetFocus(FALSE);
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
            auto previous = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
            RECT area{}; GetClientRect(window, &area); FillRect(dc, &area, GetSysColorBrush(COLOR_WINDOW));
            RECT caption{20, 16, area.right - 20, 76};
            DrawTextW(dc, L"TSF 开发测试窗口 · 输入 nihao 后按空格\n数字选词 / 退格 / Esc 取消 / Enter 原文 / Shift 中英切换", -1, &caption, DT_NOPREFIX);
            RECT document{20, 84, area.right - 20, area.bottom - 20};
            if (demo) DrawTextW(dc, demo->store->text.data(), static_cast<int>(demo->store->text.size()), &document, DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, previous); EndPaint(window, &paint); return 0;
        }
        if (message == WM_LBUTTONDOWN) { SetFocus(window); return 0; }
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    } catch (const std::exception& error) {
        const auto detail = wide(error.what()); SetWindowTextW(window, detail.c_str()); return 0;
    } catch (...) { SetWindowTextW(window, L"Private Pinyin — 测试宿主错误"); return 0; }
    return DefWindowProcW(window, message, w, l);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
        demo = std::make_unique<Harness>(std::filesystem::path(path).parent_path() / L"private_pinyin_ime.dll");
        WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = procedure;
        cls.lpszClassName = L"PrivatePinyin.TsfDemo"; cls.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        RegisterClassW(&cls);
        font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HWND window = CreateWindowW(cls.lpszClassName, L"Private Pinyin — TSF 开发测试", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 820, 460, nullptr, nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("cannot create test window");
        demo->store->window = window;
        demo->store->changed = [window] { InvalidateRect(window, nullptr, TRUE); };
        // Associate only this test window; no COM/TSF system registration is changed.
        ComPtr<ITfDocumentMgr> previous;
        demo->manager->AssociateFocus(window, demo->document.Get(), &previous);
        ShowWindow(window, show); UpdateWindow(window); SetFocus(window);
        demo->activate();
        // TSF must obtain the messages before the system IME's legacy message filter.
        ComPtr<ITfMessagePump> messages;
        Harness::require(demo->manager.As(&messages), "get TSF message pump");
        MSG message{};
        BOOL result = FALSE;
        while (SUCCEEDED(messages->GetMessageW(&message, nullptr, 0, 0, &result)) && result > 0) {
            // Avoid TranslateMessage generating WM_CHAR for keys already consumed by the IME.
            if (message.message == WM_KEYDOWN && message.hwnd == window && demo->key(message.wParam, false)) continue;
            if (message.message == WM_KEYUP && message.hwnd == window && demo->key_up(message.wParam, false)) continue;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        demo.reset(); DeleteObject(font); CoUninitialize(); return 0;
    } catch (...) {
        demo.reset(); if (font) DeleteObject(font); CoUninitialize(); return 1;
    }
}
