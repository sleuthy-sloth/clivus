// SPDX-License-Identifier: GPL-3.0-or-later
// The scanner makes two promises to a UI that runs it on another thread: it says how far it has got,
// and a cancel stops it. Both are checked against the committed corpus, so the walk has real objects
// to parse rather than the empty folders the other tests use.
#include "clivus/series_index.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: clivus_test_scan <corpus folder>\n");
        return 2;
    }
    const std::string corpus = argv[1];

    {
        clivus::ScanControl control;
        const auto series = clivus::scanDirectory(corpus, &control);
        expect(!series.empty(), "the corpus scans into at least one series");
        expect(control.examined.load() > 0, "the walk counted the entries it examined");
        expect(control.examined.load() >= series.size(), "every reported series came from an examined entry");
        expect(!control.cancel.load(), "a scan nobody cancelled leaves the flag down");
    }

    {   // Cancel already raised: the walk must not parse a single object.
        clivus::ScanControl control;
        control.cancel = true;
        const auto series = clivus::scanDirectory(corpus, &control);
        expect(series.empty(), "a scan that starts cancelled reports nothing");
        expect(control.examined.load() == 0, "a scan that starts cancelled examines nothing");
    }

    if (failures == 0) std::printf("scan: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
