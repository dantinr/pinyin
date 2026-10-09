#include "language_bar.hpp"
#include <olectl.h>
#include <iostream>

namespace pinyin::ime {
HINSTANCE module = GetModuleHandleW(nullptr);
std::atomic<long> objects{0};
std::atomic<long> server_locks{0};
}
namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
class Sink final : public ITfLangBarItemSink {
    ULONG refs_ = 1;
public:
    DWORD flags = 0; unsigned notifications = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfLangBarItemSink) return E_NOINTERFACE;
        *result = static_cast<ITfLangBarItemSink*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnUpdate(DWORD updated) override { flags |= updated; ++notifications; return S_OK; }
};
}
int main() {
    using namespace pinyin::ime;
    try {
        BarState state; unsigned commands = 0;
        ComPtr<LanguageBar> bar; bar.Attach(new LanguageBar([&] { return state; }, [&](BarCommand command) {
            ++commands; if (command == BarCommand::mode) state.chinese = !state.chinese; return S_OK;
        }));
        ComPtr<Sink> sink; sink.Attach(new Sink); DWORD cookie = TF_INVALID_COOKIE, rejected = 0;
        check(bar->AdviseSink(IID_ITfLangBarItemSink, sink.Get(), &cookie) == S_OK && cookie != TF_INVALID_COOKIE,
            "language bar sink could not subscribe");
        check(bar->AdviseSink(IID_ITfLangBarItemSink, sink.Get(), &rejected) == CONNECT_E_ADVISELIMIT,
            "second sink replaced the subscribed sink");
        check(bar->UnadviseSink(cookie + 1) == CONNECT_E_NOCONNECTION, "wrong cookie disconnected current sink");
        check(bar->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == S_OK && !state.chinese, "left click did not dispatch mode change");
        bar->notify();
        check((sink->flags & TF_LBI_BTNALL) == TF_LBI_BTNALL, "mode change did not notify icon, text and tooltip");
        bar->Show(FALSE); DWORD status = 0; bar->GetStatus(&status);
        check((status & TF_LBI_STATUS_HIDDEN) && (sink->flags & TF_LBI_STATUS), "hide did not notify visibility status");
        bar->Show(TRUE); bar->GetStatus(&status); check(!(status & TF_LBI_STATUS_HIDDEN), "show did not restore visibility");
        // GDI lazily initializes stock objects/font caches on first use.
        for (bool chinese : {false, true}) {
            state.chinese = chinese; HICON icon = nullptr; bar->GetIcon(&icon); if (icon) DestroyIcon(icon);
        }
        const auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 80; ++i) {
            state.chinese = i % 2 == 0; HICON icon = nullptr;
            check(SUCCEEDED(bar->GetIcon(&icon)) && icon, "mode icon creation failed"); DestroyIcon(icon);
        }
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles + 1, "mode icon creation leaked GDI handles");
        check(bar->UnadviseSink(cookie) == S_OK, "valid sink could not unsubscribe");
        const auto count = sink->notifications; bar->notify(); check(sink->notifications == count, "unsubscribed sink was invoked");
        DWORD replacement = 0; bar->AdviseSink(IID_ITfLangBarItemSink, sink.Get(), &replacement);
        check(replacement != cookie && bar->UnadviseSink(cookie) == CONNECT_E_NOCONNECTION, "stale cookie disconnected a new subscription");
        check(bar->OnMenuSelect(999) == E_INVALIDARG, "invalid menu command was dispatched");
        bar->disconnect();
        check(bar->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == S_FALSE && commands == 1, "detached item retained live callbacks");
        bar.Reset(); check(objects == 0, "language bar object prevented DLL unloading");
        std::cout << "PASS: " << checks << " language bar lifecycle checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
