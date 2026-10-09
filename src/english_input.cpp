#include "pinyin/english_input.hpp"
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace pinyin {
namespace {
const std::unordered_set<std::string>& words() {
    // Manually curated for this project, not imported from a third-party
    // dictionary. These seed words recognize English; subsequent words in
    // the temporary English segment are passed directly to the application.
    static const auto result = [] {
        std::istringstream stream(
            "about above accept account across action active actually add address after again against age agree all almost already also although always "
            "am among amount another answer anyone anything anyway appear apple application approach april area aren't around arrive article ask asked asking at august available away "
            "baby back bad based basic beautiful because become before begin behind believe below best better between big birthday black blue book both box break bring brother build business buy "
            "call called can't cannot care case change check child children choice choose city class clear close code coffee cold come comes coming common company complete computer consider continue control could couldn't country course create current customer "
            "data date daughter day dear december decide define definitely design detail develop didn't different dinner discuss doesn't doing done don't down drive during "
            "each early easy eat either else email end enjoy enough enter error especially even ever every everybody everyone everything example excellent except expect explain "
            "face fact family far fast father favorite february feel few field file finally find fine first five follow food for forgot form found four free friend friends friday from front full function future "
            "game general get give given glad go going gone good got great green group grow guess "
            "had hadn't half hand happen happy hard hasn't have haven't having head hear heard hello help helpful here here's high history hold home hope hot hotel hour house how however hundred hungry "
            "i he she you we us me be is it in on as by do hi ok i'm i'd i'll i've idea important improve include includes including information inside instead interest interesting internet into invite issue isn't it's item "
            "january job join july june just keep key kind know known language large last late later latest laugh learn least leave left less let's letter level life light like likely line link list little live local long look looks lost lot love low lunch "
            "made main make makes making manage many march market matter maybe mean meeting member message method might million minute missing model monday money month more morning most mother move much must myself "
            "name near necessary need never new news next nice night nobody normal north note nothing notice november now number "
            "october of off offer office often okay old once one only open operation option order other our ours ourselves out outside over own "
            "page paper parent part party password past people perfect perhaps person phone picture place plan please point possible power practice prefer present pretty private probably problem process product project provide public pull push put "
            "question quick quite read ready really reason receive recent record red remember remove reply report request require rest result return review right room run running "
            "said same saturday save say school second section security see seems self send sense sent september server service set several shall share short should shouldn't show side simple since sister sit size sleep small so some somebody someone something sometimes soon sorry sound south speak special start state stay step still stop store story street strong student study such summer sunday support sure system "
            "table take talk task team tell test text than thank thanks that that's the their theirs them themselves then there there's these they they're they've thing things think third this those though thought thousand three through thursday time today together tomorrow too took tool top total travel tree true try tuesday turn two type "
            "under understand until update upload upon use used useful user using usual usually "
            "value very video view visit voice wait walk want warm watch water way we've wednesday week welcome well went were weren't what what's when where which while white who who's whole why will window with within without won't word work working world would wouldn't write wrong "
            "year yellow yes yesterday yet you'll you're you've young your yours yourself zero");
        std::unordered_set<std::string> collected;
        for (std::string word; stream >> word;) collected.insert(std::move(word));
        return collected;
    }();
    return result;
}
}
bool starts_english(const std::string& word, const Lexicon& lexicon, const UserDictionary& users) {
    auto spelling = word;
    for (auto& letter : spelling) if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    if (!words().count(spelling)) return false;
    // A capitalized English word is explicit Latin input (Shift/CapsLock),
    // unlike lowercase you/he/she which can be ordinary pinyin.
    if (!word.empty() && word.front() >= 'A' && word.front() <= 'Z') return true;
    // he/she/you/can/an/men/... remain pinyin even without a corresponding
    // entry in a small or customized dictionary.
    try { normalize_pronunciation(spelling); return false; }
    catch (const std::invalid_argument&) {}
    const auto exact = lexicon.lookup(spelling, users, 1);
    return exact.empty() || exact.front().abbreviations != 0;
}
}
