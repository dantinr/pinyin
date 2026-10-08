#pragma once
#include "common.hpp"
#include "pinyin/input_session.hpp"
#include <functional>
#include <vector>

namespace pinyin::ime {
class CandidateWindow {
public:
    ~CandidateWindow();
    void show(const InputSession& input, const RECT& caret, HWND owner,
              std::function<void(std::size_t)> select);
    void hide() noexcept;
    bool visible() const noexcept { return window_ && IsWindowVisible(window_); }
private:
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    int row_height_ = 28;
    int padding_ = 10;
    std::size_t page_start_ = 0;
    std::size_t selected_ = 0;
    std::wstring raw_;
    std::vector<std::wstring> rows_;
    std::function<void(std::size_t)> select_;
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM) noexcept;
    void paint() noexcept;
};

// Snapshot owned independently from TextService; retained UI objects never reference a dead service.
class CandidateElement final : public ITfCandidateListUIElement, private ModuleObject {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { auto value = --references_; if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* result) override;
    HRESULT STDMETHODCALLTYPE GetGUID(GUID* result) override;
    HRESULT STDMETHODCALLTYPE Show(BOOL show) override;
    HRESULT STDMETHODCALLTYPE IsShown(BOOL* result) override;
    HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD* result) override;
    HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr** result) override;
    HRESULT STDMETHODCALLTYPE GetCount(UINT* result) override;
    HRESULT STDMETHODCALLTYPE GetSelection(UINT* result) override;
    HRESULT STDMETHODCALLTYPE GetString(UINT index, BSTR* result) override;
    HRESULT STDMETHODCALLTYPE GetPageIndex(UINT* indexes, UINT size, UINT* count) override;
    HRESULT STDMETHODCALLTYPE SetPageIndex(UINT*, UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT* result) override;
    void update(const InputSession& input, ITfDocumentMgr* document);
    bool allowed() const noexcept { return allowed_; }
    void set_visible(bool visible) noexcept { visible_ = visible; }
    void set_visibility_handler(std::function<void(bool)> handler) { visibility_handler_ = std::move(handler); }
private:
    std::atomic<ULONG> references_{1};
    bool allowed_ = true;
    bool visible_ = false;
    UINT selected_ = 0;
    std::vector<std::wstring> words_;
    ComPtr<ITfDocumentMgr> document_;
    std::function<void(bool)> visibility_handler_;
};
}
