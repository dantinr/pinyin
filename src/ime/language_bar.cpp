#include "language_bar.hpp"
#include <olectl.h>
#include <array>

namespace pinyin::ime {
namespace {
struct MenuEntry { BarCommand command; std::wstring text; bool checked; };
std::array<MenuEntry, 5> entries(const BarState& state) {
    return {{{BarCommand::mode, state.chinese ? L"切换为英文（Shift）" : L"切换为中文（Shift）", false},
        {BarCommand::learning, L"本地学习", state.settings.learning},
        {BarCommand::punctuation, L"中文标点", state.settings.chinese_punctuation},
        {BarCommand::settings, L"设置…", false}, {BarCommand::directory, L"打开个人词库目录", false}}};
}
HICON mode_icon(bool chinese) {
    const int size = GetSystemMetrics(SM_CXSMICON);
    // Monochrome AND/XOR icon: transparent background, black glyph. Windows
    // applies the current theme's text color through TEXTCOLORICON.
    auto mask = CreateBitmap(size, size * 2, 1, 1, nullptr);
    auto dc = CreateCompatibleDC(nullptr);
    if (!mask || !dc) { if (mask) DeleteObject(mask); if (dc) DeleteDC(dc); return nullptr; }
    const auto previous = SelectObject(dc, mask);
    RECT and_rect{0, 0, size, size}, xor_rect{0, size, size, size * 2};
    FillRect(dc, &and_rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FillRect(dc, &xor_rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    auto font = CreateFontW(-MulDiv(13, size, 16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ old_font = nullptr; if (font) old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, chinese ? L"中" : L"英", 1, &and_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old_font) SelectObject(dc, old_font); if (font) DeleteObject(font);
    SelectObject(dc, previous); DeleteDC(dc);
    ICONINFO info{}; info.fIcon = TRUE; info.hbmMask = mask;
    auto icon = CreateIconIndirect(&info); DeleteObject(mask); return icon;
}
}
void LanguageBar::notify() noexcept {
    auto sink = sink_; if (sink) sink->OnUpdate(TF_LBI_BTNALL | TF_LBI_STATUS);
}
void LanguageBar::disconnect() noexcept {
    state_ = {}; command_ = {}; sink_.Reset(); cookie_ = TF_INVALID_COOKIE;
    status_ |= TF_LBI_STATUS_HIDDEN | TF_LBI_STATUS_DISABLED;
}
HRESULT LanguageBar::QueryInterface(REFIID iid, void** result) {
    if (!result) return E_POINTER; *result = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfLangBarItem || iid == IID_ITfLangBarItemButton)
        *result = static_cast<ITfLangBarItemButton*>(this);
    else if (iid == IID_ITfSource) *result = static_cast<ITfSource*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
HRESULT LanguageBar::GetInfo(TF_LANGBARITEMINFO* info) {
    if (!info) return E_POINTER; *info = {};
    info->clsidService = service_id; info->guidItem = GUID_LBI_INPUTMODE;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
        TF_LBI_STYLE_HIDDENSTATUSCONTROL | TF_LBI_STYLE_TEXTCOLORICON;
    wcscpy_s(info->szDescription, L"隐私拼音 · 中英文与设置"); return S_OK;
}
HRESULT LanguageBar::GetStatus(DWORD* status) {
    if (!status) return E_POINTER; *status = status_; return S_OK;
}
HRESULT LanguageBar::Show(BOOL show) {
    if (show && state_) status_ &= ~TF_LBI_STATUS_HIDDEN;
    else status_ |= TF_LBI_STATUS_HIDDEN;
    notify(); return S_OK;
}
HRESULT LanguageBar::GetTooltipString(BSTR* text) {
    if (!text) return E_POINTER; *text = nullptr;
    return protect([&] {
        const auto state = state_ ? state_() : BarState{};
        std::wstring value = state.chinese ? L"隐私拼音 · 中文" : L"隐私拼音 · 英文";
        value += state.settings.learning ? L" · 本地学习" : L" · 无痕";
        value += L"\n点击或按 Shift 切换；右键打开设置";
        *text = SysAllocString(value.c_str()); return *text ? S_OK : E_OUTOFMEMORY;
    });
}
HRESULT LanguageBar::OnClick(TfLBIClick click, POINT point, const RECT*) {
    return protect([&] {
        if (!command_ || !state_) return S_FALSE;
        if (click == TF_LBI_CLK_LEFT) { auto command = command_; return command(BarCommand::mode); }
        if (click != TF_LBI_CLK_RIGHT) return E_INVALIDARG;
        const auto owner = GetForegroundWindow(); if (!owner) return S_FALSE;
        const auto items = entries(state_());
        const auto menu = CreatePopupMenu(); if (!menu) return E_OUTOFMEMORY;
        for (const auto& entry : items)
            AppendMenuW(menu, MF_STRING | (entry.checked ? MF_CHECKED : MF_UNCHECKED),
                static_cast<UINT>(entry.command), entry.text.c_str());
        const auto command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, owner, nullptr);
        DestroyMenu(menu);
        // The menu message loop can deactivate the service before returning.
        auto action = command_;
        return command && action ? action(static_cast<BarCommand>(command)) : S_OK;
    });
}
HRESULT LanguageBar::InitMenu(ITfMenu* menu) {
    if (!menu) return E_POINTER;
    return protect([&] {
        if (!state_) return S_FALSE;
        for (const auto& entry : entries(state_())) {
            const auto hr = menu->AddMenuItem(static_cast<UINT>(entry.command), entry.checked ? TF_LBMENUF_CHECKED : 0,
                nullptr, nullptr, entry.text.c_str(), static_cast<ULONG>(entry.text.size()), nullptr);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    });
}
HRESULT LanguageBar::OnMenuSelect(UINT command) {
    if (command < static_cast<UINT>(BarCommand::mode) || command > static_cast<UINT>(BarCommand::directory)) return E_INVALIDARG;
    return protect([&] { auto action = command_; return action ? action(static_cast<BarCommand>(command)) : S_FALSE; });
}
HRESULT LanguageBar::GetIcon(HICON* icon) {
    if (!icon) return E_POINTER; *icon = nullptr;
    return protect([&] { *icon = mode_icon(state_ ? state_().chinese : true); return *icon ? S_OK : E_OUTOFMEMORY; });
}
HRESULT LanguageBar::GetText(BSTR* text) {
    if (!text) return E_POINTER; *text = nullptr;
    return protect([&] { *text = SysAllocString(!state_ || state_().chinese ? L"中" : L"英"); return *text ? S_OK : E_OUTOFMEMORY; });
}
HRESULT LanguageBar::AdviseSink(REFIID iid, IUnknown* object, DWORD* cookie) {
    if (!cookie) return E_POINTER; *cookie = TF_INVALID_COOKIE;
    if (!object) return E_POINTER;
    if (iid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    const auto hr = object->QueryInterface(IID_PPV_ARGS(&sink_)); if (FAILED(hr)) return hr;
    if (next_cookie_ == TF_INVALID_COOKIE) ++next_cookie_;
    cookie_ = next_cookie_++; *cookie = cookie_; return S_OK;
}
HRESULT LanguageBar::UnadviseSink(DWORD cookie) {
    if (!sink_ || cookie != cookie_) return CONNECT_E_NOCONNECTION;
    sink_.Reset(); cookie_ = TF_INVALID_COOKIE; return S_OK;
}
}
