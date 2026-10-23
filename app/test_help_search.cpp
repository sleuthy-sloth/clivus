// SPDX-License-Identifier: GPL-3.0-or-later
// The manual's search must find phrases that only appear in an article's text — that was the whole
// complaint behind it. This drives the real HelpWindow offscreen (no GL involved) and asserts what a
// reader sees: which chapters remain, the no-results state, and the return to the full list.
//
// Run: QT_QPA_PLATFORM=offscreen ./app/clivus_test_help_search

#include "help_window.h"

#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <cstdio>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            std::exit(1);                                                  \
        }                                                                  \
    } while (false)

namespace {

int visibleRows(const QListWidget* nav)
{
    int count = 0;
    for (int i = 0; i < nav->count(); ++i) {
        const QListWidgetItem* item = nav->item(i);
        if (item->text().startsWith(QStringLiteral("(no matching"))) continue;
        if (!item->isHidden()) ++count;
    }
    return count;
}

bool noResultsShown(const QListWidget* nav)
{
    for (int i = 0; i < nav->count(); ++i)
        if (nav->item(i)->text().startsWith(QStringLiteral("(no matching")))
            return !nav->item(i)->isHidden();
    return false;
}

void type(QLineEdit* search, QListWidget* nav, const QString& text)
{
    search->setText(text);
    QApplication::processEvents();
    (void)nav;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("sleuthy-sloth"));
    QCoreApplication::setApplicationName(QStringLiteral("clivus-help-test"));
    QApplication app(argc, argv);
    QSettings().clear();
    qputenv("CLIVUS_CACHE_DIR",
            (QDir::tempPath() + QStringLiteral("/clivus-help-test-cache")).toUtf8());

    HelpWindow help;
    help.show();
    const auto lists = help.findChildren<QListWidget*>();
    const auto searches = help.findChildren<QLineEdit*>();
    CHECK(lists.size() == 1);
    CHECK(searches.size() == 1);
    QListWidget* nav = lists.first();
    QLineEdit* search = searches.first();

    const int allRows = visibleRows(nav);
    CHECK(allRows >= 10);   // the manual has a dozen chapters; a broken count means a broken window

    // (a) an empty query shows everything
    type(search, nav, QString());
    CHECK(visibleRows(nav) == allRows);
    CHECK(!noResultsShown(nav));

    // (b) a phrase that only exists in an article body: "millimetres" appears in Measuring, not in
    // any chapter title.
    type(search, nav, QStringLiteral("millimetres"));
    const int bodyMatches = visibleRows(nav);
    CHECK(bodyMatches >= 1);
    CHECK(bodyMatches < allRows);
    CHECK(!noResultsShown(nav));

    // (c) case-insensitive
    type(search, nav, QStringLiteral("MILLIMETRES"));
    CHECK(visibleRows(nav) == bodyMatches);

    // (d) whitespace-only behaves like empty
    type(search, nav, QStringLiteral("   "));
    CHECK(visibleRows(nav) == allRows);

    // (e) no results shows the marker and hides every chapter
    type(search, nav, QStringLiteral("ultrasound elastography"));
    CHECK(visibleRows(nav) == 0);
    CHECK(noResultsShown(nav));

    // (f) clearing restores the full list and hides the marker
    type(search, nav, QString());
    CHECK(visibleRows(nav) == allRows);
    CHECK(!noResultsShown(nav));

    QSettings().clear();
    std::printf("help search: all checks passed (%d chapters)\n", allRows);
    return 0;
}
