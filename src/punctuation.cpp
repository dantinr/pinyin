#include "pinyin/punctuation.hpp"
#include <algorithm>
#include <string_view>

namespace pinyin {
namespace {
std::string ascii_token(const std::string& before) {
    auto start = before.size();
    while (start && static_cast<unsigned char>(before[start - 1]) > 0x20 &&
           static_cast<unsigned char>(before[start - 1]) < 0x7f) --start;
    auto token = before.substr(start);
    for (auto& letter : token) if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    return token;
}
bool url_token(const std::string& token) {
    const bool letters = std::any_of(token.begin(), token.end(), [](char ch) { return ch >= 'a' && ch <= 'z'; });
    return token.find('@') != std::string::npos ||
        (letters && token.find_first_of("./\\") != std::string::npos) ||
        token == "http:" || token == "https:" || token == "ftp:" || token == "mailto:";
}
}
bool Punctuation::supports(char ascii) noexcept {
    return std::string_view(",.?!:;\\\"'()[]{}<>_^$/").find(ascii) != std::string_view::npos;
}
PunctuationResult Punctuation::resolve(char ascii, const PunctuationContext& context) const {
    const auto token = ascii_token(context.before);
    const auto raw = ascii_token(context.preedit);
    const bool url_start = (ascii == ':' && (raw == "http" || raw == "https" || raw == "ftp" || raw == "mailto")) ||
        (ascii == '.' && raw == "www");
    const bool literal = context.literal_scope || url_token(token) || url_start;
    const bool number = !token.empty() && token.back() >= '0' && token.back() <= '9' && context.preedit.empty() &&
        (ascii == '.' || ascii == ':' || ascii == '/');
    // An English word submitted with Enter also keeps its following period.
    const bool english_period = ascii == '.' && context.preedit.empty() && !token.empty() &&
        token.back() >= 'a' && token.back() <= 'z';
    if (literal || number || english_period) return {std::string(1, ascii), literal};
    switch (ascii) {
    case ',': return {"，"};
    case '.': return {"。"};
    case '?': return {"？"};
    case '!': return {"！"};
    case ':': return {"："};
    case ';': return {"；"};
    case '\\': return {"、"};
    case '"': return {double_open_ ? "“" : "”"};
    case '\'': return {single_open_ ? "‘" : "’"};
    case '(': return {"（"};
    case ')': return {"）"};
    case '[': return {"【"};
    case ']': return {"】"};
    case '{': return {"｛"};
    case '}': return {"｝"};
    case '<': return {"《"};
    case '>': return {"》"};
    case '_': return {"——"};
    case '^': return {"……"};
    case '$': return {"￥"};
    default: return {std::string(1, ascii)};
    }
}
void Punctuation::accepted(const PunctuationResult& result) noexcept {
    if (result.text == "“" || result.text == "”") double_open_ = !double_open_;
    if (result.text == "‘" || result.text == "’") single_open_ = !single_open_;
}
}
