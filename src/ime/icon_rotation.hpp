#pragma once
#include "common.hpp"

namespace pinyin::ime {
// Caller owns the returned HICON. Reads only top-level ICO files; a user file
// with the same name replaces a bundled icon. No executable resource is loaded.
HICON next_rotation_icon(const std::filesystem::path& bundled, const std::filesystem::path& personal,
    const std::filesystem::path& cursor, int size);
}
