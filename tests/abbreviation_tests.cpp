#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
const pinyin::Candidate& find(const std::vector<pinyin::Candidate>& words, const std::string& text) {
    const auto found = std::find_if(words.begin(), words.end(), [&](const auto& word) { return word.text == text; });
    if (found == words.end()) throw std::runtime_error("missing candidate: " + text);
    return *found;
}
std::size_t index_of(const pinyin::InputSession& input, const std::string& text, std::size_t end) {
    for (std::size_t i = 0; i < input.candidates().size(); ++i)
        if (input.candidates()[i].text == text && input.candidates()[i].input_end == end) return i;
    throw std::runtime_error("missing segment: " + text);
}
void type(pinyin::InputSession& input, const pinyin::Lexicon& lexicon, const std::string& text,
          const pinyin::UserDictionary& users = {}) {
    for (const auto ch : text)
        input.handle(ch == '\'' ? pinyin::InputKey::separator : pinyin::InputKey::letter, ch, lexicon, users);
}
}

int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin;
    try {
        if (argc != 2) throw std::runtime_error("expected dictionary");
        Lexicon lexicon; lexicon.load(argv[1]);
        struct Example { const char* query; const char* text; const char* reading; std::size_t abbreviations; };
        for (const auto& example : {
            Example{"nh", "你好", "ni hao", 2}, {"nhao", "你好", "ni hao", 1},
            {"nih", "你好", "ni hao", 1}, {"nihao", "你好", "ni hao", 0},
            {"n'h", "你好", "ni hao", 2}, {" NH ", "你好", "ni hao", 2},
            {"N H", "你好", "ni hao", 2}, {"bj", "北京", "bei jing", 2},
            {"beij", "北京", "bei jing", 1}, {"bjing", "北京", "bei jing", 1},
            {"zg", "中国", "zhong guo", 2}, {"zhg", "中国", "zhong guo", 2},
            {"zguo", "中国", "zhong guo", 1}, {"zhongg", "中国", "zhong guo", 1},
            {"zh'g", "中国", "zhong guo", 2}, {"cq", "重庆", "chong qing", 2},
            {"chq", "重庆", "chong qing", 2}, {"chongq", "重庆", "chong qing", 1},
            {"sh", "上海", "shang hai", 2}, {"shh", "上海", "shang hai", 2},
            {"shhai", "上海", "shang hai", 1}, {"shangh", "上海", "shang hai", 1},
            {"ls", "绿色", "lv se", 2}, {"lvs", "绿色", "lv se", 1},
            {"x'a", "西安", "xi an", 2}}) {
            const auto words = lexicon.lookup(example.query, {}, std::numeric_limits<std::size_t>::max());
            const auto& word = find(words, example.text);
            check(word.pronunciation == example.reading && word.abbreviations == example.abbreviations &&
                word.input_end == normalize_query(example.query).size() && !word.synthesized,
                "short/mixed spelling lost canonical pronunciation or input offsets");
            std::set<std::string> unique;
            for (const auto& candidate : words) check(unique.insert(candidate.text).second, "duplicate abbreviation candidate");
        }
        check(lexicon.lookup("nh").front().text == "你好", "common abbreviated word ranking failed");
        check(lexicon.lookup("bj").front().text == "北京", "Beijing abbreviation ranking failed");
        check(lexicon.lookup("nh", {}, 0).empty() && lexicon.lookup("nh", {}, 1).size() == 1, "short query limit failed");

        Lexicon priority;
        priority.add({"安", "an", 1}); priority.add({"爱你", "ai ni", 1000000000});
        UserDictionary personal; personal[{"ai ni", "爱你"}] = 100;
        check(priority.lookup("an", personal).front().text == "安", "abbreviation displaced exact full spelling");
        check(priority.lookup("a'n", personal).front().text == "爱你", "explicit initial boundary failed");
        Lexicon full_sentence;
        full_sentence.add({"阿", "a", 10000}); full_sentence.add({"嗯", "n", 10000});
        full_sentence.add({"爱你", "ai ni", 1000000000});
        check(full_sentence.decode("an", {}, 1).front().text == "阿嗯" &&
            full_sentence.lookup_composition("an").front().text == "阿嗯",
            "stored abbreviation displaced a fully spelled sentence");
        Lexicon single;
        single.add({"啊", "a", 1}); single.add({"嗯", "n", 1});
        check(single.lookup("a").size() == 1 && single.lookup("a").front().abbreviations == 0 &&
            single.lookup("n").front().abbreviations == 0, "single-letter full syllable became an abbreviation");
        Lexicon repeated;
        repeated.add({"啊爱", "a ai", 1}); repeated.add({"爱啊", "ai a", 1});
        check(repeated.lookup("aai").front().text == "啊爱" && repeated.lookup("aai").front().abbreviations == 0,
            "multiple segmentation routes lost the best full spelling");
        Lexicon partial; partial.add({"你好", "ni hao", 1}); partial.add({"北京", "bei jing", 1});
        check(partial.lookup("niha").front().text == "你好" && partial.lookup("beiji").front().text == "北京",
            "tail completion lost mixed-spelling support");
        for (const auto& query : {"nvi", "nhv", "nhi"})
            check(lexicon.decode(query).empty() && lexicon.lookup_composition(query).empty(), "invalid initial created a candidate");

        const std::string sentence = "我在北京上班", reading = "wo zai bei jing shang ban";
        for (const auto& example : {Example{"wzaibjshangban", sentence.c_str(), reading.c_str(), 3},
                                   {"wozaibjshangb", sentence.c_str(), reading.c_str(), 3}}) {
            const auto words = lexicon.decode(example.query);
            check(!words.empty() && words.front().text == sentence && words.front().pronunciation == reading &&
                words.front().synthesized && words.front().abbreviations == example.abbreviations &&
                words.front().input_end == std::string(example.query).size(), "abbreviated sentence failed");
        }
        check(find(lexicon.lookup("chq", {}, 100), "重庆").pronunciation == "chong qing", "short query invented a polyphonic reading");

        InputSession input;
        type(input, lexicon, "nhbj"); input.select(index_of(input, "你", 1), lexicon);
        check(input.preedit() == "你hbj", "initial selection consumed an unselected syllable");
        input.select(index_of(input, "好", 1), lexicon);
        check(input.preedit() == "你好bj", "second initial could not be selected independently");
        auto result = input.select(index_of(input, "北京", 2), lexicon);
        check(result.action == InputAction::commit && result.text == "你好北京" && result.pronunciation == "ni hao bei jing" &&
            input.empty(), "short segmented phrase did not commit with canonical reading");
        type(input, lexicon, "n'hbj"); input.select(index_of(input, "你", 2), lexicon);
        check(input.preedit() == "你hbj", "initial separator consumption failed");
        input.select(index_of(input, "好", 1), lexicon); input.handle(InputKey::home, 0, lexicon);
        check(input.raw() == "n'hbj" && input.confirmed_text().empty() && input.cursor() == 0,
            "Home did not restore original abbreviated spelling");
        input.clear(); type(input, lexicon, "nh"); input.handle(InputKey::backspace, 0, lexicon);
        check(input.raw() == "n" && find(input.candidates(), "你").input_end == 1, "abbreviated backspace left stale candidates");
        input.handle(InputKey::letter, 'h', lexicon); input.handle(InputKey::home, 0, lexicon);
        input.handle(InputKey::right, 0, lexicon); input.handle(InputKey::letter, 'i', lexicon);
        check(input.raw() == "nih" && input.candidates().front().text == "你好", "editing short/full mix failed");
        result = input.handle(InputKey::enter, 0, lexicon);
        check(result.text == "nih" && result.pronunciation.empty(), "raw abbreviation Enter became learnable");
        type(input, lexicon, "nh"); result = input.handle(InputKey::escape, 0, lexicon);
        check(result.action == InputAction::cancel && result.pronunciation.empty() && input.empty(), "cancelled abbreviation became learnable");

        testing::TestWorkspace workspace;
        const auto user_path = default_user_path();
        LearningDictionary learning(argv[1], user_path); learning.refresh();
        type(input, learning.lexicon(), "nhbj", learning.users());
        input.select(index_of(input, "你", 1), learning.lexicon(), learning.users());
        input.select(index_of(input, "好", 1), learning.lexicon(), learning.users());
        result = input.select(index_of(input, "北京", 2), learning.lexicon(), learning.users());
        check(learning.remember(result) && learning.users().size() == 1 &&
            learning.users().at({"ni hao bei jing", "你好北京"}) == 1, "short phrase was not learned as full pronunciation");
        LearningDictionary restarted(argv[1], user_path); restarted.refresh();
        for (const auto& query : {"nhbj", "nihaobeijing", "nihaobj", "nhaobeijing"}) {
            const auto words = restarted.lexicon().lookup(query, restarted.users());
            check(!words.empty() && words.front().text == "你好北京" && words.front().pronunciation == "ni hao bei jing" &&
                words.front().selections == 1, "restart did not share a learned word between short and full spelling");
        }
        type(input, restarted.lexicon(), "nhbj", restarted.users());
        result = input.handle(InputKey::space, 0, restarted.lexicon(), restarted.users());
        check(restarted.remember(result) && restarted.users().size() == 1 &&
            restarted.users().at({"ni hao bei jing", "你好北京"}) == 2, "short/full selections created separate user records");
        type(input, restarted.lexicon(), "nh", restarted.users());
        result = input.handle(InputKey::enter, 0, restarted.lexicon(), restarted.users());
        check(!restarted.remember(result) && restarted.users().size() == 1, "raw short text was persisted");
        // Initials alone have many homophones. Confirming the intended segments
        // teaches a full sentence, which then becomes a direct abbreviated word.
        type(input, restarted.lexicon(), "wzbjsb", restarted.users());
        input.select(index_of(input, "我", 1), restarted.lexicon(), restarted.users());
        input.select(index_of(input, "在", 1), restarted.lexicon(), restarted.users());
        input.select(index_of(input, "北京", 2), restarted.lexicon(), restarted.users());
        result = input.select(index_of(input, "上班", 2), restarted.lexicon(), restarted.users());
        check(result.text == sentence && result.pronunciation == reading && restarted.remember(result),
            "initial sentence could not be corrected and learned");
        LearningDictionary sentence_restart(argv[1], user_path); sentence_restart.refresh();
        const auto short_sentence = sentence_restart.lexicon().decode("wzbjsb", sentence_restart.users());
        check(short_sentence.front().text == sentence && short_sentence.front().pronunciation == reading &&
            short_sentence.front().abbreviations == 6 && !short_sentence.front().synthesized,
            "learned full sentence was not reused by its initials");

        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 8; ++i) check(lexicon.decode(std::string(96, 's')).size() <= 5, "ambiguous initials exceeded result limit");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        check(elapsed < 5000, "ambiguous initials exceeded the bounded work budget");
        std::cout << "PASS: " << checks << " abbreviation/mixed/learning checks; stress " << elapsed << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
