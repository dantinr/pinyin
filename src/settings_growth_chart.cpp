#include "settings_growth_chart.hpp"
#include "pinyin/input_statistics.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
struct Pen {
    HDC dc;
    HPEN value;
    Pen(HDC context, COLORREF color, int width = 1) : dc(context), value(CreatePen(PS_SOLID, width, color)) {}
    ~Pen() {
        if (value) {
            if (GetCurrentObject(dc, OBJ_PEN) == value) SelectObject(dc, GetStockObject(BLACK_PEN));
            DeleteObject(value);
        }
    }
};
void text(HDC dc, std::wstring value, RECT rectangle, UINT flags = DT_LEFT | DT_SINGLELINE) {
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rectangle, flags | DT_NOPREFIX);
}
void line(HDC dc, int left, int top, int right, int bottom) {
    MoveToEx(dc, left, top, nullptr); LineTo(dc, right, bottom);
}
}
void paint_vocabulary_growth(const DRAWITEMSTRUCT& item, const pinyin::VocabularyGrowth& growth,
    bool available) {
    const auto dc = item.hDC;
    const auto saved = SaveDC(dc);
    struct Restore { HDC dc; int saved; ~Restore() { if (saved) RestoreDC(dc, saved); } } restore{dc, saved};
    FillRect(dc, &item.rcItem, GetSysColorBrush(COLOR_WINDOW));
    const auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    if (font) SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
    const auto height = std::max(12L, metrics.tmHeight);
    const auto width = item.rcItem.right - item.rcItem.left;
    const auto bottom = item.rcItem.bottom;
    if (!available) {
        text(dc, L"词库增长记录暂不可读，请检查文件后刷新。", item.rcItem, DT_CENTER | DT_VCENTER); return;
    }
    const auto points = pinyin::vocabulary_growth_series(growth, pinyin::local_statistics_date());
    std::uint64_t peak = 0;
    for (const auto& point : points) if (point.added) peak = std::max(peak, *point.added);
    // Four divisions with integer labels, including a useful scale on zero days.
    const auto magnitude = peak < 4 ? 1.0 : std::pow(10.0, std::floor(std::log10(static_cast<double>(peak) / 4.0)));
    const auto step = std::max(1.0, std::ceil(static_cast<double>(peak) / (4.0 * magnitude)) * magnitude);
    const auto maximum = step * 4.0;
    const auto scale_label = maximum >= 1e18 ? std::wstring(L">10¹⁸") :
        std::to_wstring(static_cast<std::uint64_t>(maximum));
    SIZE scale_size{}; GetTextExtentPoint32W(dc, scale_label.c_str(), static_cast<int>(scale_label.size()), &scale_size);
    SIZE date_size{}; GetTextExtentPoint32W(dc, L"00-00", 5, &date_size);
    const auto left = std::max(height * 2, scale_size.cx + height / 2);
    const auto right = width - std::max(height, date_size.cx / 2 + height / 3);
    const auto top = height * 2;
    const auto floor = bottom - height * 2;
    if (right <= left || floor <= top) return;
    const auto since = growth.since.empty() ? L"等待开始统计" : L"开始统计：" +
        std::wstring(growth.since.begin(), growth.since.end());
    text(dc, L"近 30 天 · 每日新增字词", {height / 2, 0, width / 2, height + 2});
    text(dc, since, {width / 2, 0, width - height / 2, height + 2}, DT_RIGHT);
    Pen grid(dc, GetSysColor(COLOR_3DLIGHT)); Pen curve(dc, RGB(0, 116, 196), std::max(2L, height / 10));
    for (int index = 0; index <= 4; ++index) {
        const auto y = floor - (floor - top) * index / 4;
        SelectObject(dc, grid.value); line(dc, left, y, right, y);
        const auto value = step * index;
        const auto label = value >= 1e18 ? std::wstring(L">10¹⁸") : std::to_wstring(static_cast<std::uint64_t>(value));
        text(dc, label, {0, y - height / 2, left - height / 3, y + height}, DT_RIGHT);
    }
    auto x_at = [&](std::size_t index) { return left + static_cast<int>((right - left) * index / (points.size() - 1)); };
    auto y_at = [&](std::uint64_t value) {
        return floor - static_cast<int>(static_cast<double>(value) / maximum * (floor - top));
    };
    for (const auto index : {0u, 7u, 14u, 21u, 29u}) {
        const auto& date = points[index].date;
        const auto label = std::wstring(date.begin() + 5, date.end());
        const auto x = x_at(index);
        text(dc, label, {x - height * 2, floor + height / 3, x + height * 2, bottom}, DT_CENTER);
    }
    SelectObject(dc, curve.value);
    SelectObject(dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dc, RGB(0, 116, 196));
    bool connected = false;
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (!points[index].added) { connected = false; continue; }
        const auto x = x_at(index);
        const auto y = y_at(*points[index].added);
        if (connected) LineTo(dc, x, y); else MoveToEx(dc, x, y, nullptr);
        connected = true;
        // Save the line position because Ellipse does not modify it.
        const auto radius = std::max(2L, height / 8);
        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
    }
    if (!peak) {
        text(dc, growth.since.empty() ? L"暂无记录，重开使用输入法的应用后开始统计。" :
            L"所显示日期暂无新增，已有词汇不计入新增数量。",
            {left + height, top + height, right - height, floor - height}, DT_CENTER | DT_VCENTER);
    }
}
