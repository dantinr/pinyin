#include "common.hpp"
#include "candidate_window.hpp"
#include "language_bar.hpp"
#include "../settings_actions.hpp"
#include "pinyin/input_session.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "pinyin/punctuation.hpp"
#include "pinyin/english_input.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace pinyin::ime {
namespace {
bool same_object(IUnknown* a, IUnknown* b) {
    if (!a || !b) return a == b;
    ComPtr<IUnknown> left, right;
    return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&left))) &&
           SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&right))) && left.Get() == right.Get();
}
bool compartment_enabled(IUnknown* object, REFGUID guid) {
    ComPtr<ITfCompartmentMgr> manager; ComPtr<ITfCompartment> compartment;
    if (!object || FAILED(object->QueryInterface(IID_PPV_ARGS(&manager))) ||
        FAILED(manager->GetCompartment(guid, &compartment))) return false;
    VARIANT value; VariantInit(&value);
    const bool enabled = SUCCEEDED(compartment->GetValue(&value)) && value.vt == VT_I4 && value.lVal != 0;
    VariantClear(&value); return enabled;
}
bool input_disabled(ITfContext* context) {
    TF_STATUS status{};
    return !context || FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY) ||
        compartment_enabled(context, GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
        compartment_enabled(context, GUID_COMPARTMENT_EMPTYCONTEXT);
}
enum class FieldKind { normal, sensitive, literal };
FieldKind field_kind(ITfContext* context, TfEditCookie cookie) {
    ComPtr<ITfReadOnlyProperty> property;
    TF_SELECTION selection{}; ULONG fetched = 0;
    if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return FieldKind::sensitive;
    ComPtr<ITfRange> range; range.Attach(selection.range);
    if (FAILED(context->GetAppProperty(GUID_PROP_INPUTSCOPE, &property))) return FieldKind::normal;
    VARIANT value; VariantInit(&value);
    auto kind = FieldKind::normal;
    if (SUCCEEDED(property->GetValue(cookie, range.Get(), &value)) && value.vt == VT_UNKNOWN && value.punkVal) {
        ComPtr<ITfInputScope> scope;
        if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&scope)))) {
            InputScope* scopes = nullptr; UINT count = 0;
            if (SUCCEEDED(scope->GetInputScopes(&scopes, &count))) {
                for (UINT i = 0; i < count; ++i) {
                    if (scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PASSWORD ||
                        scopes[i] == IS_NUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN_SET)
                        kind = FieldKind::sensitive;
                    else if (kind != FieldKind::sensitive &&
                        (scopes[i] == IS_URL || scopes[i] == IS_EMAIL_USERNAME || scopes[i] == IS_EMAIL_SMTPEMAILADDRESS ||
                         scopes[i] == IS_EMAILNAME_OR_ADDRESS || scopes[i] == IS_DIGITS || scopes[i] == IS_NUMBER ||
                         scopes[i] == IS_CURRENCY_AMOUNT || scopes[i] == IS_CURRENCY_AMOUNTANDSYMBOL ||
                         scopes[i] == IS_TIME_FULLTIME || scopes[i] == IS_DATE_FULLDATE)) kind = FieldKind::literal;
                }
                CoTaskMemFree(scopes);
            }
        }
    }
    VariantClear(&value); return kind;
}
bool direct_input(ITfContext* context, TfEditCookie cookie) {
    return field_kind(context, cookie) == FieldKind::sensitive;
}
std::optional<char> punctuation_key(WPARAM key) {
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (key) {
    case VK_OEM_COMMA: return shift ? '<' : ',';
    case VK_OEM_PERIOD: return shift ? '>' : '.';
    case VK_OEM_1: return shift ? ':' : ';';
    case VK_OEM_2: return shift ? '?' : '/';
    case VK_OEM_5: if (!shift) return '\\'; break;
    case VK_OEM_7: return shift ? '"' : '\'';
    case VK_OEM_4: return shift ? '{' : '[';
    case VK_OEM_6: return shift ? '}' : ']';
    case VK_OEM_MINUS: if (shift) return '_'; break;
    case '1': if (shift) return '!'; break;
    case '4': if (shift) return '$'; break;
    case '6': if (shift) return '^'; break;
    case '9': if (shift) return '('; break;
    case '0': if (shift) return ')'; break;
    default: break;
    }
    return {};
}
struct Key {
    InputKey type;
    char value = 0;
};
std::optional<Key> translate(WPARAM key) {
    if (key >= 'A' && key <= 'Z') return Key{InputKey::letter, static_cast<char>(key - 'A' + 'a')};
    if (key >= '0' && key <= '9') return Key{InputKey::digit, static_cast<char>(key)};
    switch (key) {
    case VK_OEM_7: return Key{InputKey::separator};
    case VK_BACK: return Key{InputKey::backspace};
    case VK_DELETE: return Key{InputKey::delete_forward};
    case VK_LEFT: return Key{InputKey::left};
    case VK_RIGHT: return Key{InputKey::right};
    case VK_HOME: return Key{InputKey::home};
    case VK_END: return Key{InputKey::end};
    case VK_UP: return Key{InputKey::previous};
    case VK_DOWN: return Key{InputKey::next};
    case VK_PRIOR: return Key{InputKey::page_previous};
    case VK_NEXT: return Key{InputKey::page_next};
    case VK_SPACE: return Key{InputKey::space};
    case VK_RETURN: return Key{InputKey::enter};
    case VK_ESCAPE: return Key{InputKey::escape};
    default: return {};
    }
}
bool ends_english(WPARAM key) {
    switch (key) {
    case VK_RETURN: case VK_TAB: case VK_ESCAPE:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: return true;
    default: return false;
    }
}

