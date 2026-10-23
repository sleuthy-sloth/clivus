// SPDX-License-Identifier: GPL-3.0-or-later
// Manual search: the manual's usefulness depends on finding a phrase that appears in an article, not
// only in a chapter title. Pure logic, no Qt — the window is wired to it separately.
#include "clivus/manual_search.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

const std::vector<clivus::manual::Article> kArticles = {
    {"Getting Started", "<p>Open a folder of DICOM files with <b>Open Folder</b>.</p>"},
    {"Window/Level", "<p>Left-drag widens the window; presets are listed in the options bar.</p>"},
    {"Measurements", "<p>Left-drag draws a distance in millimetres; Shift-click three points "
                     "draws an angle.</p>"},
    {"Limitations", "<p>No volume rendering. JPEG 2000 requires the codec.</p>"},
    {"Empty", ""},
};

}  // namespace

int main()
{
    using namespace clivus::manual;

    // title match
    expect(matches(kArticles[0], "started"), "matches a word inside a title");
    // body-only phrase
    expect(matches(kArticles[1], "presets are listed"), "matches a phrase that only appears in the body");
    expect(matches(kArticles[2], "Shift-click three points"), "matches a body phrase with punctuation");
    // case-insensitive
    expect(matches(kArticles[2], "SHIFT-CLICK"), "matching is case-insensitive");
    expect(matches(kArticles[1], "window/level"), "lower-case query matches a capitalised title");
    // markup must not be searchable as text when it is invisible
    expect(!matches(kArticles[0], "<b>"), "HTML tags are not searchable text");
    expect(matches(kArticles[0], "folder"), "visible text inside tags is searchable");
    // empty / whitespace queries show everything, which is what "no filter" means
    expect(matches(kArticles[4], ""), "an empty query matches every article");
    expect(matches(kArticles[4], "   "), "a whitespace-only query matches every article");
    // no results
    expect(!matches(kArticles[3], "ultrasound"), "a phrase that is absent does not match");
    // multiple matches, order preserved
    {
        const auto hits = matchingArticles(kArticles, "drag");
        expect(hits.size() == 2, "both articles mentioning 'drag' match");
        expect(hits.size() == 2 && hits[0] == 1 && hits[1] == 2, "matching indices keep article order");
    }
    // an empty query returns every index, not a filtered subset
    expect(matchingArticles(kArticles, "").size() == kArticles.size(),
           "an empty query lists every article");
    expect(matchingArticles(kArticles, "zzz").empty(), "a no-match query returns no indices");

    // excerpts: the reader sees why it matched
    {
        const std::string e = excerpt(kArticles[2], "millimetres");
        expect(!e.empty(), "a body match produces an excerpt");
        expect(e.find("millimetres") != std::string::npos, "the excerpt contains the matched word");
    }
    // a title-only match has no body excerpt to offer
    expect(excerpt(kArticles[0], "started").empty(), "a title-only match has no excerpt");
    expect(excerpt(kArticles[2], "").empty(), "an empty query produces no excerpt");
    // markup is stripped from what the user reads
    {
        const std::string e = excerpt(kArticles[0], "folder");
        expect(e.find('<') == std::string::npos, "excerpts carry no markup");
    }
    // whitespace collapsing: "window level" (space) matches "window; presets" adjacency? no — but a
    // query spanning a newline in the source must still match
    {
        const Article multi{"X", "<p>alpha\n   beta</p>"};
        expect(matches(multi, "alpha beta"), "whitespace runs in the source collapse for searching");
    }

    if (failures == 0) std::printf("manual_search: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
