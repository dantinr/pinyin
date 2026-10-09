#pragma once
#include "common.hpp"
#include <ctfutb.h>
#include <ctffunc.h>
#include "pinyin/ime_settings.hpp"
#include <functional>

namespace pinyin::ime {
enum class BarCommand : UINT { mode = 1, learning, punctuation, settings, directory };
struct BarState { bool chinese = true; ImeSettings settings; };
class LanguageBar final : public ITfLangBarItemButton, public ITfSource, private ModuleObject {
    std::atomic<ULONG> refs_{1};
    ComPtr<ITfLangBarItemSink> sink_;
    DWORD cookie_ = TF_INVALID_COOKIE;
    DWORD next_cookie_ = 1;
    DWORD status_ = 0;
    std::function<BarState()> state_;
    std::function<HRESULT(BarCommand)> command_;
public:
    LanguageBar(std::function<BarState()> state, std::function<HRESULT(BarCommand)> command)
        : state_(std::move(state)), command_(std::move(command)) {}
    void notify() noexcept;
    void disconnect() noexcept;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO*) override;
    HRESULT STDMETHODCALLTYPE GetStatus(DWORD*) override;
    HRESULT STDMETHODCALLTYPE Show(BOOL) override;
    HRESULT STDMETHODCALLTYPE GetTooltipString(BSTR*) override;
    HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick, POINT, const RECT*) override;
    HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu*) override;
    HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) override;
    HRESULT STDMETHODCALLTYPE GetIcon(HICON*) override;
    HRESULT STDMETHODCALLTYPE GetText(BSTR*) override;
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID, IUnknown*, DWORD*) override;
    HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD) override;
};
}
