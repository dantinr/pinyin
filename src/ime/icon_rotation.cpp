#include "icon_rotation.hpp"
#include <map>
#include <cwctype>
#include <vector>

namespace pinyin::ime {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
HICON monochrome(HICON source, int size) {
    // The system recolors TEXTCOLORICON glyphs for light/dark taskbars. Convert
    // alpha silhouettes to a real AND/XOR icon, preserving transparent pixels.
    BITMAPINFO pixels{}; pixels.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pixels.bmiHeader.biWidth = size; pixels.bmiHeader.biHeight = -size;
    pixels.bmiHeader.biPlanes = 1; pixels.bmiHeader.biBitCount = 32; pixels.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    auto color = CreateDIBSection(nullptr, &pixels, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto dc = CreateCompatibleDC(nullptr);
    if (!color || !dc) { if (color) DeleteObject(color); if (dc) DeleteDC(dc); return nullptr; }
    auto old = SelectObject(dc, color);
    ZeroMemory(bits, size * size * 4);
    const bool drawn = !!DrawIconEx(dc, 0, 0, source, size, size, 0, nullptr, DI_NORMAL);
    GdiFlush();
    auto values = static_cast<DWORD*>(bits);
    bool alpha = false;
    for (int i = 0; i < size * size; ++i) alpha = alpha || (values[i] >> 24) != 0;
    const int stride = ((size + 15) / 16) * 2;
    std::vector<BYTE> mask(stride * size * 2, 0);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const auto pixel = values[y * size + x];
        const bool ink = alpha ? (pixel >> 24) >= 128 : (pixel & 0xffffff) < 0xc0c0c0;
        if (!ink) mask[y * stride + x / 8] |= static_cast<BYTE>(0x80 >> (x % 8));
    }
    SelectObject(dc, old); DeleteDC(dc); DeleteObject(color);
    if (!drawn) return nullptr;
    auto bitmap = CreateBitmap(size, size * 2, 1, 1, mask.data());
    if (!bitmap) return nullptr;
    ICONINFO info{}; info.fIcon = TRUE; info.hbmMask = bitmap;
    auto result = CreateIconIndirect(&info); DeleteObject(bitmap); return result;
}
void collect(const std::filesystem::path& directory, std::map<std::wstring, std::filesystem::path>& icons) {
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error), end;
    for (; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) continue;
        auto extension = it->path().extension().wstring();
        for (auto& c : extension) c = static_cast<wchar_t>(std::towlower(c));
        if (extension != L".ico") continue;
        auto name = it->path().filename().wstring();
        for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
        icons[name] = it->path();
    }
}
}
HICON next_rotation_icon(const std::filesystem::path& bundled, const std::filesystem::path& personal,
    const std::filesystem::path& cursor, int size) {
    if (size <= 0) return nullptr;
    std::map<std::wstring, std::filesystem::path> icons;
    collect(bundled, icons); collect(personal, icons);
    if (icons.empty()) return nullptr;
    std::error_code error; std::filesystem::create_directories(cursor.parent_path(), error);
    if (error) return nullptr;
    // Short, non-blocking cross-process selection. A busy or unavailable file
    // leaves the current icon intact; input must never depend on this lock.
    Handle file{CreateFileW(cursor.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) return nullptr;
    wchar_t saved[256]{}; DWORD read = 0;
    if (!ReadFile(file.value, saved, sizeof(saved), &read, nullptr)) return nullptr;
    const std::wstring previous(saved, read / sizeof(wchar_t));
    auto start = icons.find(previous);
    if (start == icons.end() || ++start == icons.end()) start = icons.begin();
    auto current = start;
    do {
        const auto loaded = static_cast<HICON>(LoadImageW(nullptr, current->second.c_str(), IMAGE_ICON,
            size, size, LR_LOADFROMFILE));
        const auto icon = loaded ? monochrome(loaded, size) : nullptr;
        if (loaded) DestroyIcon(loaded);
        if (icon) {
            DWORD written = 0;
            const auto bytes = static_cast<DWORD>(current->first.size() * sizeof(wchar_t));
            SetFilePointer(file.value, 0, nullptr, FILE_BEGIN);
            const bool saved_ok = WriteFile(file.value, current->first.data(), bytes, &written, nullptr) &&
                written == bytes && SetEndOfFile(file.value);
            if (!saved_ok) { DestroyIcon(icon); return nullptr; }
            return icon;
        }
        if (++current == icons.end()) current = icons.begin();
    } while (current != start);
    return nullptr;
}
}