class TextService;
class EditSession final : public ITfEditSession, private ModuleObject {
    std::atomic<ULONG> refs_{1};
    ComPtr<IUnknown> owner_;
    std::function<HRESULT(TfEditCookie)> action_;
public:
    EditSession(IUnknown* owner, std::function<HRESULT(TfEditCookie)> action) : owner_(owner), action_(std::move(action)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession) return E_NOINTERFACE;
        *result = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override { return protect([&] { return action_(cookie); }); }
};

class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink,
    public ITfThreadMgrEventSink, public ITfThreadFocusSink, public ITfCompositionSink, public ITfTextEditSink,
    public ITfTextLayoutSink, public ITfDisplayAttributeProvider, private ModuleObject {
    std::atomic<ULONG> refs_{1};
    ComPtr<ITfThreadMgr> manager_;
    TfClientId client_ = TF_CLIENTID_NULL;
    DWORD thread_cookie_ = TF_INVALID_COOKIE;
    DWORD focus_cookie_ = TF_INVALID_COOKIE;
    DWORD text_cookie_ = TF_INVALID_COOKIE;
    DWORD layout_cookie_ = TF_INVALID_COOKIE;
    ComPtr<ITfContext> observed_;
    ComPtr<ITfContext> composing_;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfUIElementMgr> ui_manager_;
    ComPtr<CandidateElement> element_;
    DWORD element_id_ = TF_INVALID_UIELEMENTID;
    TfGuidAtom attribute_atom_ = TF_INVALID_GUIDATOM;
    std::unique_ptr<LearningDictionary> dictionary_;
    std::filesystem::path user_path_;
    ImeSettings settings_;
    ComPtr<ITfLangBarItemMgr> bar_manager_;
    ComPtr<LanguageBar> language_bar_;
    InputSession input_;
    Punctuation punctuation_;
    CandidateWindow window_;
    bool chinese_ = true;
    bool automatic_english_ = false;
    bool candidate_navigation_ = false;
    bool shift_alone_ = false;
    bool editing_ = false;
    bool own_edit_ = false;
    bool key_sink_ = false;
    bool ui_suspended_ = false;
    std::uint64_t generation_ = 0;

    IUnknown* identity() { return static_cast<ITfTextInputProcessorEx*>(this); }
    void set_english(bool active) {
        if (automatic_english_ == active) return;
        automatic_english_ = active;
        if (language_bar_) language_bar_->notify();
    }
    void refresh_settings() {
        const auto next = read_ime_settings(user_path_);
        const bool changed = next.learning != settings_.learning || next.chinese_punctuation != settings_.chinese_punctuation ||
            next.automatic_english != settings_.automatic_english;
        if (dictionary_ && next.learning != settings_.learning) {
            dictionary_->refresh();
            input_.refresh_candidates(dictionary_->lexicon(), dictionary_->users());
        }
        if (next.chinese_punctuation != settings_.chinese_punctuation) punctuation_.reset();
        settings_ = next;
        if (!settings_.automatic_english) set_english(false);
        if (changed && language_bar_) language_bar_->notify();
    }
    void toggle_mode() {
        if (automatic_english_) set_english(false);
        else chinese_ = !chinese_;
        shift_alone_ = false; punctuation_.reset(); finish_later();
        if (language_bar_) language_bar_->notify();
    }
    HRESULT bar_command(BarCommand command) {
        if (!manager_) return S_FALSE;
        switch (command) {
        case BarCommand::mode: toggle_mode(); return S_OK;
        case BarCommand::learning:
            set_ime_learning(user_path_, !read_ime_settings(user_path_).learning); refresh_settings(); return S_OK;
        case BarCommand::punctuation:
            set_chinese_punctuation(user_path_, !read_ime_settings(user_path_).chinese_punctuation); refresh_settings(); return S_OK;
        case BarCommand::automatic_english:
            set_automatic_english(user_path_, !read_ime_settings(user_path_).automatic_english); refresh_settings(); return S_OK;
        case BarCommand::settings: {
            const auto executable = module_path().parent_path() / L"private_pinyin_settings.exe";
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", executable.c_str(),
                nullptr, executable.parent_path().c_str(), SW_SHOWNORMAL));
            return result > 32 ? S_OK : E_FAIL;
        }
        case BarCommand::directory: return open_user_directory();
        }
        return E_INVALIDARG;
    }
    HRESULT request(ITfContext* context, DWORD flags, std::function<HRESULT(TfEditCookie)> action) {
        if (!context || client_ == TF_CLIENTID_NULL) return E_UNEXPECTED;
        ComPtr<ITfEditSession> session; session.Attach(new EditSession(identity(), std::move(action)));
        HRESULT result = E_FAIL;
        const auto hr = context->RequestEditSession(client_, session.Get(), flags, &result);
        return FAILED(hr) ? hr : result;
    }
    void close_ui() noexcept {
        window_.hide();
        if (element_) { element_->set_visibility_handler({}); element_->set_visible(false); }
        if (ui_manager_ && element_id_ != TF_INVALID_UIELEMENTID) ui_manager_->EndUIElement(element_id_);
        element_id_ = TF_INVALID_UIELEMENTID; element_.Reset();
    }
    void discard_state() noexcept {
        ++generation_; input_.clear(); candidate_navigation_ = false;
        composition_.Reset(); composing_.Reset(); ui_suspended_ = false; close_ui();
    }
    HRESULT finish(TfEditCookie cookie, const std::optional<std::wstring>& replacement) {
        if (!composition_) { discard_state(); return S_OK; }
        auto composition = composition_;
        ComPtr<ITfRange> range;
        auto hr = composition->GetRange(&range);
        if (FAILED(hr)) return hr;
        if (replacement) {
            hr = range->SetText(cookie, 0, replacement->data(), static_cast<LONG>(replacement->size()));
            if (FAILED(hr)) return hr;
            ComPtr<ITfRange> caret;
            if (SUCCEEDED(range->Clone(&caret))) {
                caret->Collapse(cookie, TF_ANCHOR_END);
                TF_SELECTION selection{caret.Get(), {TF_AE_NONE, FALSE}};
                composing_->SetSelection(cookie, 1, &selection);
            }
        }
        ComPtr<ITfProperty> attribute;
        if (composing_ && SUCCEEDED(composing_->GetProperty(GUID_PROP_ATTRIBUTE, &attribute))) attribute->Clear(cookie, range.Get());
        // Detach before EndComposition: it can synchronously call OnCompositionTerminated.
        composition_.Reset();
        hr = composition->EndComposition(cookie);
        discard_state(); return hr;
    }
    void finish_later() {
        set_english(false);
        // Queued layout/UI read sessions can run before the terminating write
        // session. They must not resurrect a popup after focus loss/deactivation.
        ui_suspended_ = true;
        close_ui();
        if (!composition_ || !composing_) { discard_state(); return; }
        const auto generation = generation_;
        auto context = composing_;
        // Preserve unconfirmed raw text when focus or selection changes.
        request(context.Get(), TF_ES_ASYNC | TF_ES_READWRITE,
            [this, context, generation](TfEditCookie cookie) {
                if (generation != generation_ || !same_object(context.Get(), composing_.Get())) return S_OK;
                return finish(cookie, std::nullopt);
            });
    }
    void unobserve() noexcept {
        if (observed_) {
            ComPtr<ITfSource> source;
            if (SUCCEEDED(observed_.As(&source))) {
                if (text_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(text_cookie_);
                if (layout_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(layout_cookie_);
            }
        }
        text_cookie_ = layout_cookie_ = TF_INVALID_COOKIE; observed_.Reset();
    }
    HRESULT observe(ITfContext* context) {
        if (same_object(context, observed_.Get())) return S_OK;
        set_english(false);
        if (composition_ && !same_object(context, composing_.Get())) finish_later();
        unobserve();
        punctuation_.reset();
        if (!context) return S_OK;
        ComPtr<ITfSource> source;
        auto hr = context->QueryInterface(IID_PPV_ARGS(&source));
        if (FAILED(hr)) return hr;
        observed_ = context;
        hr = source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &text_cookie_);
        if (FAILED(hr)) { unobserve(); return hr; }
        hr = source->AdviseSink(IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this), &layout_cookie_);
        if (FAILED(hr)) { unobserve(); return hr; }
        return S_OK;
    }
    HRESULT update_ui(TfEditCookie cookie) {
        if (!manager_ || ui_suspended_ || !composition_ || input_.empty() || !composing_) { close_ui(); return S_OK; }
        ComPtr<ITfDocumentMgr> document; composing_->GetDocumentMgr(&document);
        if (ui_manager_) {
            if (!element_) {
                element_.Attach(new CandidateElement);
                element_->update(input_, document.Get());
                BOOL show = TRUE;
                if (FAILED(ui_manager_->BeginUIElement(element_.Get(), &show, &element_id_))) {
                    element_.Reset(); element_id_ = TF_INVALID_UIELEMENTID;
                } else {
                    element_->Show(show);
                    element_->set_visibility_handler([this](bool visible) {
                        if (!visible) { window_.hide(); return; }
                        if (!composing_) return;
                        const auto generation = generation_; auto target = composing_;
                        request(target.Get(), TF_ES_ASYNC | TF_ES_READ, [this, target, generation](TfEditCookie read) {
                            return generation == generation_ && same_object(target.Get(), composing_.Get()) ? update_ui(read) : S_OK;
                        });
                    });
                }
            } else { element_->update(input_, document.Get()); ui_manager_->UpdateUIElement(element_id_); }
            if (element_ && !element_->allowed()) { window_.hide(); element_->set_visible(false); return S_OK; }
        }
        ComPtr<ITfContextView> view; ComPtr<ITfRange> range;
        if (FAILED(composing_->GetActiveView(&view)) || FAILED(composition_->GetRange(&range))) {
            window_.hide(); if (element_) element_->set_visible(false); return S_OK;
        }
        RECT caret{}; BOOL clipped = FALSE;
        // Query the composition range: zero-length caret ranges are unsupported by some editors.
        const auto hr = view->GetTextExt(cookie, range.Get(), &caret, &clipped);
        if (FAILED(hr) || clipped) { window_.hide(); if (element_) element_->set_visible(false); return S_OK; }
        HWND owner = nullptr; view->GetWnd(&owner);
        if (!owner) owner = GetFocus();
        window_.show(input_, caret, owner, [this](std::size_t index) {
            protect([&] {
                if (!composing_) return S_FALSE;
                if (index >= input_.candidates().size()) return S_FALSE;
                const auto spelling = input_.preedit();
                const auto word = input_.candidates()[index];
                auto context = composing_; const auto generation = generation_;
                return request(context.Get(), TF_ES_ASYNC | TF_ES_READWRITE,
                    [this, context, generation, index, spelling, word](TfEditCookie write) {
                        if (generation != generation_ || !same_object(context.Get(), composing_.Get())) return S_OK;
                        if (input_.preedit() != spelling || index >= input_.candidates().size() ||
                            input_.candidates()[index].text != word.text ||
                            input_.candidates()[index].pronunciation != word.pronunciation ||
                            input_.candidates()[index].input_end != word.input_end) return S_FALSE;
                        if (input_disabled(context.Get()) || direct_input(context.Get(), write)) return S_FALSE;
                        refresh_settings();
                        if (index >= input_.candidates().size() || input_.candidates()[index].text != word.text ||
                            input_.candidates()[index].pronunciation != word.pronunciation) return S_FALSE;
                        auto previous = input_;
                        const auto result = input_.select(index, dictionary_->lexicon(), dictionary_->users());
                        const auto hr = apply_result(context.Get(), write, result);
                        if (FAILED(hr)) input_ = std::move(previous);
                        return hr;
                    });
            });
        }, !dictionary_->enabled() ? L"无痕" : dictionary_->available() ? L"本地学习" : L"学习暂不可用");
        if (element_) element_->set_visible(window_.visible());
        return S_OK;
    }
    HRESULT update_composition(ITfContext* context, TfEditCookie cookie) {
        ComPtr<ITfRange> range;
        if (!composition_) {
            ComPtr<ITfInsertAtSelection> insertion;
            auto hr = context->QueryInterface(IID_PPV_ARGS(&insertion));
            if (FAILED(hr)) return hr;
            hr = insertion->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, nullptr, 0, &range);
            if (FAILED(hr)) return hr;
            ComPtr<ITfContextComposition> composer;
            hr = context->QueryInterface(IID_PPV_ARGS(&composer));
            if (FAILED(hr)) return hr;
            hr = composer->StartComposition(cookie, range.Get(), this, &composition_);
            if (FAILED(hr) || !composition_) return FAILED(hr) ? hr : E_FAIL;
            composing_ = context; ui_suspended_ = false; ++generation_;
        } else {
            auto hr = composition_->GetRange(&range);
            if (FAILED(hr)) return hr;
        }
        const auto text = wide(input_.preedit());
        auto hr = range->SetText(cookie, 0, text.data(), static_cast<LONG>(text.size()));
        if (FAILED(hr)) return hr;
        ComPtr<ITfProperty> attribute;
        if (attribute_atom_ != TF_INVALID_GUIDATOM && SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &attribute))) {
            VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = attribute_atom_;
            attribute->SetValue(cookie, range.Get(), &value);
        }
        ComPtr<ITfRange> caret;
        hr = range->Clone(&caret); if (FAILED(hr)) return hr;
        caret->Collapse(cookie, TF_ANCHOR_START);
        LONG shifted = 0;
        const auto position = wide(input_.confirmed_text()).size() + input_.cursor();
        hr = caret->ShiftEnd(cookie, static_cast<LONG>(position), &shifted, nullptr);
        if (FAILED(hr)) return hr;
        caret->Collapse(cookie, TF_ANCHOR_END);
        TF_SELECTION selection{caret.Get(), {TF_AE_NONE, FALSE}};
        hr = context->SetSelection(cookie, 1, &selection);
        if (FAILED(hr)) return hr;
        return update_ui(cookie);
    }
    HRESULT insert_text(ITfContext* context, TfEditCookie cookie, const std::wstring& text) {
        ComPtr<ITfInsertAtSelection> insertion; ComPtr<ITfRange> range;
        auto hr = context->QueryInterface(IID_PPV_ARGS(&insertion));
        if (FAILED(hr)) return hr;
        hr = insertion->InsertTextAtSelection(cookie, 0, text.data(), static_cast<LONG>(text.size()), &range);
        if (FAILED(hr)) return hr;
        hr = range->Collapse(cookie, TF_ANCHOR_END); if (FAILED(hr)) return hr;
        TF_SELECTION selection{range.Get(), {TF_AE_NONE, FALSE}};
        return context->SetSelection(cookie, 1, &selection);
    }
    PunctuationContext punctuation_context(ITfContext* context, TfEditCookie cookie, FieldKind kind) {
        PunctuationContext result; result.preedit = input_.preedit(); result.literal_scope = kind == FieldKind::literal;
        if (result.literal_scope) return result;
        ComPtr<ITfRange> range;
        if (composition_) {
            if (FAILED(composition_->GetRange(&range))) return result;
        } else {
            TF_SELECTION selection{}; ULONG fetched = 0;
            if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return result;
            range.Attach(selection.range);
        }
        if (FAILED(range->Collapse(cookie, TF_ANCHOR_START))) return result;
        // Read only the contiguous ASCII token before the caret, in small
        // transient chunks. Retain no application context between keys.
        bool first = true, letters = false, separator = false;
        for (;;) {
            LONG shifted = 0; wchar_t buffer[64]{}; ULONG read = 0;
            if (FAILED(range->ShiftStart(cookie, -64, &shifted, nullptr)) || !shifted ||
                FAILED(range->GetText(cookie, 0, buffer, 64, &read))) return result;
            if (first) {
                for (ULONG i = 0; i < read; ++i) result.before += buffer[i] < 0x80 ? static_cast<char>(buffer[i]) : ' ';
                first = false;
            }
            for (auto i = read; i-- > 0;) {
                const auto ch = buffer[i];
                if (ch <= 0x20 || ch >= 0x7f) return result;
                letters = letters || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
                separator = separator || ch == L'.' || ch == L'/' || ch == L'\\';
                if (ch == L'@' || (letters && separator)) { result.literal_scope = true; return result; }
            }
            if (shifted > -64 || FAILED(range->Collapse(cookie, TF_ANCHOR_START))) return result;
        }
    }
    std::string english_word(ITfContext* context, TfEditCookie cookie) {
        ComPtr<ITfRange> range;
        if (composition_) {
            if (FAILED(composition_->GetRange(&range))) return {};
        } else {
            TF_SELECTION selection{}; ULONG fetched = 0;
            if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return {};
            range.Attach(selection.range);
        }
        if (FAILED(range->Collapse(cookie, TF_ANCHOR_START))) return {};
        // Only the adjacent ASCII word, transiently, including text entered
        // directly by the application (for example with CapsLock).
        LONG shifted = 0; wchar_t text[64]{}; ULONG read = 0;
        if (FAILED(range->ShiftStart(cookie, -64, &shifted, nullptr)) ||
            FAILED(range->GetText(cookie, 0, text, 64, &read))) return {};
        auto start = read;
        while (start && ((text[start - 1] >= L'a' && text[start - 1] <= L'z') ||
                        (text[start - 1] >= L'A' && text[start - 1] <= L'Z') || text[start - 1] == L'\'')) --start;
        std::string word;
        for (auto i = start; i < read; ++i) word += static_cast<char>(text[i]);
        return word + input_.raw();
    }
    HRESULT apply_result(ITfContext* context, TfEditCookie cookie, const InputResult& result,
                         const std::wstring& suffix = {}) {
        if (result.action == InputAction::pass) return S_FALSE;
        own_edit_ = true;
        struct EditingScope {
            bool& flag;
            explicit EditingScope(bool& value) : flag(value) { flag = true; }
            ~EditingScope() { flag = false; }
        } editing(editing_);
        HRESULT hr;
        if (result.action == InputAction::update) hr = update_composition(context, cookie);
        else if (result.action == InputAction::cancel) hr = finish(cookie, std::wstring{});
        else {
            const auto text = wide(result.text) + suffix;
            hr = composition_ ? finish(cookie, text) : insert_text(context, cookie, text);
        }
        if (hr == S_OK && result.action == InputAction::commit && !result.pronunciation.empty())
            dictionary_->remember(result);
        return hr;
    }
    bool wants_key(ITfContext* context, WPARAM key) {
        if (!manager_) return false;
        refresh_settings();
        if (input_disabled(context)) return false;
        if (automatic_english_ && same_object(context, observed_.Get()) && ends_english(key)) return true;
        if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
            (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) return false;
        if (key == VK_SHIFT) return true;
        // Probe without changing sentence state; the real end key is still
        // passed to the application after OnKeyDown resets automatic English.
        if (automatic_english_ && same_object(context, observed_.Get())) return false;
        if (!chinese_ || (GetKeyState(VK_CAPITAL) & 1)) return false;
        if (const auto mark = punctuation_key(key); mark && (*mark != '\'' || input_.raw().empty()))
            return settings_.chinese_punctuation || !input_.empty();
        const auto translated = translate(key);
        if (!translated) return false;
        // Keep an initial Shift+letter and the rest of that literal word in
        // the same editable composition. Existing Chinese segments keep their behavior.
        if (translated->type == InputKey::letter)
            return !(GetKeyState(VK_SHIFT) & 0x8000) || input_.empty() || input_.literal();
        // Claim Space only when there is composition to confirm. An idle probe
        // must agree with OnKeyDown so hosts still translate ordinary spaces.
        if (input_.empty()) return false;
        if (translated->type == InputKey::digit) {
            if (GetKeyState(VK_SHIFT) & 0x8000) return false;
            if (input_.literal()) return true;
            if (translated->value == '0') return false;
            return input_.page() * InputSession::page_size + translated->value - '1' < input_.candidates().size();
        }
        if (translated->type == InputKey::separator && (GetKeyState(VK_SHIFT) & 0x8000)) return false;
        return true;
    }
