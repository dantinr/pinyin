#pragma once
#include <initguid.h>
#include "common.hpp"
#include <textstor.h>
#include <olectl.h>
#include <algorithm>
#include <functional>
#include <vector>

namespace pinyin::testing {
using namespace pinyin::ime;

class ScopeAttribute final : public ITfInputScope {
    std::atomic<ULONG> refs_{1};
    InputScope scope_;
public:
    explicit ScopeAttribute(InputScope scope) : scope_(scope) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(ITfInputScope)) return E_NOINTERFACE;
        *result = static_cast<ITfInputScope*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetInputScopes(InputScope** scopes, UINT* count) override {
        if (!scopes || !count) return E_POINTER; *scopes = nullptr; *count = 0;
        auto value = static_cast<InputScope*>(CoTaskMemAlloc(sizeof(InputScope)));
        if (!value) return E_OUTOFMEMORY;
        *value = scope_; *scopes = value; *count = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPhrase(BSTR** value, UINT* count) override {
        if (value) *value = nullptr; if (count) *count = 0; return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetRegularExpression(BSTR* value) override { if (value) *value = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSRGS(BSTR* value) override { if (value) *value = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetXML(BSTR* value) override { if (value) *value = nullptr; return E_NOTIMPL; }
};

// A small real TSF text store, used only by integration tests and the developer test window.
// TSF supplies the context/ranges/composition implementation; none of those are mocked.
class TextStore final : public ITextStoreACP {
    std::atomic<ULONG> refs_{1};
    ComPtr<ITextStoreACPSink> sink_;
    DWORD lock_ = 0;
    std::vector<DWORD> queued_;
    bool scope_requested_ = false;
    TS_SELECTION_ACP selection_{0, 0, {TS_AE_NONE, FALSE}};
    bool valid(LONG start, LONG end) const { return start >= 0 && end >= start && end <= static_cast<LONG>(text.size()); }
    bool reading() const { return (lock_ & TS_LF_READ) != 0; }
    bool writing() const { return (lock_ & TS_LF_READWRITE) == TS_LF_READWRITE; }
public:
    std::wstring text;
    HWND window = nullptr;
    bool readonly = false;
    InputScope scope = IS_DEFAULT;
    std::function<void()> changed;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITextStoreACP) return E_NOINTERFACE;
        *result = static_cast<ITextStoreACP*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid, IUnknown* object, DWORD) override {
        if (iid != IID_ITextStoreACPSink || !object) return E_INVALIDARG;
        if (sink_) return CONNECT_E_ADVISELIMIT;
        return object->QueryInterface(IID_PPV_ARGS(&sink_));
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown* object) override {
        if (!object || !sink_) return CONNECT_E_NOCONNECTION;
        ComPtr<IUnknown> a, b;
        object->QueryInterface(IID_PPV_ARGS(&a)); sink_.As(&b);
        if (a.Get() != b.Get()) return CONNECT_E_NOCONNECTION;
        sink_.Reset(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLock(DWORD flags, HRESULT* session) override {
        if (!session) return E_POINTER;
        if (!sink_) { *session = E_UNEXPECTED; return E_UNEXPECTED; }
        if (lock_) {
            if (flags & TS_LF_SYNC) { *session = TS_E_SYNCHRONOUS; return S_OK; }
            queued_.push_back(flags); *session = TS_S_ASYNC; return S_OK;
        }
        lock_ = flags; *session = sink_->OnLockGranted(flags); lock_ = 0;
        while (!queued_.empty()) {
            const auto next = queued_.front(); queued_.erase(queued_.begin());
            lock_ = next; sink_->OnLockGranted(next); lock_ = 0;
        }
        if (changed) changed(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* status) override {
        if (!status) return E_POINTER; *status = {readonly ? static_cast<DWORD>(TS_SD_READONLY) : 0UL, 0}; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInsert(LONG start, LONG end, ULONG, LONG* first, LONG* last) override {
        if (!first || !last) return E_POINTER; if (!valid(start, end)) return TS_E_INVALIDPOS;
        *first = start; *last = end; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* result, ULONG* fetched) override {
        if (!result || !fetched) return E_POINTER; *fetched = 0;
        if (!reading()) return TS_E_NOLOCK;
        if (!count) return S_OK;
        if (index != 0 && index != TS_DEFAULT_SELECTION) return E_INVALIDARG;
        *result = selection_; *fetched = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override {
        if (!selection) return E_POINTER;
        if (!writing()) return TS_E_NOLOCK;
        if (count != 1 || !valid(selection->acpStart, selection->acpEnd)) return E_INVALIDARG;
        selection_ = *selection; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetText(LONG start, LONG end, WCHAR* plain, ULONG requested, ULONG* returned,
        TS_RUNINFO* runs, ULONG runs_requested, ULONG* runs_returned, LONG* next) override {
        if (!returned || !runs_returned || !next || (requested && !plain) || (runs_requested && !runs)) return E_POINTER;
        *returned = 0; *runs_returned = 0; *next = start;
        if (!reading()) return TS_E_NOLOCK;
        if (end == -1) end = static_cast<LONG>(text.size());
        if (!valid(start, end)) return TS_E_INVALIDPOS;
        const auto available = static_cast<ULONG>(end - start);
        const auto count = requested ? std::min(available, requested) : available;
        if (requested) { std::copy_n(text.data() + start, count, plain); *returned = count; }
        if (runs_requested && count) { *runs = {count, TS_RT_PLAIN}; *runs_returned = 1; }
        *next = start + count; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetText(DWORD, LONG start, LONG end, const WCHAR* value, ULONG count, TS_TEXTCHANGE* change) override {
        if (!change || (count && !value)) return E_POINTER;
        if (!writing()) return TS_E_NOLOCK;
        if (readonly) return TS_E_READONLY;
        if (!valid(start, end)) return TS_E_INVALIDPOS;
        text.replace(start, end - start, value ? value : L"", count);
        *change = {start, end, start + static_cast<LONG>(count)};
        selection_.acpStart = selection_.acpEnd = change->acpNewEnd;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD flags, const WCHAR* value, ULONG count, LONG* start, LONG* end, TS_TEXTCHANGE* change) override {
        if (!reading()) return TS_E_NOLOCK;
        if (start) *start = selection_.acpStart;
        if (end) *end = selection_.acpEnd;
        if (flags & TS_IAS_QUERYONLY) return S_OK;
        TS_TEXTCHANGE local{};
        auto hr = SetText(0, selection_.acpStart, selection_.acpEnd, value, count, change ? change : &local);
        if (SUCCEEDED(hr) && end) *end = selection_.acpEnd;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFormattedText(LONG, LONG, IDataObject** result) override { if (result) *result = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEmbedded(LONG, REFGUID, REFIID, IUnknown** result) override { if (result) *result = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* result) override {
        if (!result) return E_POINTER; *result = FALSE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD, ULONG count, const TS_ATTRID* attrs) override {
        if (count && !attrs) return E_POINTER;
        scope_requested_ = count == 0;
        for (ULONG i = 0; i < count; ++i) if (attrs[i] == GUID_PROP_INPUTSCOPE) scope_requested_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG, ULONG count, const TS_ATTRID* attrs, DWORD flags) override {
        return RequestSupportedAttrs(flags, count, attrs);
    }
    HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD,
        LONG* next, BOOL* found, LONG* offset) override {
        if (!next || !found || !offset) return E_POINTER;
        *next = halt; *found = FALSE; *offset = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG count, TS_ATTRVAL* attrs, ULONG* fetched) override {
        if (!fetched || (count && !attrs)) return E_POINTER; *fetched = 0;
        if (scope_requested_ && count) {
            auto value = new (std::nothrow) ScopeAttribute(scope); if (!value) return E_OUTOFMEMORY;
            attrs[0].idAttr = GUID_PROP_INPUTSCOPE; attrs[0].dwOverlapId = 0;
            VariantInit(&attrs[0].varValue); attrs[0].varValue.vt = VT_UNKNOWN; attrs[0].varValue.punkVal = value;
            *fetched = 1; scope_requested_ = false;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEndACP(LONG* result) override {
        if (!result) return E_POINTER; if (!reading()) return TS_E_NOLOCK;
        *result = static_cast<LONG>(text.size()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* result) override { if (!result) return E_POINTER; *result = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie, LONG start, LONG end, RECT* result, BOOL* clipped) override {
        if (!result || !clipped) return E_POINTER;
        if (!valid(start, end)) return TS_E_INVALIDPOS;
        if (!window || !IsWindowVisible(window)) return TS_E_NOLAYOUT;
        RECT area{}; GetClientRect(window, &area);
        POINT origin{20, 84}; ClientToScreen(window, &origin);
        *result = {origin.x + start * 12, origin.y, origin.x + std::max(start + 1, end) * 12, origin.y + 28};
        *clipped = FALSE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie, RECT* result) override {
        if (!result) return E_POINTER; if (!window) return TS_E_NOLAYOUT;
        GetWindowRect(window, result); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie, HWND* result) override { if (!result) return E_POINTER; *result = window; return S_OK; }
    void insert_external(const std::wstring& value) {
        if (lock_) throw std::runtime_error("external edit during TSF lock");
        TS_TEXTCHANGE change{selection_.acpStart, selection_.acpEnd, selection_.acpStart + static_cast<LONG>(value.size())};
        text.replace(selection_.acpStart, selection_.acpEnd - selection_.acpStart, value);
        selection_.acpStart = selection_.acpEnd = change.acpNewEnd;
        if (sink_) { sink_->OnTextChange(0, &change); sink_->OnSelectionChange(); }
        if (changed) changed();
    }
};

class CapturingFactory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};
    ComPtr<IClassFactory> factory_;
public:
    ComPtr<ITfTextInputProcessorEx> last;
    explicit CapturingFactory(IClassFactory* factory) : factory_(factory) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        const auto hr = factory_->CreateInstance(outer, iid, result);
        if (SUCCEEDED(hr) && result && *result) {
            ComPtr<ITfTextInputProcessorEx> service;
            static_cast<IUnknown*>(*result)->QueryInterface(IID_PPV_ARGS(&service));
            if (service) last = std::move(service);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { return factory_->LockServer(lock); }
};

class Harness {
    HMODULE dll_ = nullptr;
    bool active_ = false;
    DWORD class_cookie_ = 0;
    bool profile_registered_ = false;
    LANGID local_language_ = 0;
    CLSID local_service_{};
    GUID local_profile_{};
    ComPtr<CapturingFactory> factory_;
    ComPtr<ITfInputProcessorProfileMgr> profiles_;
public:
    ComPtr<TextStore> store;
    ComPtr<ITfThreadMgr> manager;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
    ComPtr<ITfTextInputProcessorEx> service;
    ComPtr<ITfKeyEventSink> keys;
    TfClientId client = 0;
    HRESULT unload_result = E_UNEXPECTED;
    explicit Harness(const std::filesystem::path& dll_path, InputScope scope = IS_DEFAULT) {
        try {
            dll_ = LoadLibraryW(dll_path.c_str()); if (!dll_) throw std::runtime_error("cannot load IME DLL");
            const auto get = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(GetProcAddress(dll_, "DllGetClassObject"));
            if (!get) throw std::runtime_error("missing class factory export");
            ComPtr<IClassFactory> factory;
            require(get(service_id, IID_PPV_ARGS(&factory)), "get class factory");
            factory_.Attach(new CapturingFactory(factory.Get()));
            // A system-installed service with the production CLSID can bypass
            // our capturing factory. Alias the same DLL factory only in this process.
            require(CoCreateGuid(&local_service_), "create local service identity");
            require(CoCreateGuid(&local_profile_), "create local profile identity");
            require(CoRegisterClassObject(local_service_, factory_.Get(), CLSCTX_INPROC_SERVER,
                REGCLS_MULTIPLEUSE, &class_cookie_), "register process-local class factory");
            require(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles_)), "create profile manager");
            ComPtr<ITfInputProcessorProfiles> legacy; require(profiles_.As(&legacy), "get language manager");
            require(legacy->GetCurrentLanguage(&local_language_), "get current language");
            require(profiles_->RegisterProfile(local_service_, local_language_, local_profile_, description,
                static_cast<ULONG>(std::size(description) - 1), nullptr, 0, 0, nullptr, 0, TRUE, TF_RP_LOCALPROCESS),
                "register process-local profile");
            profile_registered_ = true;
            require(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager)), "create TSF manager");
            require(manager->Activate(&client), "activate TSF manager"); active_ = true;
            store.Attach(new TextStore);
            store->scope = scope;
            require(manager->CreateDocumentMgr(&document), "create document");
            TfEditCookie cookie = 0;
            require(document->CreateContext(client, 0, store.Get(), &context, &cookie), "create context");
            require(document->Push(context.Get()), "push context");
            require(manager->SetFocus(document.Get()), "focus document");
            // Drain initial focus/profile notifications before selecting the test
            // profile; a queued system-profile activation can otherwise replace it.
            pump();
            activate();
        } catch (...) { close(); throw; }
    }
    static void require(HRESULT hr, const char* action) {
        if (FAILED(hr)) throw std::runtime_error(std::string(action) + " failed, HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
    }
    void activate() {
        require(profiles_->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, local_language_, local_service_,
            local_profile_, nullptr, TF_IPPMF_FORPROCESS), "activate process-local profile");
        service = factory_->last;
        if (!service) throw std::runtime_error("TSF did not instantiate the process-local service");
        require(service.As(&keys), "get active key interface");
    }
    bool key(WPARAM value, bool drain_messages = true) {
        struct KeyboardState {
            BYTE original[256]{};
            bool isolated = false;
            explicit KeyboardState(bool synthetic) {
                if (!synthetic) return;
                BYTE neutral[256]{};
                isolated = !!GetKeyboardState(original);
                if (!isolated || !SetKeyboardState(neutral)) throw std::runtime_error("cannot isolate synthetic keyboard state");
            }
            ~KeyboardState() { if (isolated) SetKeyboardState(original); }
        };
        BOOL eaten = FALSE;
        {
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setkeyboardstate
            // SetKeyboardState changes only the calling thread. The interactive
            // demo passes false and continues to use its actual modifier state.
            KeyboardState keyboard(drain_messages);
            require(keys->OnTestKeyDown(context.Get(), value, 0, &eaten), "test key");
            if (eaten) require(keys->OnKeyDown(context.Get(), value, 0, &eaten), "handle key");
        }
        if (drain_messages) pump(); return !!eaten;
    }
    bool key_up(WPARAM value, bool drain_messages = true) {
        BOOL eaten = FALSE; require(keys->OnTestKeyUp(context.Get(), value, 0, &eaten), "test key up");
        if (eaten) require(keys->OnKeyUp(context.Get(), value, 0, &eaten), "handle key up");
        if (drain_messages) pump(); return !!eaten;
    }
    static void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    void type(const char* value) {
        const std::string query(value);
        while (*value) {
            const auto letter = *value++;
            const auto virtual_key = letter == '\'' ? static_cast<WPARAM>(VK_OEM_7) :
                static_cast<WPARAM>(static_cast<unsigned char>(letter) - 'a' + 'A');
            if (!key(virtual_key))
                throw std::runtime_error("letter was not handled: " + query + ", key=" + letter +
                    ", CapsLock=" + std::to_string(GetKeyState(VK_CAPITAL) & 1));
        }
    }
    std::size_t compositions() const {
        ComPtr<ITfContextComposition> composer; require(context.As(&composer), "get composer");
        ComPtr<IEnumITfCompositionView> enumeration; require(composer->EnumCompositions(&enumeration), "enumerate compositions");
        ULONG fetched = 0; std::size_t count = 0;
        do { ComPtr<ITfCompositionView> view; fetched = 0; enumeration->Next(1, &view, &fetched); count += fetched; } while (fetched);
        return count;
    }
    void close() noexcept {
        if (profiles_ && profile_registered_) profiles_->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,
            local_language_, local_service_, local_profile_, nullptr, TF_IPPMF_FORPROCESS);
        if (service) service->Deactivate(); pump(); keys.Reset(); service.Reset();
        if (document) document->Pop(TF_POPF_ALL);
        context.Reset(); document.Reset(); store.Reset();
        if (manager && active_) manager->Deactivate(); active_ = false; manager.Reset();
        if (profiles_ && profile_registered_) profiles_->UnregisterProfile(local_service_, local_language_, local_profile_, TF_URP_LOCALPROCESS);
        profile_registered_ = false; profiles_.Reset();
        if (class_cookie_) CoRevokeClassObject(class_cookie_); class_cookie_ = 0; factory_.Reset();
        if (dll_) {
            const auto can_unload = reinterpret_cast<HRESULT(__stdcall*)()>(GetProcAddress(dll_, "DllCanUnloadNow"));
            unload_result = can_unload ? can_unload() : E_FAIL;
            // Never unload code still referenced by TSF; tests explicitly check this result.
            if (unload_result == S_OK) FreeLibrary(dll_);
            dll_ = nullptr;
        }
    }
    ~Harness() { close(); }
};
}
