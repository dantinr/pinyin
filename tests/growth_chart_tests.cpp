#include "settings_growth_chart.hpp"
#include "pinyin/input_statistics.hpp"
#include <fstream>
#include <iostream>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void bitmap(const std::filesystem::path& path, const BITMAPINFOHEADER& info, const void* pixels) {
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info); header.bfSize = header.bfOffBits + info.biSizeImage;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info), sizeof(info));
    file.write(static_cast<const char*>(pixels), info.biSizeImage);
    check(file.good(), "cannot save chart rendering");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        constexpr int width = 1000, height = 300;
        const auto dc = CreateCompatibleDC(nullptr); check(dc != nullptr, "cannot create chart drawing context");
        struct DeleteDCGuard { HDC dc; ~DeleteDCGuard() { DeleteDC(dc); } } close{dc};
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        info.bmiHeader.biSizeImage = width * height * 4;
        void* pixels = nullptr;
        const auto image = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        check(image != nullptr, "cannot create chart image");
        const auto original = SelectObject(dc, image);
        const auto font = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        const auto original_font = SelectObject(dc, font);
        struct GraphicsGuard {
            HDC dc; HGDIOBJ image, font, original, original_font;
            ~GraphicsGuard() { SelectObject(dc, original); SelectObject(dc, original_font); DeleteObject(image); DeleteObject(font); }
        } graphics{dc, image, font, original, original_font};
        DRAWITEMSTRUCT item{}; item.hDC = dc; item.rcItem = {0, 0, width, height};
        const auto date = pinyin::local_statistics_date();
        const auto dates = pinyin::vocabulary_growth_series({}, date);
        pinyin::VocabularyGrowth growth; growth.since = dates[8].date;
        const unsigned values[] = {4, 8, 5, 12, 9, 0, 0, 3, 7, 11, 8, 14, 6, 4, 9, 10, 8, 5, 12, 16, 7, 13};
        for (std::size_t index = 8; index < dates.size(); ++index)
            if (values[index - 8]) growth.days[dates[index].date] = values[index - 8];
        // Windows can create process-owned system brushes/font caches on the
        // first draw. Compare subsequent refreshes after that initialization.
        paint_vocabulary_growth(item, growth, true);
        const auto before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int iteration = 0; iteration < 200; ++iteration) paint_vocabulary_growth(item, growth, true);
        const auto after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        if (after != before) std::cerr << "GDI objects: " << before << " -> " << after << '\n';
        check(after == before, "chart refresh leaked GDI resources");
        if (argc == 2) {
            const std::filesystem::path output = argv[1]; std::filesystem::create_directories(output);
            bitmap(output / L"daily-growth.bmp", info.bmiHeader, pixels);
            growth.days.clear(); growth.since = date; paint_vocabulary_growth(item, growth, true);
            bitmap(output / L"empty-growth.bmp", info.bmiHeader, pixels);
            growth.days[date] = 1; paint_vocabulary_growth(item, growth, true);
            bitmap(output / L"first-word.bmp", info.bmiHeader, pixels);
        }
        // Extremely large valid counters still paint without numeric overflow.
        growth.days[date] = UINT64_MAX; paint_vocabulary_growth(item, growth, true);
        paint_vocabulary_growth(item, {}, false);
        std::cout << "PASS: native chart rendering and 200 resource-safe refreshes\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