public:
    ~TextService() { Deactivate(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx)
            *result = static_cast<ITfTextInputProcessorEx*>(this);
        else if (iid == IID_ITfKeyEventSink) *result = static_cast<ITfKeyEventSink*>(this);
        else if (iid == IID_ITfThreadMgrEventSink) *result = static_cast<ITfThreadMgrEventSink*>(this);
        else if (iid == IID_ITfThreadFocusSink) *result = static_cast<ITfThreadFocusSink*>(this);
        else if (iid == IID_ITfCompositionSink) *result = static_cast<ITfCompositionSink*>(this);
        else if (iid == IID_ITfTextEditSink) *result = static_cast<ITfTextEditSink*>(this);
        else if (iid == IID_ITfTextLayoutSink) *result = static_cast<ITfTextLayoutSink*>(this);
        else if (iid == IID_ITfDisplayAttributeProvider) *result = static_cast<ITfDisplayAttributeProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* manager, TfClientId client) override { return ActivateEx(manager, client, 0); }
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* manager, TfClientId client, DWORD flags) override {
        return protect([&] {
            if (!manager || client == TF_CLIENTID_NULL) return E_INVALIDARG;
            if (manager_) return E_UNEXPECTED;
            if (flags & (TF_TMF_SECUREMODE | TF_TMF_COMLESS | TF_TMF_IMMERSIVEMODE)) return E_NOTIMPL;
            user_path_.clear();
            try { user_path_ = default_user_path(); } catch (const std::exception&) {}
            settings_ = read_ime_settings(user_path_);
            dictionary_ = std::make_unique<LearningDictionary>(
                module_path().parent_path() / L"data" / L"base.tsv", user_path_);
            manager_ = manager; client_ = client; chinese_ = true;
            ComPtr<ITfKeystrokeMgr> keys; auto hr = manager_.As(&keys);
            if (SUCCEEDED(hr)) hr = keys->AdviseKeyEventSink(client_, this, TRUE);
            if (FAILED(hr)) { Deactivate(); return hr; } key_sink_ = true;
            ComPtr<ITfSource> source; hr = manager_.As(&source);
            if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &thread_cookie_);
            if (FAILED(hr)) { Deactivate(); return hr; }
            hr = source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &focus_cookie_);
            if (FAILED(hr)) { Deactivate(); return hr; }
            manager_.As(&ui_manager_);
            hr = manager_.As(&bar_manager_);
            if (SUCCEEDED(hr)) {
                language_bar_.Attach(new LanguageBar([this] { return BarState{chinese_ && !automatic_english_, read_ime_settings(user_path_), automatic_english_}; },
                    [this](BarCommand command) { return bar_command(command); }));
                hr = bar_manager_->AddItem(language_bar_.Get());
            }
            if (FAILED(hr)) { Deactivate(); return hr; }
            ComPtr<ITfCategoryMgr> categories;
            if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories))))
                categories->RegisterGUID(attribute_id, &attribute_atom_);
            ComPtr<ITfDocumentMgr> focused; manager_->GetFocus(&focused);
            if (focused) { ComPtr<ITfContext> context; focused->GetTop(&context); hr = observe(context.Get()); }
            if (FAILED(hr)) Deactivate(); return hr;
        });
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        return protect([&] {
            // Never leave TSF subscriptions or candidate windows pointing into an unloaded DLL.
            finish_later(); unobserve();
            if (language_bar_) {
                language_bar_->disconnect();
                if (bar_manager_) bar_manager_->RemoveItem(language_bar_.Get());
            }
            language_bar_.Reset(); bar_manager_.Reset();
            if (manager_) {
                ComPtr<ITfSource> source;
                if (thread_cookie_ != TF_INVALID_COOKIE && SUCCEEDED(manager_.As(&source))) source->UnadviseSink(thread_cookie_);
                if (focus_cookie_ != TF_INVALID_COOKIE && SUCCEEDED(manager_.As(&source))) source->UnadviseSink(focus_cookie_);
                ComPtr<ITfKeystrokeMgr> keys;
                if (key_sink_ && SUCCEEDED(manager_.As(&keys))) keys->UnadviseKeyEventSink(client_);
            }
            thread_cookie_ = focus_cookie_ = TF_INVALID_COOKIE; key_sink_ = false; shift_alone_ = false;
            manager_.Reset(); ui_manager_.Reset(); client_ = TF_CLIENTID_NULL;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override {
        return protect([&] { if (!foreground) { shift_alone_ = false; finish_later(); } else refresh_settings(); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE OnSetThreadFocus() override {
        return protect([&] { refresh_settings(); if (language_bar_) language_bar_->notify(); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE OnKillThreadFocus() override {
        return protect([&] { shift_alone_ = false; finish_later(); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER; *eaten = FALSE;
        return protect([&] { if (key != VK_SHIFT) shift_alone_ = false; *eaten = wants_key(context, key); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER; *eaten = FALSE;
        return protect([&] {
            if (!wants_key(context, key)) return S_OK;
            if (key == VK_SHIFT) { shift_alone_ = true; *eaten = TRUE; return S_OK; }
            shift_alone_ = false;
            if (automatic_english_ && same_object(context, observed_.Get()) && ends_english(key)) {
                set_english(false); punctuation_.reset(); return S_OK;
            }
            auto mark = punctuation_key(key);
            if (mark && *mark == '\'' && !input_.raw().empty()) mark.reset(); // Pinyin separator or English contraction.
            auto translated = translate(key); if (!mark && !translated) return S_OK;
            if (translated && translated->type == InputKey::letter && (GetKeyState(VK_SHIFT) & 0x8000))
                translated->value = static_cast<char>(key);
            auto hr = observe(context); if (FAILED(hr)) return hr;
            auto target = ComPtr<ITfContext>(context);
            hr = request(context, TF_ES_SYNC | TF_ES_READWRITE, [this, target, translated, mark](TfEditCookie cookie) {
                const auto kind = field_kind(target.Get(), cookie);
                if (input_disabled(target.Get()) || kind == FieldKind::sensitive) return S_FALSE;
                if (composition_ && !same_object(composing_.Get(), target.Get())) return S_FALSE;
                auto previous = input_;
                const bool word_confirmation = translated &&
                    (translated->type == InputKey::space || translated->type == InputKey::enter);
                const auto suffix = mark ? std::wstring(1, *mark) :
                    translated && translated->type == InputKey::space ? std::wstring(L" ") : std::wstring{};
                if (input_.literal() && (word_confirmation || mark)) {
                    const bool english = settings_.automatic_english && kind == FieldKind::normal &&
                        input_.cursor() == input_.raw().size() &&
                        !punctuation_context(target.Get(), cookie, kind).literal_scope &&
                        starts_english(input_.raw(), dictionary_->lexicon(), dictionary_->users());
                    const auto result = input_.handle(InputKey::enter, 0, dictionary_->lexicon(), dictionary_->users());
                    const auto hr = apply_result(target.Get(), cookie, result, suffix);
                    if (hr == S_OK) { punctuation_.reset(); set_english(english); }
                    else if (FAILED(hr)) input_ = std::move(previous);
                    return hr;
                }
                if (settings_.automatic_english && kind == FieldKind::normal && !candidate_navigation_ &&
                    input_.confirmed_text().empty() && input_.cursor() == input_.raw().size() &&
                    (word_confirmation || mark) &&
                    !punctuation_context(target.Get(), cookie, kind).literal_scope &&
                    starts_english(english_word(target.Get(), cookie), dictionary_->lexicon(), dictionary_->users())) {
                    const auto result = input_.empty() ? InputResult{InputAction::commit, {}, {}} :
                        input_.handle(InputKey::enter, 0, dictionary_->lexicon(), dictionary_->users());
                    const auto hr = apply_result(target.Get(), cookie, result, suffix);
                    if (hr == S_OK) { punctuation_.reset(); set_english(true); }
                    else if (FAILED(hr)) input_ = std::move(previous);
                    return hr;
                }
                if (mark) {
                    const auto punctuation = punctuation_.resolve(*mark, punctuation_context(target.Get(), cookie, kind));
                    // Disabling Chinese punctuation changes the symbol, not the
                    // candidate-confirmation or URL/password behavior.
                    const auto symbol = settings_.chinese_punctuation ? punctuation.text : std::string(1, *mark);
                    const auto result = input_.empty() ? InputResult{InputAction::commit, {}, {}} :
                        punctuation.raw ? input_.handle(InputKey::enter, 0, dictionary_->lexicon(), dictionary_->users()) :
                        input_.confirm_remaining(dictionary_->lexicon(), dictionary_->users());
                    const auto hr = apply_result(target.Get(), cookie, result, wide(symbol));
                    if (hr == S_OK && settings_.chinese_punctuation) punctuation_.accepted(punctuation);
                    else if (FAILED(hr)) input_ = std::move(previous);
                    return hr;
                }
                if (input_.empty() && translated->type == InputKey::letter) dictionary_->refresh();
                const auto result = input_.handle(translated->type, translated->value, dictionary_->lexicon(), dictionary_->users());
                if (result.action == InputAction::pass) return S_FALSE;
                const auto hr = apply_result(target.Get(), cookie, result);
                if (hr == S_OK && result.action == InputAction::update &&
                    (translated->type == InputKey::previous || translated->type == InputKey::next ||
                     translated->type == InputKey::page_previous || translated->type == InputKey::page_next))
                    candidate_navigation_ = true;
                if (FAILED(hr)) input_ = std::move(previous);
                return hr;
            });
            *eaten = hr == S_OK;
            // Fail open: input reaches the application if TSF cannot grant a write session.
            return FAILED(hr) ? S_OK : hr;
        });
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER; *eaten = key == VK_SHIFT && shift_alone_; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER; *eaten = FALSE;
        return protect([&] {
            if (key == VK_SHIFT && shift_alone_) {
                toggle_mode(); *eaten = TRUE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override {
        if (!eaten) return E_POINTER; *eaten = FALSE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* focused, ITfDocumentMgr*) override {
        return protect([&] { ComPtr<ITfContext> context; if (focused) focused->GetTop(&context); return observe(context.Get()); });
    }
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext* context) override { return protect([&] { return observe(context); }); }
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext* context) override {
        return protect([&] { if (same_object(context, observed_.Get())) { finish_later(); unobserve(); } return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie cookie, ITfComposition* composition) override {
        return protect([&] {
            if (same_object(composition, composition_.Get())) {
                ComPtr<ITfRange> range; ComPtr<ITfProperty> attribute;
                if (composing_ && SUCCEEDED(composition->GetRange(&range)) &&
                    SUCCEEDED(composing_->GetProperty(GUID_PROP_ATTRIBUTE, &attribute))) attribute->Clear(cookie, range.Get());
                discard_state();
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE OnEndEdit(ITfContext* context, TfEditCookie cookie, ITfEditRecord* record) override {
        return protect([&] {
            if (editing_ || !composition_ || !same_object(context, composing_.Get())) return S_OK;
            if (own_edit_) { own_edit_ = false; return S_OK; }
            // If the application edited text (e.g. a punctuation key passed through), preserve
            // that edit and terminate instead of overwriting it on the next pinyin keystroke.
            ComPtr<IEnumTfRanges> updates;
            if (record && SUCCEEDED(record->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, nullptr, 0, &updates))) {
                ComPtr<ITfRange> updated; ULONG fetched = 0;
                if (updates && SUCCEEDED(updates->Next(1, &updated, &fetched)) && fetched) { finish_later(); return S_OK; }
            }
            BOOL changed = FALSE; if (!record || FAILED(record->GetSelectionStatus(&changed)) || !changed) return S_OK;
            TF_SELECTION selection{}; ULONG fetched = 0; ComPtr<ITfRange> range;
            if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || !fetched) return S_OK;
            ComPtr<ITfRange> selected; selected.Attach(selection.range);
            if (FAILED(composition_->GetRange(&range))) return S_OK;
            ComPtr<ITfRange> expected; if (FAILED(range->Clone(&expected))) return S_OK;
            expected->Collapse(cookie, TF_ANCHOR_START);
            LONG shifted = 0; expected->ShiftEnd(cookie, static_cast<LONG>(input_.cursor()), &shifted, nullptr);
            expected->Collapse(cookie, TF_ANCHOR_END);
            LONG start = 0, end = 0;
            if (SUCCEEDED(selected->CompareStart(cookie, expected.Get(), TF_ANCHOR_START, &start)) &&
                SUCCEEDED(selected->CompareEnd(cookie, expected.Get(), TF_ANCHOR_END, &end)) && (start != 0 || end != 0)) finish_later();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) override {
        return protect([&] {
            if (!composition_ || !same_object(context, composing_.Get())) return S_OK;
            if (code == TF_LC_DESTROY) { finish_later(); return S_OK; }
            const auto generation = generation_; auto target = ComPtr<ITfContext>(context);
            request(context, TF_ES_ASYNC | TF_ES_READ, [this, target, generation](TfEditCookie cookie) {
                return generation == generation_ && same_object(target.Get(), composing_.Get()) ? update_ui(cookie) : S_OK;
            }); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** result) override { return create_attribute_enumerator(result); }
    HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        return guid == attribute_id ? create_attribute(result) : E_INVALIDARG;
    }
};
}
HRESULT create_service(REFIID iid, void** result) {
    if (!result) return E_POINTER; *result = nullptr;
    return protect([&] { auto service = new TextService; auto hr = service->QueryInterface(iid, result); service->Release(); return hr; });
}
}
