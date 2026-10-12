#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pinyin/vocabulary_growth.hpp"
#include <windows.h>

void paint_vocabulary_growth(const DRAWITEMSTRUCT& item, const pinyin::VocabularyGrowth& growth,
    bool available);
