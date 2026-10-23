// SPDX-License-Identifier: GPL-3.0-or-later
#include "help_window.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

// Skeleton only (roadmap 5.2): the chapter list follows the recommended outline, and each article is
// an explicit stub saying the chapter is not yet written. Do not grow this into real instructions
// here — chapters ship with the phases that make their subject matter verifiable.
struct Chapter {
    const char* title;
    const char* stub;
};

const Chapter kChapters[] = {
    {"Welcome to Clivus",
     "<p><i>This chapter is not yet written.</i> Clivus is a DICOM viewing application intended "
     "for research and educational use. It is not a substitute for a clinically validated "
     "diagnostic workstation.</p>"},
    {"Getting Started", "<p><i>This chapter is not yet written.</i></p>"},
    {"Interface Overview", "<p><i>This chapter is not yet written.</i></p>"},
    {"Opening DICOM Files", "<p><i>This chapter is not yet written.</i></p>"},
    {"Working with Series", "<p><i>This chapter is not yet written.</i></p>"},
    {"Viewing Images", "<p><i>This chapter is not yet written.</i></p>"},
    {"Window/Level", "<p><i>This chapter is not yet written.</i></p>"},
    {"Keyboard and Mouse Controls", "<p><i>This chapter is not yet written.</i></p>"},
    {"Preferences", "<p><i>This chapter is not yet written.</i></p>"},
    {"Troubleshooting", "<p><i>This chapter is not yet written.</i></p>"},
    {"Limitations and Research Use", "<p><i>This chapter is not yet written.</i></p>"},
    {"About Clivus", "<p><i>This chapter is not yet written.</i></p>"},
};

}  // namespace

HelpWindow::HelpWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Clivus User Manual");
    resize(760, 520);

    m_nav = new QListWidget(this);
    for (const auto& chapter : kChapters) m_nav->addItem(QString::fromUtf8(chapter.title));

    m_text = new QTextBrowser(this);
    m_text->setOpenExternalLinks(false);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_nav, 1);
    layout->addWidget(m_text, 2);

    connect(m_nav, &QListWidget::currentRowChanged, this, &HelpWindow::showChapter);
    m_nav->setCurrentRow(0);
}

void HelpWindow::showChapter(int row)
{
    if (row < 0 || row >= int(sizeof(kChapters) / sizeof(kChapters[0]))) return;
    m_text->setHtml(QString("<h2>%1</h2>%2")
                        .arg(QString::fromUtf8(kChapters[row].title),
                             QString::fromUtf8(kChapters[row].stub)));
}
