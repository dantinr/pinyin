#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary);
    if (!(stream << data)) throw std::runtime_error("cannot write fixture");
}
void type(pinyin::InputSession& input, const pinyin::LearningDictionary& dictionary, const char* spelling) {
    while (*spelling) input.handle(pinyin::InputKey::letter, *spelling++, dictionary.lexicon(), dictionary.users());
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto base = workspace.root / L"base.tsv", path = default_user_path();
        write(base, "如\tru\t20\n入\tru\t10\n何\the\t20\n和\the\t10\n");
        check(ime_learning_enabled(path) && !std::filesystem::exists(path.parent_path()),
            "fresh settings did not default to learning or created storage during lookup");
        check(!ime_learning_enabled({}), "missing user path enabled local learning");
        LearningDictionary dictionary(base, path);
        dictionary.refresh();
        check(dictionary.enabled() && dictionary.available() && dictionary.users().empty() && !std::filesystem::exists(path.parent_path()),
            "default local learning failed or invented word records");
        const InputResult confirmed{InputAction::commit, "如何", "ru he"};
        InputSession input; type(input, dictionary, "ruhe");
        const auto prefix = std::find_if(input.candidates().begin(), input.candidates().end(),
            [](const auto& word) { return word.text == "如"; });
        if (prefix == input.candidates().end()) throw std::runtime_error("missing partial selection fixture");
        auto result = input.select(static_cast<std::size_t>(prefix - input.candidates().begin()),
            dictionary.lexicon(), dictionary.users());
        check(result.action == InputAction::update && input.preedit() == "如he", "partial selection regression");
        check(!dictionary.remember(result) && !std::filesystem::exists(path), "partial word was persisted");
        result = input.handle(InputKey::space, 0, dictionary.lexicon(), dictionary.users());
        check(result.text == "如何" && dictionary.remember(result), "complete selected phrase was not learned");
        check(dictionary.lexicon().lookup("ruhe", dictionary.users()).front().text == "如何", "learned phrase was not immediately queryable");
        check(read(path).find("如何\tru he\t1") != std::string::npos && read(path).find("ruhe") == std::string::npos,
            "user record contains raw input or incorrect pronunciation/count");
        LearningDictionary restarted(base, path); restarted.refresh();
        type(input, restarted, "ruhe");
        check(input.candidates().front().text == "如何", "restart did not promote whole-word matching");
        result = input.handle(InputKey::space, 0, restarted.lexicon(), restarted.users());
        check(restarted.remember(result) && restarted.users().at({"ru he", "如何"}) == 2, "repeated use duplicated a word instead of increasing its count");
        // Confirmed custom candidates can contain mixed text, rare characters,
        // or a phrase expanded from a shorter reading.
        for (const auto& custom : {Entry{"Git项目", "ji xiang mu", 1}, Entry{"𠮷", "ji", 1},
                                  Entry{"常用地址和联系方式", "di zhi", 1}}) {
            auto lexicon = dictionary.lexicon(); lexicon.add(custom);
            auto spelling = normalize_query(custom.pronunciation);
            for (auto ch : spelling) input.handle(ch == '\'' ? InputKey::separator : InputKey::letter, ch, lexicon);
            result = input.handle(InputKey::space, 0, lexicon);
            check(result.text == custom.text && dictionary.remember(result) &&
                dictionary.lexicon().lookup(spelling, dictionary.users()).front().text == custom.text,
                "confirmed custom candidate was rejected by automatic learning");
        }
        const auto saved = read(path);
        for (const auto& ignored : {
            InputResult{InputAction::commit, "ruhe", {}}, InputResult{InputAction::cancel, "如何", "ru he"},
            InputResult{InputAction::update, "如", "ru"}, InputResult{InputAction::commit, "如何", "invalid"},
            InputResult{InputAction::commit, "如\t何", "ru he"}})
            check(!dictionary.remember(ignored) && read(path) == saved, "unconfirmed or malformed input was learned");
        type(input, dictionary, "ruhe");
        result = input.handle(InputKey::enter, 0, dictionary.lexicon(), dictionary.users());
        check(!dictionary.remember(result) && read(path) == saved, "raw Enter submission became a custom word");

        {
            const auto phrase_base = workspace.root / L"phrases.tsv";
            const auto phrase_path = workspace.root / L"phrases" / L"words.user.tsv";
            write(phrase_base, "同意\ttong yi\t8000\n后台\thou tai\t8000\n重庆\tchong qing\t8000\n");
            {
                UserStore store(phrase_path);
                store.save({{{"tong yi hou tai", "统一后台"}, 7},
                    {{"tong yi she zhi", "统一设置"}, 2},
                    {{"tong yi hou", "统一后台"}, 1},
                    {{"tong yi hou tai", "统一AB"}, 1},
                    {{"zhong qing hou tai", "重庆后台"}, 1},
                    {{"ji ye hou tai", "𠮷野后台"}, 1}});
            }
            LearningDictionary phrases(phrase_base, phrase_path); phrases.refresh();
            InputSession phrase_input; type(phrase_input, phrases, "tongyi");
            const auto fragment = std::find_if(phrase_input.candidates().begin(), phrase_input.candidates().end(),
                [](const auto& word) { return word.text == "统一"; });
            check(fragment != phrase_input.candidates().end(),
                "a confirmed longer phrase did not recover its missing short word");
            check(fragment->pronunciation == "tong yi" && fragment->selections == 0 &&
                std::count_if(phrase_input.candidates().begin(), phrase_input.candidates().end(),
                    [](const auto& word) { return word.text == "统一"; }) == 1,
                "phrase fallback fabricated selections, reading or duplicate candidates");
            check(phrase_input.candidates().front().text == "同意",
                "an inferred phrase fragment displaced a stored exact word");
            const auto before_lookup = read(phrase_path);
            const auto separated = phrases.lexicon().lookup("tong'yi", phrases.users());
            check(std::any_of(separated.begin(), separated.end(), [](const auto& word) { return word.text == "统一"; }),
                "explicit syllable boundaries hid a phrase fragment");
            check(read(phrase_path) == before_lookup && !phrases.users().count({"tong yi", "统一"}),
                "looking up a phrase fragment wrote invented user records");
            const UserDictionary unaligned{{{"tong yi hou", "统一后台"}, 1},
                {{"tong yi hou", "统一A"}, 1}, {{"tong yi hou", "统一😀"}, 1}};
            const auto invalid_fragments = phrases.lexicon().lookup("tongyi", unaligned);
            check(std::none_of(invalid_fragments.begin(), invalid_fragments.end(),
                [](const auto& word) { return word.text == "统一"; }),
                "mixed text or mismatched syllables produced a guessed fragment reading");
            const auto middle = phrases.lexicon().lookup("yehou", phrases.users());
            check(std::any_of(middle.begin(), middle.end(), [](const auto& word) { return word.text == "野后"; }),
                "phrase fallback only searched the start of a stored phrase");
            const auto incomplete = phrases.lexicon().lookup("tongy", phrases.users());
            check(std::none_of(incomplete.begin(), incomplete.end(), [](const auto& word) { return word.text == "统一"; }),
                "phrase fallback guessed a short word from an unfinished syllable");
            const auto rare = phrases.lexicon().lookup_composition("jiye", phrases.users());
            check(std::any_of(rare.begin(), rare.end(), [](const auto& word) { return word.text == "𠮷野"; }),
                "supplementary-plane Han characters broke phrase alignment");
            const auto wrong_reading = phrases.lexicon().lookup_composition("zhongqing", phrases.users());
            check(std::none_of(wrong_reading.begin(), wrong_reading.end(), [](const auto& word) { return word.text == "重庆"; }),
                "phrase fallback invented a conflicting known pronunciation");
            result = phrase_input.select(static_cast<std::size_t>(fragment - phrase_input.candidates().begin()),
                phrases.lexicon(), phrases.users());
            check(result.text == "统一" && phrases.remember(result), "recovered short word could not be confirmed and learned");
            LearningDictionary next_app(phrase_base, phrase_path); next_app.refresh();
            check(next_app.users().at({"tong yi", "统一"}) == 1 &&
                next_app.lexicon().lookup_composition("tongyi", next_app.users()).front().text == "统一",
                "confirmed short word was lost after restarting or inherited the full phrase count");
            set_ime_learning(phrase_path, false); next_app.refresh();
            const auto disabled = next_app.lexicon().lookup_composition("jiye", next_app.users());
            check(std::none_of(disabled.begin(), disabled.end(), [](const auto& word) { return word.text == "𠮷野"; }),
                "disabled learning exposed a personal phrase fragment");

            const auto segments_path = workspace.root / L"segments" / L"words.user.tsv";
            LearningDictionary segments(phrase_base, segments_path); segments.refresh();
            auto choices = segments.lexicon(); choices.add({"统一", "tong yi", 1});
            InputSession segmented;
            for (const auto letter : std::string("tongyihoutai")) segmented.handle(InputKey::letter, letter, choices);
            const auto prefix = std::find_if(segmented.candidates().begin(), segmented.candidates().end(),
                [](const auto& word) { return word.text == "统一"; });
            check(prefix != segmented.candidates().end(), "missing selected-word fixture");
            result = segmented.select(static_cast<std::size_t>(prefix - segmented.candidates().begin()), choices);
            check(result.action == InputAction::update && !segments.remember(result) && !std::filesystem::exists(segments_path),
                "a selected segment was persisted before the composition committed");
            result = segmented.handle(InputKey::space, 0, choices);
            check(result.text == "统一后台" && segments.remember(result), "segmented phrase did not commit");
            LearningDictionary reloaded(phrase_base, segments_path); reloaded.refresh();
            check(reloaded.users().at({"tong yi", "统一"}) == 1 &&
                reloaded.users().at({"hou tai", "后台"}) == 1 &&
                reloaded.users().at({"tong yi hou tai", "统一后台"}) == 1,
                "committing a phrase discarded independently confirmed words or counted them twice");
            const auto before_cancel = read(segments_path);
            for (const auto letter : std::string("tongyihoutai")) segmented.handle(InputKey::letter, letter, choices);
            const auto selected = std::find_if(segmented.candidates().begin(), segmented.candidates().end(),
                [](const auto& word) { return word.text == "统一"; });
            segmented.select(static_cast<std::size_t>(selected - segmented.candidates().begin()), choices);
            result = segmented.handle(InputKey::escape, 0, choices);
            check(!segments.remember(result) && result.words.empty() && read(segments_path) == before_cancel,
                "cancelling a selected segment changed the personal dictionary");
        }

        {
            const auto retry_path = workspace.root / L"retry" / L"words.user.tsv";
            LearningDictionary retry(base, retry_path); retry.refresh();
            const InputResult embedded{InputAction::commit, "嵌入", "qian ru"};
            {
                UserStore held(retry_path);
                check(!retry.remember(embedded) && !std::filesystem::exists(retry_path),
                    "busy dictionary was overwritten by a queued word");
                const auto candidates = retry.lexicon().lookup_composition("qianru", retry.users());
                check(!candidates.empty() && candidates.front().text == "嵌入",
                    "a temporary write failure hid a newly confirmed personal word");
                check(!retry.remember(embedded), "a second queued selection bypassed the writer lock");
                retry.refresh();
                check(!retry.available() && retry.users().at({"qian ru", "嵌入"}) == 2,
                    "retrying while busy lost or duplicated pending selections");
            }
            {
                UserStore other(retry_path);
                other.save({{{"qian ru", "迁入"}, 3}});
            }
            retry.refresh();
            const auto saved_retry = read_user_dictionary(retry_path);
            check(retry.available() && saved_retry.at({"qian ru", "嵌入"}) == 2 &&
                saved_retry.at({"qian ru", "迁入"}) == 3,
                "pending selections were not merged with another application's additions");
            retry.refresh();
            check(read_user_dictionary(retry_path) == saved_retry,
                "a successful retry was applied more than once");
            const auto held_file = CreateFileW(retry_path.c_str(), GENERIC_READ, 0, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            check(held_file != INVALID_HANDLE_VALUE, "cannot hold dictionary read fixture");
            retry.refresh();
            const bool retained = !retry.available() && retry.users() == saved_retry &&
                !retry.lexicon().lookup("qianru", retry.users()).empty();
            CloseHandle(held_file);
            check(retained, "a temporary read failure cleared previously loaded personal words");
            retry.refresh();
            check(retry.available() && retry.users() == saved_retry,
                "dictionary did not recover after a temporary read failure");
            {
                UserStore held(retry_path);
                check(!retry.remember(embedded), "privacy fixture unexpectedly saved a queued selection");
                set_ime_learning(retry_path, false); retry.refresh();
                check(retry.users().empty() && retry.lexicon().lookup("qianru").empty(),
                    "disabling learning retained pending personal candidates");
            }
            set_ime_learning(retry_path, true); retry.refresh();
            check(retry.users() == saved_retry && read_user_dictionary(retry_path) == saved_retry,
                "re-enabling learning saved a selection discarded by the privacy switch");
        }

        {
            // A queued local save must not hide another application's already
            // published words while that application still owns the write lock.
            const auto busy_path = workspace.root / L"pending-reader" / L"words.user.tsv";
            LearningDictionary observer(base, busy_path); observer.refresh();
            const InputResult local{InputAction::commit, "嵌入", "qian ru"};
            {
                UserStore publisher(busy_path);
                check(!observer.remember(local), "pending reader fixture unexpectedly saved");
                publisher.save({{{"ming ling", "命令"}, 2}, {{"qian ru", "嵌入"}, 3}});
                for (int attempt = 0; attempt < 2; ++attempt) {
                    observer.refresh();
                    const auto words = observer.lexicon().lookup_composition("mingling", observer.users());
                    check(!observer.available() && !words.empty() && words.front().text == "命令",
                        "a queued save hid published personal words behind another application's write lock");
                    check(observer.users().at({"qian ru", "嵌入"}) == 4 &&
                        read_user_dictionary(busy_path).at({"qian ru", "嵌入"}) == 3,
                        "read-only refresh lost, duplicated or persisted pending selections");
                }
            }
            observer.refresh(); observer.refresh();
            const auto published = read_user_dictionary(busy_path);
            check(observer.available() && published.at({"qian ru", "嵌入"}) == 4 &&
                published.at({"ming ling", "命令"}) == 2 && observer.users() == published,
                "retry failed to persist exactly the pending delta after the lock was released");
        }

        // Two independent caches use separate kernel file handles, like two apps.
        std::atomic<unsigned> failures{0};
        LearningDictionary first_client(base, path), second_client(base, path);
        auto writer = [&](LearningDictionary& client, const char* text) {
            try {
                for (int i = 0; i < 8; ++i)
                    client.remember({InputAction::commit, text, "ru he"});
            } catch (...) { ++failures; }
        };
        std::thread first([&] { writer(first_client, "入何"); });
        std::thread second([&] { writer(second_client, "入和"); });
        first.join(); second.join();
        // A busy write may queue a selection. Once contention ends, the next
        // input refresh must publish every pending count exactly once.
        first_client.refresh(); second_client.refresh();
        UserDictionary merged;
        { UserStore store(path); merged = store.load(); }
        check(failures == 0 && first_client.available() && second_client.available() &&
            merged.at({"ru he", "入何"}) == 8 && merged.at({"ru he", "入和"}) == 8 &&
            merged.at({"ru he", "如何"}) == 2, "concurrent learning lost an application's updates");
        dictionary.refresh(); check(dictionary.users() == merged, "next composition did not reload another application's words");
        // Another application can still hold its write transaction after the
        // complete dictionary has been published by atomic replacement.
        const auto shared_path = workspace.root / L"shared" / L"words.user.tsv";
        LearningDictionary observer(base, shared_path); observer.refresh();
        {
            UserStore writer(shared_path);
            UserDictionary published{{{"bian ji", "编缉"}, 1}};
            writer.save(published);
            observer.refresh();
            auto candidates = observer.lexicon().lookup_composition("bianji", observer.users());
            check(observer.available() && !candidates.empty() && candidates.front().text == "编缉",
                "another application's write lock hid an already published learned word");
            LearningDictionary newcomer(base, shared_path); newcomer.refresh();
            check(newcomer.available() && newcomer.users() == published,
                "a newly activated application could not read a published dictionary during a write transaction");
            published[{"ce shi", "测试"}] = 1; writer.save(published);
            observer.refresh();
            check(observer.users() == published, "a running application missed the next atomic dictionary replacement");

            std::atomic<unsigned> reader_errors{0};
            std::string reader_error, writer_error;
            std::thread reader([&] {
                for (int i = 0; i < 80; ++i) {
                    try {
                        const auto snapshot = read_user_dictionary(shared_path);
                        if (snapshot.size() != 2 || snapshot.at({"bian ji", "编缉"}) != 1 ||
                            !snapshot.at({"ce shi", "测试"})) {
                            ++reader_errors; reader_error = "incomplete snapshot";
                        }
                    } catch (const std::exception& error) { ++reader_errors; reader_error = error.what(); }
                }
            });
            unsigned writer_errors = 0;
            for (int i = 0; i < 40; ++i) {
                try { ++published[{"ce shi", "测试"}]; writer.save(published); }
                catch (const std::exception& error) { ++writer_errors; writer_error = error.what(); }
            }
            reader.join();
            const auto failure = "concurrent snapshot reads/publication failed: reader=" + reader_error +
                "; writer=" + writer_error;
            check(reader_errors == 0 && writer_errors == 0, failure.c_str());
        }
        set_ime_learning(path, false); dictionary.refresh();
        check(!dictionary.enabled() && dictionary.users().empty() && dictionary.lexicon().lookup("ruhe").empty(),
            "disabling learning kept personal words in the active lexicon");
        check(!dictionary.remember(confirmed) && read(path).find("入何") != std::string::npos, "turning learning off deleted existing records");
        LearningDictionary disabled_restart(base, path); disabled_restart.refresh();
        check(!disabled_restart.enabled() && disabled_restart.users().empty() && !disabled_restart.remember(confirmed),
            "restart forgot an explicit opt-out");
        set_ime_learning(path, true);
        dictionary.refresh();
        check(!std::filesystem::exists(ime_learning_flag(path)) && dictionary.enabled() && dictionary.users() == merged,
            "enabling learning did not restore the default or reload existing words");
        {
            UserStore held(path); const auto previous = read(path);
            check(!dictionary.remember(confirmed) && read(path) == previous, "busy dictionary was overwritten");
        }
        write(path, "如何\tru he\tinvalid\n"); const auto corrupt = read(path);
        dictionary.refresh();
        check(!dictionary.available() && !dictionary.lexicon().lookup("ruhe").empty(),
            "corrupt user data cleared previously loaded personal words");
        check(!dictionary.remember(confirmed) && read(path) == corrupt, "learning replaced corrupt personal data");
        { UserStore store(path); store.save({}); }
        dictionary.refresh();
        check(dictionary.available(), "a successful reload did not restore learning availability");
        check(dictionary.remember(confirmed), "learning did not recover after user data repair");
        std::cout << "PASS: " << checks << " local learning/privacy/concurrency checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
