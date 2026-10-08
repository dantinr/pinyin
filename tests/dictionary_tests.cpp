#include "pinyin/lexicon.hpp"
#include "pinyin/input_session.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

bool contains(const pinyin::Lexicon& lexicon, const std::string& query,
              const std::string& text, const std::string& pronunciation) {
    const auto candidates = lexicon.lookup(query, {}, 128);
    return std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.text == text && candidate.pronunciation == pronunciation;
    });
}

// Lexicon::load already validates UTF-8; count its code points for this dataset's
// one-written-character-per-syllable convention, including an explicit er for 儿.
std::size_t character_count(const std::string& text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](unsigned char byte) {
        return (byte & 0xc0) != 0x80;
    }));
}

struct Sample { const char* query; const char* text; const char* pronunciation; };
const Sample samples[] = {
    {"xiexie", "谢谢", "xie xie"},
    {"tianqi", "天气", "tian qi"},
    {"jiating", "家庭", "jia ting"},
    {"jidan", "鸡蛋", "ji dan"},
    {"shufu", "舒服", "shu fu"},
    {"zixingche", "自行车", "zi xing che"},
    {"zuoye", "作业", "zuo ye"},
    {"tushuguan", "图书馆", "tu shu guan"},
    {"hetong", "合同", "he tong"},
    {"huiyi", "会议", "hui yi"},
    {"diannao", "电脑", "dian nao"},
    {"jianpan", "键盘", "jian pan"},
    {"liulanqi", "浏览器", "liu lan qi"},
    {"dianziyoujian", "电子邮件", "dian zi you jian"},
    {"yuandaima", "源代码", "yuan dai ma"},
    {"pinyinshurufa", "拼音输入法", "pin yin shu ru fa"},
    {"yinsibaohu", "隐私保护", "yin si bao hu"},
    {"ditie", "地铁", "di tie"},
    {"beijing", "北京", "bei jing"},
    {"xi'an", "西安", "xi an"},
    {"lvcha", "绿茶", "lv cha"},
    {"nühai", "女孩", "nv hai"},
    {"LÜSE", "绿色", "lv se"},
    {"lvyou", "旅游", "lv you"},
    {"nver", "女儿", "nv er"},
    {"xiexienindebangzhu", "谢谢您的帮助", "xie xie nin de bang zhu"},
    {"zaoshanghao", "早上好", "zao shang hao"},
    {"yanjing", "眼睛", "yan jing"},
    {"yinhangka", "银行卡", "yin hang ka"},
    {"buzhaoji", "不着急", "bu zhao ji"},
    {"chongqing", "重庆", "chong qing"},
    {"chonglai", "重来", "chong lai"},
    {"zhongliang", "重量", "zhong liang"},
    {"yinhang", "银行", "yin hang"},
    {"xingzou", "行走", "xing zou"},
    {"hangshu", "行数", "hang shu"},
    {"yinyue", "音乐", "yin yue"},
    {"kuaile", "快乐", "kuai le"},
    {"shuijiao", "睡觉", "shui jiao"},
    {"ganjue", "感觉", "gan jue"},
    {"pianyi", "便宜", "pian yi"},
    {"fangbian", "方便", "fang bian"},
    {"zhaoyang", "朝阳", "zhao yang"},
    {"chaoyang", "朝阳", "chao yang"},
    {"tonghang", "同行", "tong hang"},
    {"tongxing", "同行", "tong xing"},
    {"renshen", "人参", "ren shen"},
    {"canjia", "参加", "can jia"},
    {"cenci", "参差", "cen ci"},
    {"chenzhi", "称职", "chen zhi"},
    {"duichen", "对称", "dui chen"},
    {"juese", "角色", "jue se"},
    {"zhujue", "主角", "zhu jue"},
    {"chuchai", "出差", "chu chai"},
    {"tiaoshi", "调试", "tiao shi"},
    {"diaoyong", "调用", "diao yong"},
    {"changcheng", "长城", "chang cheng"},
    {"chengzhang", "成长", "cheng zhang"},
    {"xizang", "西藏", "xi zang"},
    {"shoucang", "收藏", "shou cang"},
    {"zhuanji", "传记", "zhuan ji"},
    {"chuanbo", "传播", "chuan bo"},
    {"jiyu", "给予", "ji yu"},
    {"jiaoshu", "教书", "jiao shu"},
    {"zhaoji", "着急", "zhao ji"},
    {"zhuoshou", "着手", "zhuo shou"},
    {"zhi", "滞", "zhi"},
    {"tingzhi", "停滞", "ting zhi"},
    {"tingzhibuqian", "停滞不前", "ting zhi bu qian"},
    {"zhihou", "滞后", "zhi hou"},
    {"zhiliu", "滞留", "zhi liu"},
    {"zhenzhuo", "斟酌", "zhen zhuo"},
    {"ganga", "尴尬", "gan ga"},
    {"cankui", "惭愧", "can kui"},
    {"qieyi", "惬意", "qie yi"},
    {"qiaocui", "憔悴", "qiao cui"},
    {"juejiang", "倔强", "jue jiang"},
    {"zhiniu", "执拗", "zhi niu"},
    {"yunhan", "蕴含", "yun han"},
    {"yunniang", "酝酿", "yun niang"},
    {"qianyi", "迁移", "qian yi"},
    {"qianyimohua", "潜移默化", "qian yi mo hua"},
    {"cencibuqi", "参差不齐", "cen ci bu qi"},
    {"xian", "癣", "xian"},
    {"wanlaijuji", "万籁俱寂", "wan lai ju ji"},
    {"ruhe", "如何", "ru he"},
    {"zhantie", "粘贴", "zhan tie"},
    {"huomian", "和面", "huo mian"},
    {"caifeng", "裁缝", "cai feng"},
    {"fengxi", "缝隙", "feng xi"},
    {"junlie", "龟裂", "jun lie"},
    {"qieerbushe", "锲而不舍", "qie er bu she"},
    {"zenghen", "憎恨", "zeng hen"},
    {"conglong", "葱茏", "cong long"},
    {"baoxiang", "爆香", "bao xiang"},
    {"ruyuanyichang", "如愿以偿", "ru yuan yi chang"},
};
const Sample priorities[] = {
    {"nihao", "你好", "ni hao"},
    {"shouji", "手机", "shou ji"},
    {"sheji", "设计", "she ji"},
    {"huifu", "回复", "hui fu"},
    {"xiaoxi", "消息", "xiao xi"},
    {"keyi", "可以", "ke yi"},
    {"zhidao", "知道", "zhi dao"},
    {"zhongqing", "钟情", "zhong qing"},
};
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        if (argc != 2) throw std::runtime_error("expected production dictionary path");
        pinyin::Lexicon lexicon;
        const auto start = std::chrono::steady_clock::now();
        lexicon.load(argv[1]);
        const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        if (!input) throw std::runtime_error("cannot read dictionary for audit");
        std::set<std::pair<std::string, std::string>> readings;
        std::map<std::string, std::set<std::string>> single_readings;
        std::size_t rows = 0, line_number = 0;
        for (std::string line; std::getline(input, line);) {
            ++line_number;
            if (line_number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const auto first = line.find('\t'), second = line.find('\t', first + 1);
            const auto text = line.substr(0, first);
            const auto pronunciation = line.substr(first + 1, second - first - 1);
            const auto context = " at line " + std::to_string(line_number) + ": " + text;
            if (pronunciation != pinyin::normalize_pronunciation(pronunciation))
                throw std::runtime_error("noncanonical pronunciation" + context);
            const auto syllables = static_cast<std::size_t>(
                std::count(pronunciation.begin(), pronunciation.end(), ' ') + 1);
            if (character_count(text) != syllables)
                throw std::runtime_error("character/syllable count mismatch" + context);
            if (!readings.emplace(text, pronunciation).second)
                throw std::runtime_error("duplicate word/reading" + context);
            if (character_count(text) == 1) single_readings[text].insert(pronunciation);
            ++rows;
        }
        check(!input.bad(), "dictionary audit could not finish reading");
        check(rows == lexicon.size(), "source rows were merged unexpectedly");
        // GB2312 level 1 occupies B0A1-D7F9, with full 94-cell rows except
        // the final row. Windows code page 936 preserves this GB2312 range.
        // Decode the coverage baseline locally, without importing a word list.
        std::size_t level1_count = 0;
        for (int high = 0xb0; high <= 0xd7; ++high) {
            const int final_low = high == 0xd7 ? 0xf9 : 0xfe;
            for (int low = 0xa1; low <= final_low; ++low) {
                const char encoded[] = {static_cast<char>(high), static_cast<char>(low)};
                wchar_t character = 0; char utf8[4]{};
                if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, encoded, 2, &character, 1) != 1)
                    throw std::runtime_error("cannot decode level-1 coverage baseline");
                const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &character, 1, utf8, 4, nullptr, nullptr);
                if (!size) throw std::runtime_error("cannot encode level-1 coverage baseline");
                const std::string text(utf8, size);
                if (!single_readings.count(text)) throw std::runtime_error("missing level-1 character: " + text);
                ++level1_count;
            }
        }
        check(level1_count == 3755, "level-1 coverage range has changed");
        for (const auto& reading : readings) {
            // Every word must have independently usable characters for its
            // annotated pronunciation, so segmented selection cannot stall.
            std::istringstream syllables(reading.second);
            for (std::size_t start = 0; start < reading.first.size();) {
                auto end = start + 1;
                while (end < reading.first.size() && (static_cast<unsigned char>(reading.first[end]) & 0xc0) == 0x80) ++end;
                const auto text = reading.first.substr(start, end - start);
                std::string syllable; syllables >> syllable;
                const auto found = single_readings.find(text);
                if (found == single_readings.end() || !found->second.count(syllable))
                    throw std::runtime_error("word lacks a usable character reading: " + reading.first + " -> " + text + "/" + syllable);
                start = end;
            }
            const auto candidates = lexicon.lookup_composition(reading.second);
            if (std::none_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return candidate.text == reading.first && candidate.pronunciation == reading.second;
            })) throw std::runtime_error("reading is unreachable in the IME candidate list: " + reading.first + "/" + reading.second);
        }
        check(true, "character readings and candidate reachability audit completed");
        for (const auto& sample : samples)
            check(contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("missing expected reading: ") + sample.query + " -> " + sample.text);
        for (const auto& sample : priorities) {
            const auto candidates = lexicon.lookup(sample.query);
            check(!candidates.empty() && candidates.front().text == sample.text &&
                      candidates.front().pronunciation == sample.pronunciation,
                  std::string("unexpected first candidate: ") + sample.query);
        }
        check(!contains(lexicon, "zhongqing", "重庆", "zhong qing"), "incorrect 重庆 reading");
        check(!contains(lexicon, "xingshu", "行数", "xing shu"), "incorrect 行数 reading");
        check(!contains(lexicon, "ren can", "人参", "ren can"), "incorrect 人参 reading");
        check(!contains(lexicon, "yin xing", "银行", "yin xing"), "incorrect 银行 reading");
        check(!contains(lexicon, "jiao se", "角色", "jiao se"), "incorrect 角色 reading");
        check(!contains(lexicon, "xuan", "癣", "xuan"), "incorrect 癣 reading");
        check(!contains(lexicon, "gui lie", "龟裂", "gui lie"), "incorrect 龟裂 reading");
        check(!contains(lexicon, "ju qiang", "倔强", "ju qiang"), "incorrect 倔强 reading");
        check(!contains(lexicon, "can cha", "参差", "can cha"), "incorrect 参差 reading");
        check(!contains(lexicon, "qia er bu she", "锲而不舍", "qia er bu she"), "incorrect 锲而不舍 reading");
        pinyin::InputSession composition;
        for (const char letter : std::string("tingzhi")) composition.handle(pinyin::InputKey::letter, letter, lexicon);
        const auto stop = std::find_if(composition.candidates().begin(), composition.candidates().end(), [](const auto& word) { return word.text == "停"; });
        if (stop == composition.candidates().end()) throw std::runtime_error("missing 停 prefix for segmented regression");
        auto result = composition.select(static_cast<std::size_t>(stop - composition.candidates().begin()), lexicon);
        check(result.action == pinyin::InputAction::update && composition.preedit() == "停zhi", "selecting 停 lost the remaining zhi");
        const auto stagnate = std::find_if(composition.candidates().begin(), composition.candidates().end(), [](const auto& word) { return word.text == "滞"; });
        if (stagnate == composition.candidates().end()) throw std::runtime_error("missing 滞 for independent selection");
        result = composition.select(static_cast<std::size_t>(stagnate - composition.candidates().begin()), lexicon);
        check(result.action == pinyin::InputAction::commit && result.text == "停滞" && result.pronunciation == "ting zhi",
            "independent 滞 selection did not complete 停滞");
        std::cout << "PASS: audited " << rows << " rows, " << checks
                  << " checks; " << level1_count << " level-1 characters; load " << load_ms << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
