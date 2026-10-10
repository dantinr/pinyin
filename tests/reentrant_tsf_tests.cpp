#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include <iostream>

namespace {
using namespace pinyin::testing;
class UiSink final : public ITfUIElementSink {
    std::atomic<ULONG> refs_{1};
public:
    std::function<void()> begin, update, end;
    unsigned begins = 0, updates = 0, ends = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfUIElementSink) return E_NOINTERFACE;
        *result = static_cast<ITfUIElementSink*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    static void invoke(std::function<void()>& action) {
        auto callback = std::move(action); action = {};
        if (callback) callback();
    }
    HRESULT STDMETHODCALLTYPE BeginUIElement(DWORD, BOOL*) override { ++begins; invoke(begin); return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD) override { ++updates; invoke(update); return S_OK; }
    HRESULT STDMETHODCALLTYPE EndUIElement(DWORD) override { ++ends; invoke(end); return S_OK; }
};
struct Subscription {
    ComPtr<ITfSource> source;
    ComPtr<UiSink> sink;
    DWORD cookie = TF_INVALID_COOKIE;
    explicit Subscription(Harness& ime) {
        sink.Attach(new UiSink);
        Harness::require(ime.manager.As(&source), "get UI source");
        Harness::require(source->AdviseSink(IID_ITfUIElementSink, sink.Get(), &cookie), "advise UI sink");
    }
    ~Subscription() { if (source && cookie != TF_INVALID_COOKIE) source->UnadviseSink(cookie); }
};
unsigned candidate_elements(Harness& ime) {
    ComPtr<ITfUIElementMgr> ui; Harness::require(ime.manager.As(&ui), "get UI manager");
    ComPtr<IEnumTfUIElements> elements; Harness::require(ui->EnumUIElements(&elements), "enumerate UI");
    unsigned count = 0;
    for (;;) {
        ComPtr<ITfUIElement> element; ULONG fetched = 0;
        if (elements->Next(1, &element, &fetched) != S_OK || !fetched) return count;
        GUID id{}; Harness::require(element->GetGUID(&id), "read UI identity");
        if (id == candidate_id) ++count;
    }
}
}

int wmain(int argc, wchar_t* argv[]) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int checks = 0;
    auto check = [&](bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); };
    int result = 0;
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace;
        for (const auto stage : {"begin", "update", "end", "deactivate"}) {
            Harness ime(argv[1]);
            {
                Subscription ui(ime);
                ComPtr<ITfThreadFocusSink> focus; Harness::require(ime.service.As(&focus), "get focus sink");
                const auto lose_focus = [&] { Harness::require(focus->OnKillThreadFocus(), "lose focus during UI callback"); };
                std::cout << "Testing synchronous UI callback: " << stage << std::endl;
                const std::string trigger(stage);
                if (trigger == "begin") {
                    ui.sink->begin = lose_focus; ime.key('N');
                } else if (trigger == "update") {
                    ime.key('N'); ui.sink->update = lose_focus; ime.key('I');
                    check(ui.sink->updates == 1, "application did not receive candidate update");
                } else if (trigger == "end") {
                    ime.key('N'); ui.sink->end = lose_focus; ime.key(VK_ESCAPE);
                } else {
                    ui.sink->begin = [&] { Harness::require(ime.service->Deactivate(), "deactivate during candidate creation"); };
                    ime.key('N');
                }
                const std::wstring raw = trigger == "update" ? L"ni" : trigger == "end" ? L"" : L"n";
                check(ui.sink->begins == 1, "application did not receive candidate creation");
                check(ime.compositions() == 0 && ime.store->text == raw, "reentrant callback did not end the preedit");
                check(ui.sink->ends == 1, "candidate registration was not ended exactly once");
                check(candidate_elements(ime) == 0, "reentrant callback left an orphaned TSF candidate element");
                if (trigger != "deactivate") {
                    Harness::require(focus->OnSetThreadFocus(), "regain focus");
                    ime.type("nihao"); ime.key(VK_SPACE);
                    check(ime.store->text == raw + L"你好", "typing did not recover after reentrant focus loss");
                    check(ui.sink->begins == 2 && ui.sink->ends == 2 && candidate_elements(ime) == 0,
                        "later composition did not clean up its UI registration");
                }
            }
            ime.close(); check(ime.unload_result == S_OK, "reentrant callbacks leaked DLL references");
        }
        std::cout << checks << " reentrant TSF checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
