#include "pinyin/lexicon.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
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
            ++rows;
        }
        check(!input.bad(), "dictionary audit could not finish reading");
        check(rows == lexicon.size(), "source rows were merged unexpectedly");
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
        std::cout << "PASS: audited " << rows << " rows, " << checks
                  << " checks; load " << load_ms << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
