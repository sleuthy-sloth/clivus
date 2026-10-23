// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

// Searching the in-app manual. Kept out of the Qt window so the behaviour — case folding, markup
// stripping, excerpts — is testable without a GUI, and so the manual's HTML and the search see the
// same text.
namespace clivus::manual {

struct Article {
    std::string title;
    std::string body;   // may contain HTML; searches run on the visible text
};

// The text a reader actually sees: tags removed, entities left as-is (the manual uses few), runs of
// whitespace collapsed to single spaces.
std::string plainText(const std::string& markup);

// Trimmed and lower-cased (ASCII fold: the manual is written in English).
std::string normalize(const std::string& text);

// True when the normalized query appears in the article's title or visible body text, or when the
// query is empty (an empty search matches everything, which is what showing the full list means).
bool matches(const Article& article, const std::string& query);

// A short plain-text excerpt around the first body hit, for showing why an article matched.
// Empty when the query is empty or the body does not contain it (a title-only match has no excerpt;
// the title is already on screen).
std::string excerpt(const Article& article, const std::string& query, size_t contextChars = 60);

// Indices of the matching articles, in their original order.
std::vector<int> matchingArticles(const std::vector<Article>& articles, const std::string& query);

}  // namespace clivus::manual
