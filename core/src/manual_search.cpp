// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/manual_search.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace clivus::manual {
namespace {

std::string lowerAscii(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}

std::string trimmed(const std::string& text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

// Collapses whitespace runs, because the HTML carries newlines and indentation that a search for
// "window level" should not trip over.
void collapseSpace(std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (const char c : text) {
        const bool isSpace = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (isSpace) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    text = std::move(out);
}

}  // namespace

std::string plainText(const std::string& markup)
{
    std::string out;
    out.reserve(markup.size());
    bool inTag = false;
    for (const char c : markup) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; out.push_back(' '); continue; }   // tags separate words
        if (!inTag) out.push_back(c);
    }
    // A few entities that would otherwise read as noise in an excerpt.
    for (const auto& [from, to] : {std::pair<const char*, const char*>{"&#9656;", ">"},
                                   {"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"},
                                   {"&gt;", ">"}, {"&quot;", "\""}}) {
        std::string::size_type pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    collapseSpace(out);
    return trimmed(out);
}

std::string normalize(const std::string& text)
{
    return lowerAscii(trimmed(text));
}

bool matches(const Article& article, const std::string& query)
{
    const std::string needle = normalize(query);
    if (needle.empty()) return true;   // no query: everything is shown
    return normalize(article.title).find(needle) != std::string::npos ||
           normalize(plainText(article.body)).find(needle) != std::string::npos;
}

std::string excerpt(const Article& article, const std::string& query, const size_t contextChars)
{
    const std::string needle = normalize(query);
    if (needle.empty()) return {};
    const std::string text = plainText(article.body);
    const std::string haystack = normalize(text);
    const size_t hit = haystack.find(needle);
    if (hit == std::string::npos) return {};   // title-only match: the title is already visible

    size_t begin = hit > contextChars ? hit - contextChars : 0;
    size_t end = std::min(text.size(), hit + needle.size() + contextChars);
    // Do not start or end mid-word: the excerpt is read by a person.
    while (begin > 0 && std::isalpha(static_cast<unsigned char>(text[begin - 1]))) --begin;
    while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) ++end;

    std::string out = text.substr(begin, end - begin);
    if (begin > 0) out = "…" + out;
    if (end < text.size()) out += "…";
    return out;
}

std::vector<int> matchingArticles(const std::vector<Article>& articles, const std::string& query)
{
    std::vector<int> out;
    for (size_t i = 0; i < articles.size(); ++i)
        if (matches(articles[i], query)) out.push_back(int(i));
    return out;
}

}  // namespace clivus::manual
