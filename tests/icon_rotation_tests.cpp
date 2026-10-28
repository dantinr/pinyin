#include "icon_rotation.hpp"
#include "test_workspace.hpp"
#include <fstream>
#include <iostream>
#include <vector>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
std::wstring selected(const std::filesystem::path& cursor) {
    std::ifstream file(cursor, std::ios::binary);
    std::wstring value(static_cast<std::size_t>(std::filesystem::file_size(cursor)) / sizeof(wchar_t), L'\0');
    file.read(reinterpret_cast<char*>(value.data()), value.size() * sizeof(wchar_t)); return value;
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin::ime;
    try {
        check(argc == 2, "expected bundled icon directory");
        pinyin::testing::TestWorkspace workspace;
        const auto bundled = workspace.root / L"bundled", personal = workspace.root / L"personal";
        const auto cursor = workspace.root / L"state" / L"cursor";
        std::filesystem::create_directory(bundled); std::filesystem::create_directory(personal);
        const std::filesystem::path source(argv[1]);
        check(!next_rotation_icon(bundled, personal, cursor, 16) && !std::filesystem::exists(cursor),
            "empty pools created cursor state or returned an icon");
        for (const auto name : {L"01-nib.ico", L"02-mountain.ico", L"03-leaf.ico"})
            std::filesystem::copy_file(source / name, bundled / name);
        std::ofstream(bundled / L"00-broken.ico") << "not an icon";
        std::filesystem::create_directory(bundled / L"00-directory.ico");
        std::filesystem::copy_file(source / L"04-spark.ico", bundled / L"ignored.txt");
        const auto next = [&](int size = 16) {
            const auto icon = next_rotation_icon(bundled, personal, cursor, size);
            check(icon != nullptr, "valid pool failed to load");
            ICONINFO info{}; check(!!GetIconInfo(icon, &info), "cannot inspect selected icon");
            BITMAP bitmap{}; GetObjectW(info.hbmMask, sizeof(bitmap), &bitmap);
            const bool monochrome = !info.hbmColor && bitmap.bmWidth == size && bitmap.bmHeight == size * 2;
            std::vector<BYTE> pixels(bitmap.bmWidthBytes * bitmap.bmHeight);
            GetBitmapBits(info.hbmMask, static_cast<LONG>(pixels.size()), pixels.data());
            bool ink = false, transparent = false;
            for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
                const bool clear = (pixels[y * bitmap.bmWidthBytes + x / 8] & (0x80 >> (x % 8))) != 0;
                ink = ink || !clear; transparent = transparent || clear;
            }
            if (info.hbmColor) DeleteObject(info.hbmColor);
            DeleteObject(info.hbmMask); DestroyIcon(icon);
            check(monochrome, "selected icon is not monochrome at the requested size");
            check(ink && transparent, "selected icon lost its glyph or transparent background");
            return selected(cursor);
        };
        check(next() == L"01-nib.ico" && next() == L"02-mountain.ico" && next() == L"03-leaf.ico" && next() == L"01-nib.ico",
            "selection did not wrap deterministically across calls");
        std::ofstream(personal / L"02-MOUNTAIN.ICO") << "broken override";
        check(next(24) == L"03-leaf.ico", "personal override was ignored or invalid icon was selected");
        std::filesystem::copy_file(source / L"04-spark.ico", personal / L"04-星芒.ICO");
        check(next(32) == L"04-星芒.ico", "Unicode uppercase ICO filename was not discovered");
        const auto locked = CreateFileW(cursor.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "cannot lock cursor fixture");
        const auto busy = next_rotation_icon(bundled, personal, cursor, 16); CloseHandle(locked);
        check(!busy && selected(cursor) == L"04-星芒.ico", "busy cursor changed shared state");
        std::filesystem::remove(bundled / L"01-nib.ico");
        check(next() == L"03-leaf.ico", "removed file prevented cycling");
        { std::ofstream file(cursor, std::ios::binary); file << "invalid-state"; }
        check(next() == L"03-leaf.ico", "invalid cursor did not recover");
        next(); const auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 40; ++i) next();
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles + 1, "rotation leaked GDI resources");
        const auto absent = workspace.root / L"absent";
        check(!next_rotation_icon(absent, absent, cursor, 16), "missing directories returned an icon");
        check(!next_rotation_icon(bundled, personal, cursor, 0), "invalid size was accepted");
        std::cout << "PASS: " << checks << " icon rotation checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
