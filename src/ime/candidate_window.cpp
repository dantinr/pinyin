#include "candidate_window.hpp"
#include <algorithm>
#include <mutex>

namespace pinyin::ime {
namespace {
constexpr wchar_t window_class[] = L"PrivatePinyin.Candidate.4ea569f1";
std::mutex class_mutex;
unsigned window_count = 0;
const wchar_t* hint_text(bool empty) noexcept {
    return empty ? L"继续输入 · Enter 原文 · Esc 取消" : L"空格选词 · 数字 1–9 · PgUp/PgDn";
}
struct DpiScope {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
};
}
CandidateWindow::~CandidateWindow() {
    if (window_) {
        DestroyWindow(window_);
        std::lock_guard<std::mutex> lock(class_mutex);
        if (--window_count == 0) UnregisterClassW(window_class, module);
    }
    if (font_) DeleteObject(font_);
}
void CandidateWindow::hide() noexcept {
    if (window_) ShowWindow(window_, SW_HIDE);
    select_ = {};
}
void CandidateWindow::show(const InputSession& input, const RECT& caret, HWND owner,
                           std::function<void(std::size_t)> select, const std::wstring& mode) {
    DpiScope dpi_scope;
    if (!window_) {
        std::lock_guard<std::mutex> lock(class_mutex);
        if (!window_count) {
            WNDCLASSEXW cls{sizeof(cls)};
            cls.lpfnWndProc = procedure; cls.hInstance = module; cls.lpszClassName = window_class;
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            if (!RegisterClassExW(&cls)) throw std::runtime_error("candidate window class registration failed");
        }
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            window_class, L"隐私拼音候选", WS_POPUP | WS_BORDER, 0, 0, 1, 1, owner, nullptr, module, this);
        if (!window_) {
            if (!window_count) UnregisterClassW(window_class, module);
            throw std::runtime_error("candidate window creation failed");
        }
        ++window_count;
    }
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    const auto dpi = owner ? GetDpiForWindow(owner) : GetDpiForWindow(window_);
    padding_ = MulDiv(10, dpi ? dpi : 96, 96);
    row_height_ = MulDiv(28, dpi ? dpi : 96, 96);
    if (font_) DeleteObject(font_);
    font_ = CreateFontW(-MulDiv(16, dpi ? dpi : 96, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    const auto confirmed = wide(input.confirmed_text());
    raw_ = confirmed + wide(input.raw());
    raw_.insert(confirmed.size() + input.cursor(), L"|");
    page_start_ = input.page() * InputSession::page_size;
    selected_ = input.selected(); rows_.clear();
    for (auto i = page_start_; i < input.candidates().size() && i < page_start_ + InputSession::page_size; ++i)
        rows_.push_back(std::to_wstring(i - page_start_ + 1) + L". " + wide(input.candidates()[i].text));
    select_ = std::move(select);
    HDC dc = GetDC(window_);
    const auto previous_font = SelectObject(dc, font_);
    int width = MulDiv(220, dpi ? dpi : 96, 96);
    SIZE measured{};
    GetTextExtentPoint32W(dc, raw_.data(), static_cast<int>(raw_.size()), &measured);
    width = std::max(width, static_cast<int>(measured.cx) + 2 * padding_);
    hint_ = mode + L" · " + hint_text(rows_.empty());
    GetTextExtentPoint32W(dc, hint_.data(), static_cast<int>(hint_.size()), &measured);
    width = std::max(width, static_cast<int>(measured.cx) + 2 * padding_ + 2);
    for (const auto& row : rows_) {
        GetTextExtentPoint32W(dc, row.data(), static_cast<int>(row.size()), &measured);
        width = std::max(width, static_cast<int>(measured.cx) + 2 * padding_);
    }
    SelectObject(dc, previous_font); ReleaseDC(window_, dc);
    const int height = 2 * padding_ + row_height_ * static_cast<int>(2 + rows_.size());
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &monitor);
    width = std::min(width, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    int x = std::clamp(caret.left, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
    int y = caret.bottom + 3;
    if (y + height > monitor.rcWork.bottom) y = caret.top - height - 3;
    y = std::max(y, static_cast<int>(monitor.rcWork.top));
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, TRUE);
}
void CandidateWindow::paint() noexcept {
    PAINTSTRUCT paint{}; HDC dc = BeginPaint(window_, &paint);
    RECT area{}; GetClientRect(window_, &area);
    FillRect(dc, &area, GetSysColorBrush(COLOR_WINDOW));
    const auto previous_font = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    RECT row{padding_, padding_, area.right - padding_, padding_ + row_height_};
    DrawTextW(dc, raw_.data(), static_cast<int>(raw_.size()), &row, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        OffsetRect(&row, 0, row_height_);
        const bool selected = page_start_ + i == selected_;
        if (selected) FillRect(dc, &row, GetSysColorBrush(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        DrawTextW(dc, rows_[i].data(), static_cast<int>(rows_[i].size()), &row, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    OffsetRect(&row, 0, row_height_);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(dc, hint_.data(), static_cast<int>(hint_.size()), &row, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, previous_font); EndPaint(window_, &paint);
}
LRESULT CALLBACK CandidateWindow::procedure(HWND window, UINT message, WPARAM w, LPARAM l) noexcept {
    auto self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (self && message == WM_PAINT) { self->paint(); return 0; }
    if (self && message == WM_LBUTTONDOWN) {
        const int y = static_cast<short>(HIWORD(l));
        const int index = (y - self->padding_) / self->row_height_ - 1;
        if (index >= 0 && static_cast<std::size_t>(index) < self->rows_.size() && self->select_) {
            // Copy before the callback: committing destroys/hides the active candidate state.
            try { auto callback = self->select_; callback(self->page_start_ + index); } catch (...) {}
        }
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

HRESULT CandidateElement::QueryInterface(REFIID iid, void** result) {
    if (!result) return E_POINTER; *result = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfUIElement || iid == IID_ITfCandidateListUIElement)
        *result = static_cast<ITfCandidateListUIElement*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
HRESULT CandidateElement::GetDescription(BSTR* result) {
    if (!result) return E_POINTER;
    *result = SysAllocString(L"隐私拼音候选"); return *result ? S_OK : E_OUTOFMEMORY;
}
HRESULT CandidateElement::GetGUID(GUID* result) { if (!result) return E_POINTER; *result = candidate_id; return S_OK; }
HRESULT CandidateElement::Show(BOOL show) {
    return protect([&] {
        allowed_ = !!show;
        if (!allowed_) visible_ = false;
        if (visibility_handler_) visibility_handler_(allowed_);
        return S_OK;
    });
}
HRESULT CandidateElement::IsShown(BOOL* result) { if (!result) return E_POINTER; *result = visible_; return S_OK; }
HRESULT CandidateElement::GetUpdatedFlags(DWORD* result) {
    if (!result) return E_POINTER;
    *result = TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE; return S_OK;
}
HRESULT CandidateElement::GetDocumentMgr(ITfDocumentMgr** result) { return document_.CopyTo(result); }
HRESULT CandidateElement::GetCount(UINT* result) { if (!result) return E_POINTER; *result = static_cast<UINT>(words_.size()); return S_OK; }
HRESULT CandidateElement::GetSelection(UINT* result) { if (!result) return E_POINTER; *result = selected_; return S_OK; }
HRESULT CandidateElement::GetString(UINT index, BSTR* result) {
    if (!result) return E_POINTER; *result = nullptr;
    if (index >= words_.size()) return E_INVALIDARG;
    *result = SysAllocStringLen(words_[index].data(), static_cast<UINT>(words_[index].size()));
    return *result ? S_OK : E_OUTOFMEMORY;
}
HRESULT CandidateElement::GetPageIndex(UINT* indexes, UINT size, UINT* count) {
    if (!count || (size && !indexes)) return E_POINTER;
    *count = static_cast<UINT>((words_.size() + InputSession::page_size - 1) / InputSession::page_size);
    for (UINT i = 0; i < std::min(size, *count); ++i) indexes[i] = i * static_cast<UINT>(InputSession::page_size);
    return size < *count ? S_FALSE : S_OK;
}
HRESULT CandidateElement::GetCurrentPage(UINT* result) {
    if (!result) return E_POINTER; *result = selected_ / static_cast<UINT>(InputSession::page_size); return S_OK;
}
void CandidateElement::update(const InputSession& input, ITfDocumentMgr* document) {
    words_.clear(); for (const auto& candidate : input.candidates()) words_.push_back(wide(candidate.text));
    selected_ = static_cast<UINT>(input.selected()); document_ = document;
}
}
