#pragma once
#include "pinyin/ime_settings.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>

namespace pinyin::ime {
struct CandidatePalette {
    COLORREF background, text, selection, selection_text, hint;
};
inline COLORREF contrasting_text(COLORREF background) noexcept {
    const auto linear = [](BYTE value) {
        const double channel = value / 255.0;
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const double luminance = 0.2126 * linear(GetRValue(background)) +
        0.7152 * linear(GetGValue(background)) + 0.0722 * linear(GetBValue(background));
    return (luminance + 0.05) / 0.05 >= 1.05 / (luminance + 0.05) ? RGB(0, 0, 0) : RGB(255, 255, 255);
}
inline CandidatePalette candidate_palette(CandidateColors colors) noexcept {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    if (!colors.custom || high_contrast)
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_HIGHLIGHT),
            GetSysColor(COLOR_HIGHLIGHTTEXT), GetSysColor(COLOR_GRAYTEXT)};
    const auto text = contrasting_text(colors.background);
    return {colors.background, text, colors.selection, contrasting_text(colors.selection), text};
}
inline void fill_candidate_color(HDC dc, const RECT& rect, COLORREF color) noexcept {
    // DC_BRUSH avoids allocating GDI objects for every candidate row/preview.
    const auto previous = SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, previous);
}
}
