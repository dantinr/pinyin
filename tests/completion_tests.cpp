#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
const pinyin::Candidate& find(const std::vector<pinyin::Candidate>& words, const std::string& text) {
    const auto found = std::find_if(words.begin(), words.end(), [&](const auto& word) { return word.text == text; });
    if (found == words.end()) throw std::runtime_error("missing candidate: " + text); return *found;
}
bool contains(const std::vector<pinyin::Candidate>& words, const std::string& text) {
    return std::any_of(words.begin(), words.end(), [&](const auto& word) { return word.text == text; });
}
void type(pinyin::InputSession& input, const pinyin::Lexicon& lexicon, const std::string& spelling,
          const pinyin::UserDictionary& users = {}) {
    for (const auto ch : spelling) input.handle(ch == '\'' ? pinyin::InputKey::separator : pinyin::InputKey::letter, ch, lexicon, users);
}
std::size_t index(const pinyin::InputSession& input, const std::string& text, std::size_t end) {
    for (std::size_t i = 0; i < input.candidates().size(); ++i)
        if (input.candidates()[i].text == text && input.candidates()[i].input_end == end) return i;
    throw std::runtime_error("missing selectable segment: " + text);
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin;
    try {
        check(argc == 2, "expected dictionary"); Lexicon lexicon; lexicon.load(argv[1]);
        struct Example { const char* query; const char* word; const char* reading; std::size_t shortened; };
        for (const auto& example : {Example{"niha", "你好", "ni hao", 0}, {"ni'ha", "你好", "ni hao", 0},
            {"beiji", "北京", "bei jing", 0}, {"beijin", "北京", "bei jing", 0},
            {"bjin", "北京", "bei jing", 1}, {"nha", "你好", "ni hao", 1},
            {"chongqin", "重庆", "chong qing", 0}, {"chqin", "重庆", "chong qing", 1},
            {"xihuan", "喜欢", "xi huan", 0}}) {
            const auto words = lexicon.lookup(example.query, {}, std::numeric_limits<std::size_t>::max());
            const auto& word = find(words, example.word);
            check(word.pronunciation == example.reading && word.input_end == normalize_query(example.query).size() &&
                word.abbreviations == example.shortened && word.completed == (std::string(example.query) != "xihuan"),
                "tail completion lost canonical reading or typed offsets");
            std::set<std::string> texts; for (const auto& candidate : words)
                check(texts.insert(candidate.text).second, "multiple completion routes duplicated a candidate");
        }
        check(lexicon.lookup_composition("niha").front().text == "你好", "niha did not offer its common complete word first");
        check(lexicon.lookup_composition("beijin").front().text == "北京", "beijin did not offer Beijing first");
        check(lexicon.lookup("niha", {}, 0).empty() && lexicon.decode("niha", {}, 0).empty(), "completion ignored result limit");
        check(find(lexicon.lookup("nih"), "你好").abbreviations == 1 && !find(lexicon.lookup("nih"), "你好").completed,
            "single initial was reclassified as completion");
        check(find(lexicon.lookup("zh"), "中").abbreviations == 1 && !find(lexicon.lookup("zh"), "中").completed,
            "digraph initial was reclassified as completion");

        Lexicon boundaries; boundaries.add({"你好", "ni hao", 10}); boundaries.add({"你", "ni", 5});
        boundaries.add({"好", "hao", 5}); boundaries.add({"北京", "bei jing", 10});
        boundaries.add({"你好世界", "ni hao shi jie", 1000000000});
        check(boundaries.lookup("niha").size() == 1 && boundaries.lookup("niha").front().text == "你好",
            "completion predicted untyped syllables");
        check(boundaries.lookup("niha'beijing").empty() && !contains(boundaries.decode("niha'beijing"), "你好北京"),
            "completion crossed an explicit middle-syllable boundary");
        check(!contains(boundaries.decode("nihao'v"), "你好") && boundaries.lookup_composition("nihao'v").empty(),
            "invalid tail was swallowed");
        for (const auto& query : {"nhi", "nhv", "nvi", "vi"})
            check(lexicon.lookup_composition(query).empty(), "invalid tail was invented");

        Lexicon exact; exact.add({"按", "an", 1}); exact.add({"昂", "ang", 1000000000});
        UserDictionary users; users[{"ang", "昂"}] = 999;
        check(exact.lookup("an", users).front().text == "按" && !exact.lookup("an", users).front().completed,
            "learned completion displaced an exact stored spelling");
        Lexicon readings; readings.add({"长", "chang", 2}); readings.add({"长", "zhang", 1});
        const auto polyphonic = readings.lookup("zhan");
        check(polyphonic.size() == 1 && polyphonic.front().pronunciation == "zhang", "completion invented a polyphonic reading");
        check(!contains(lexicon.decode("zhongqin"), "重庆"), "completion bypassed known word pronunciation");

        const std::string phrase = "我在北京", reading = "wo zai bei jing";
        const auto sentence = lexicon.decode("wozaibeijin");
        check(!sentence.empty() && sentence.front().text == phrase && sentence.front().pronunciation == reading && sentence.front().completed &&
            sentence.front().synthesized && sentence.front().input_end == 11, "sentence tail completion failed");
        check(find(lexicon.decode("wozaibjin"), phrase).completed, "mixed sentence tail completion failed");
        check(lexicon.decode("wozaibeijingshangba").front().text == "我在北京伤疤",
            "completion hid an already fully spelled sentence");

        InputSession input; type(input, boundaries, "niha");
        auto result = input.handle(InputKey::space, 0, boundaries);
        check(result.text == "你好" && result.pronunciation == "ni hao" && input.empty(), "completed word failed to confirm");
        type(input, boundaries, "niha"); input.select(index(input, "你", 2), boundaries);
        check(input.preedit() == "你ha" && find(input.candidates(), "好").input_end == 2, "partial selection consumed untyped letters");
        input.handle(InputKey::home, 0, boundaries); check(input.raw() == "niha" && input.confirmed_text().empty(), "Home restored invented spelling");
        input.clear(); type(input, boundaries, "niha"); input.handle(InputKey::letter, 'o', boundaries);
        check(!find(input.candidates(), "你好").completed, "continuing input retained a stale completion flag");
        input.handle(InputKey::backspace, 0, boundaries);
        check(find(input.candidates(), "你好").completed, "backspace did not restore tail completion");
        result = input.handle(InputKey::enter, 0, boundaries);
        check(result.text == "niha" && result.pronunciation.empty(), "raw incomplete Enter became learnable");
        type(input, boundaries, "niha"); result = input.handle(InputKey::escape, 0, boundaries);
        check(result.action == InputAction::cancel && input.empty(), "completion cancellation left input state");

        testing::TestWorkspace workspace; const auto path = default_user_path();
        LearningDictionary learning(argv[1], path); learning.refresh();
        type(input, learning.lexicon(), "niha", learning.users());
        result = input.handle(InputKey::space, 0, learning.lexicon(), learning.users());
        check(learning.remember(result) && learning.users().size() == 1 && learning.users().at({"ni hao", "你好"}) == 1,
            "completion saved incomplete pronunciation");
        LearningDictionary restarted(argv[1], path); restarted.refresh();
        for (const auto& query : {"niha", "nihao", "nh", "nha"}) {
            const auto words = restarted.lexicon().lookup(query, restarted.users());
            check(find(words, "你好").selections == 1 && find(words, "你好").pronunciation == "ni hao", "full/short/completed spellings split personal records");
        }
        type(input, restarted.lexicon(), "niha", restarted.users());
        result = input.handle(InputKey::enter, 0, restarted.lexicon(), restarted.users());
        check(!restarted.remember(result) && restarted.users().size() == 1, "raw completion was learned");
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 8; ++i) {
            auto stress = std::string(96, 's') + "ha";
            check(lexicon.decode(stress).size() <= 5, "completion exceeded bounded sentence result count");
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        check(elapsed < 5000, "completion exceeded interactive work budget");
        std::cout << "PASS: " << checks << " completion/mixed/learning checks; stress " << elapsed << " ms\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
