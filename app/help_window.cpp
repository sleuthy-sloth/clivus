// SPDX-License-Identifier: GPL-3.0-or-later
#include "help_window.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include "clivus/manual_search.h"

#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

// The manual's one authoritative source (roadmap 5.2): the text lives here, not duplicated in the
// README or docs/. Everything describes verified, shipped behaviour — presets are the values the
// options bar applies, gestures are the ones canvas_widget.cpp implements, limitations are the
// ones the test suite documents.
struct Chapter {
    const char* title;
    const char* body;
};

const Chapter kChapters[] = {
{"Welcome to Clivus",
 "<p>Clivus is a free, open-source viewer for <b>DICOM</b> — the standard format in which medical "
 "imaging devices store their output: CT, MR, RT Dose and related objects, organised as "
 "<i>studies</i> containing <i>series</i>, each series holding one or more <i>images</i>.</p>"
 "<p>Clivus is for research and educational use: point it at a folder of DICOM files, browse its "
 "series, examine every slice, and study how window/level settings change what an image "
 "shows.</p>"
 "<div style=\"padding:8px; margin:8px 0;\">"
 "<b>Research use, not clinical use.</b> Clivus is a DICOM viewing application intended for "
 "research and educational use. It is not a substitute for a clinically validated diagnostic "
 "workstation, and its output should not be relied upon for clinical decisions without "
 "appropriate independent validation.</div>"},
{"Getting Started",
 "<p>Launch Clivus. When no folder is open you see the welcome page: what Clivus does, an "
 "<b>Open Folder…</b> button, and a link to this manual.</p>"
 "<ol>"
 "<li>Click <b>Open Folder…</b> (or press <b>Ctrl+O</b>) and choose a folder that contains "
 "DICOM files.</li>"
 "<li>Clivus scans the folder in the background — the status bar counts the entries it has "
 "examined. <b>Cancel</b> stops it and leaves the folder unread.</li>"
 "<li>Pick a series in the left-hand browser. Its first slice decodes and appears in the "
 "canvas; the status bar reports the slice number.</li>"
 "<li>Use the mouse wheel to move through the series' slices.</li>"
 "<li>Adjust the image with the window/level drag (see <i>Window/Level</i>) or the numeric "
 "fields above the canvas.</li>"
 "</ol>"},
{"Interface Overview",
 "<p><b>Menu bar</b> — File (Open Folder, Exit), View (Fit Image, Reset View, Tools, Series "
 "Sidebar, Show Diagnostics, Reset Layout), Help (this manual, About).</p>"
 "<p><b>Toolbar</b> — the same actions, with tooltips that spell out each gesture.</p>"
 "<p><b>Series browser</b> — left pane: a search box above the series list; each row shows the "
 "series description plus a line of <i>modality · image count · dimensions</i>, and a "
 "thumbnail. Rows that cannot be previewed show a missing-image marker, say so plainly, and "
 "remain openable.</p>"
 "<p><b>Options bar</b> — Window Center / Window Width fields and the preset selector, above "
 "the canvas.</p>"
 "<p><b>Image canvas</b> — black workspace, the image in the middle, overlays in the corners: "
 "series information top-left, window values and zoom top-right.</p>"
 "<p><b>Slice bar</b> — a slider plus &ldquo;Slice N / M&rdquo; under the canvas, for series "
 "larger than one image.</p>"
 "<p><b>Status bar</b> — state messages (Ready / Loading / Cancelled) and errors in plain "
 "language. Render timings are development information and appear only while <b>View ▸ Show "
 "Diagnostics</b> is checked (Ctrl+D).</p>"},
{"Opening DICOM Files",
 "<p><b>File ▸ Open Folder…</b> or <b>Ctrl+O</b>. Clivus scans the folder <i>and its "
 "subfolders</i>, reads each file's header — with a full-read fallback for objects whose meta "
 "section is too large for the fast path — and groups the objects by series. The first scan is "
 "stored in a small index next to the data, so the same folder opens from cache in about a "
 "millisecond afterwards; a changed folder misses and rescans on its own.</p>"
 "<p>Supported encodings are listed in <i>Limitations</i>; files without DICOM pixel data are "
 "ignored rather than failing the scan.</p>"
 "<p>If no supported images are found, the status bar says so and the application stays usable "
 "— you can open another folder at any time.</p>"},
{"Working with Series",
 "<p>A <b>study</b> is one imaging session; a <b>series</b> is one scan within it (one CT "
 "pass, one MR sequence); an <b>image</b> is a single two-dimensional slice — and for "
 "multi-frame objects, Clivus walks the frames as if they were slices.</p>"
 "<p>Select a series with a click or with arrows + Return. The description, modality, image "
 "count and dimensions shown in the browser are the values the files themselves declare; a "
 "blank description means the files don't declare one (Clivus falls back to the study "
 "description, then shows &ldquo;(no description)&rdquo;). Nothing in the interface is "
 "guessed.</p>"},
{"Viewing Images",
 "<p><b>Mouse wheel</b> — next/previous slice. <b>Left-drag</b> — window/level: horizontal "
 "widens the window, vertical moves the centre. <b>Right-drag</b> — pan. <b>Double-click or "
 "R</b> — reset view. <b>Ctrl+wheel</b> — zoom. <b>F</b> — fit image.</p>"
 "<p>Zoom is viewport state: it persists across slices within a series and resets when a new "
 "series loads. Fit scales the image to the canvas keeping its aspect ratio.</p>"
 "<p>The overlays report what is being shown: series identity top-left, window values and zoom "
 "top-right. Colour images are displayed without a window — the standard applies VOI LUT to "
 "greyscale only, and Clivus states so in the overlay instead of pretending otherwise.</p>"},
{"Cine playback",
 "<p>Some questions are easier to answer in motion: a stack that steps through a joint, a "
 "multi-frame acquisition, a study where one slice alone looks ambiguous. Press <b>Space</b> (or "
 "<b>View &#9656; Play / pause cine</b>) to loop the current series; press it again to stop.</p>"
 "<p>The rate is 12 frames per second by default. <b>[</b> slows it by two frames per second "
 "(down to 2) and <b>]</b> speeds it up (up to 30); the status bar confirms the new rate.</p>"
 "<p>Playback uses the same one-decode-at-a-time path as the wheel, so it never shows a slice out "
 "of order and never decodes ahead of what it can display: on a slow disk the loop simply runs "
 "slower rather than skipping frames &#8212; skipping would hide exactly the images you are looking "
 "for. A manual slice step, a new series, or a new folder stops playback; you are back in control "
 "the moment you touch the wheel or the strip.</p>"},
{"Window/Level",
 "<p>Window Center and Window Width select which range of grey levels fills the display's "
 "black to white. CT data covers thousands of values a monitor cannot draw; the window chooses "
 "the segment, and dragging changes it live. Dose objects are scaled into real units first "
 "(<b>DoseGridScaling</b>), so their windows use Gy-scale values.</p>"
 "<p>Type values directly into the fields, or drag on the image. A width below 1 or "
 "non-numeric input is rejected with an inline message and the last good value stays.</p>"
 "<p>Presets are viewing conventions, not diagnostic prescriptions:</p>"
 "<table border=1 cellpadding=4 style=\"border-collapse:collapse;\">"
 "<tr><th>Preset</th><th>Center</th><th>Width</th><th>Intended for</th></tr>"
 "<tr><td>Soft Tissue</td><td>40</td><td>400</td><td>general CT</td></tr>"
 "<tr><td>Lung</td><td>−600</td><td>1500</td><td>lung parenchyma</td></tr>"
 "<tr><td>Bone</td><td>300</td><td>1500</td><td>bone detail</td></tr>"
 "<tr><td>Brain</td><td>40</td><td>80</td><td>head CT</td></tr>"
 "<tr><td>Liver</td><td>60</td><td>160</td><td>liver CT</td></tr>"
 "<tr><td>MRI Default</td><td>450</td><td>900</td><td>MRI</td></tr>"
 "</table>"
 "<p>A CT preset applied to MR data is simply a different wrong image; the selector is "
 "disabled on colour images. Once values are edited by hand the combo shows <b>(custom)</b> "
 "until a preset is picked again.</p>"},
{"Keyboard and Mouse Controls",
 "<table border=1 cellpadding=4 style=\"border-collapse:collapse;\">"
 "<tr><th>Action</th><th>Input</th><th>Where</th></tr>"
 "<tr><td>Change slices</td><td>Mouse wheel</td><td>Image canvas, multi-image series</td></tr>"
 "<tr><td>Window/level</td><td>Left-drag</td><td>Image canvas, grayscale</td></tr>"
 "<tr><td>Pan</td><td>Right-drag</td><td>Image canvas</td></tr>"
 "<tr><td>Zoom</td><td>Ctrl + mouse wheel</td><td>Image canvas</td></tr>"
 "<tr><td>Reset view</td><td>Double-click or R</td><td>Image canvas</td></tr>"
 "<tr><td>Fit image</td><td>F</td><td>Window</td></tr>"
 "<tr><td>Open folder</td><td>Ctrl+O</td><td>Window</td></tr>"
 "<tr><td>Toggle series sidebar</td><td>Ctrl+B</td><td>Window</td></tr>"
 "<tr><td>Measurement tool</td><td>M</td><td>Window (left-drag then measures)</td></tr>"
 "<tr><td>Distance</td><td>Left-drag</td><td>Image canvas, Measure tool active</td></tr>"
 "<tr><td>Angle</td><td>Shift+click three points</td><td>Image canvas, Measure tool active</td></tr>"
 "<tr><td>Browse a slice directly</td><td>Click or drag the slice strip</td><td>Under the image</td></tr>"
 "<tr><td>Open by dropping</td><td>Drop a folder or file on the window</td><td>Window</td></tr>"
 "<tr><td>Show diagnostics</td><td>Ctrl+D</td><td>Window</td></tr>"
 "<tr><td>Find series</td><td>Ctrl+F</td><td>Window</td></tr>"
 "<tr><td>User manual</td><td>F1</td><td>Window</td></tr>"
 "<tr><td>Exit</td><td>standard</td><td>Window</td></tr>"
 "</table>"},
{"Preferences",
 "<p>Clivus remembers and restores one thing between sessions: your layout — the window's "
 "size and position, the series-sidebar width, whether the sidebar is open, and whether Show "
 "Diagnostics is on. <b>View ▸ Reset Layout</b> returns all of it to the built-in "
 "default.</p>"
 "<p>Nothing else is stored. Clivus does not remember which folders you opened, and never "
 "stores patient information: a viewer of medical images should not leave a trail of what was "
 "opened on shared machines.</p>"},
{"Measuring",
 "<p>The Measure tool turns the left drag into a measuring gesture: while it is the active tool, "
 "left-drag draws a distance, and Shift+clicking three points draws an angle (the second click is "
 "the vertex). Right-drag still pans, the wheel still changes slices, and switching to any other "
 "tool restores window/level on the left drag.</p>"
 "<p>Distances are reported in millimetres from the object's DICOM Pixel Spacing. When an object "
 "carries no usable spacing the label says <i>spacing unknown</i> — the app will not invent a "
 "number. Measurements belong to the slice they were drawn on: changing slice or series clears "
 "them, and Tools &#9656; Clear measurements removes them at once.</p>"},
{"MPR (research/experimental)",
 "<p>View &#9656; MPR opens a three-plane window — axial, coronal and sagittal — resliced through "
 "the study's own geometry. Scroll any plane with its wheel or strip and the other two show "
 "hairline crosshairs through that position. Window/level is shared across the three planes.</p>"
 "<p>This is a first round: reslicing is nearest-neighbour (no interpolation across slices, so "
 "nothing is invented between them), and the entry is disabled for studies whose objects do not "
 "all report image position and orientation. The single-series view remains the default and MPR "
 "never replaces it.</p>"},
{"Troubleshooting",
 "<p><b>A folder opens but no series appear.</b> The folder may hold files without DICOM "
 "pixel data, or be the wrong folder entirely. Check the content, or try a folder you know "
 "works (the repository's <code>testdata/corpus</code>).</p>"
 "<p><b>A series shows a missing-preview marker.</b> Its first object could not be "
 "previewed; the series usually remains openable — select it and read the status "
 "message.</p>"
 "<p><b>&ldquo;This slice could not be decoded.&rdquo;</b> That file's pixels are "
 "unsupported or damaged. Other slices of the same series and other series keep working; "
 "failed slices are not retried on their own — scroll away and back.</p>"
 "<p><b>The image looks too dark or too bright.</b> Window settings, not file damage: try a "
 "preset for the modality, or drag on the image.</p>"
 "<p><b>Slice navigation does nothing.</b> Single-image series have nothing to scroll.</p>"
 "<p><b>The window looks wrong after a restart.</b> View ▸ Reset Layout returns the "
 "built-in arrangement; the settings file behind it is a plain <code>QSettings</code> file "
 "and safe to delete.</p>"
 "<p><b>Still stuck.</b> Report the version (in <i>About</i>), the operating system, the "
 "steps that reproduce it and the exact status-bar message — with a test dataset, never a "
 "real patient file or a screenshot of one.</p>"},
{"Limitations and Research Use",
 "<p>Supported and test-asserted: explicit-VR little-endian; RLE, JPEG and JPEG-LS lossless "
 "and near-lossless decoding with pixel-exact output; JPEG 2000 where the DCMTK build carries "
 "it (the Windows release build does). Colour: RGB and YBR_FULL with planar variants "
 "converted. Multi-frame objects are browsed in full, RT Dose objects read in real dose units "
 "via DoseGridScaling, and non-linear modality LUTs and PALETTE COLOR palettes are applied at "
 "decode time.</p>"
 "<p>Implemented and working: distance and angle measurement with mm calibration from each "
 "object's pixel spacing (unknown spacing is reported as unknown, never invented); three-plane "
 "MPR resliced through the study's own geometry (experimental, nearest-neighbour, needs "
 "per-slice position and orientation).</p>"
 "<p>Not supported: volume-rendered 3D, oblique reformats, DICOM networking or PACS, "
 "anonymisation or de-identification, annotations beyond distance/angle, and orientation "
 "markers on the image.</p>"
 "<p>Every claim in this chapter is backed by a test in the repository; the parity gate fails "
 "CI if window/level rendering drifts from the library's own semantics.</p>"},
{"Regions of interest",
 "<p>Window/level answers <i>what does this look like</i>; a region of interest answers <i>what "
 "are the numbers</i>. Clivus measures the pixels inside a shape you drag, using the object's own "
 "modality values &#8212; Hounsfield units for CT, stored values elsewhere &#8212; and keeps the "
 "region with the series so it is there next time.</p>"
 "<p><b>Drawing one.</b> Choose <b>Tools &#9656; Rectangle ROI</b> (or <b>Ctrl+R</b>) or "
 "<b>Ellipse ROI</b>, then left-drag a box over the image. While either tool is active the left "
 "drag draws the region instead of adjusting window/level; right-drag still pans, and switching to "
 "another tool restores the usual gesture. On release Clivus shows <b>mean</b>, <b>standard "
 "deviation</b>, <b>min</b>, <b>max</b> and, when the file carries Pixel Spacing, <b>area in "
 "mm&#178;</b>.</p>"
 "<p><b>What the numbers mean.</b> The statistics are computed over the pixels the shape actually "
 "covers: an ellipse counts the pixels inside the ellipse, not the corners of its bounding box, so "
 "its area is the true pixel-covered area rather than &#960;ab. Standard deviation is the "
 "population form (divide by the number of pixels). If the header has no Pixel Spacing the area "
 "line says <i>area unknown</i> rather than inventing one &#8212; the same rule distances follow."
 "</p>"
 "<p><b>Where they are kept.</b> ROIs are saved per series, per slice, in a small JSON file in the "
 "application data directory, keyed by the Series Instance UID. They carry geometry and tool type "
 "only: no patient name, no file paths. The numbers are recomputed from the pixels each time they "
 "are shown, so a file edited by hand cannot make the statistics lie. <b>File &#9656; Export "
 "measurements&#8230;</b> writes the series' regions to a CSV for a spreadsheet or a report.</p>"
 "<p><b>Limits:</b> rectangles and ellipses only &#8212; no freehand, no arrow or text labels, no "
 "RECIST or Cobb templates. These are a local sidecar, not DICOM Structured Reporting or "
 "Presentation State; nothing here is written back into the DICOM files, and other software will "
 "not see these annotations. Angle and distance measurements are session-only and are not part of "
 "the saved file yet.</p>"},
{"Opening a study from a server",
 "<p><b>File &#9656; Open from server&#8230;</b> fetches a study over <b>DICOMweb</b> &#8212; the "
 "HTTP interface modern PACS installations expose &#8212; instead of opening a folder on disk.</p>"
 "<ol>"
 "<li>Enter the service's base URL, for example <code>https://pacs.example.com/dicomweb</code>. "
 "Clivus remembers it; nothing else about the session is stored.</li>"
 "<li>If the server needs it, put the full <code>Authorization</code> header value in the "
 "Auth field (<code>Bearer &lt;token&gt;</code> or <code>Basic &lt;base64&gt;</code>). It is "
 "masked and kept only while the dialog is open.</li>"
 "<li>Filter by patient name, study date (DICOM <code>YYYYMMDD</code>, ranges like "
 "<code>20240101-20240131</code> where the server supports them), accession number or modality, "
 "then <b>Search</b>.</li>"
 "<li>Select a study and press <b>Download and open</b>. Progress is shown per instance, and "
 "<b>Cancel download</b> stops it cleanly.</li>"
 "</ol>"
 "<p>The study is written to its own folder inside the application cache and opened there, so "
 "everything after this point &#8212; series list, window/level, measurements, MPR &#8212; works "
 "exactly as it does for a local folder.</p>"
 "<p><b>If something fails, Clivus says what:</b> an HTTP status (401 means the token is wrong or "
 "expired, 404 means the server has no such study), a server that is not DICOMweb, or a study "
 "whose instances could not all be fetched. A partial download is reported as an error rather "
 "than opened as if complete.</p>"
 "<p><b>Limits:</b> this is the DICOMweb pair of services, QIDO-RS for search and WADO-RS for "
 "retrieval. It does not speak the older C-FIND/C-MOVE protocol, does not upload (no STOW-RS), "
 "does not fetch modality worklists, and expects single-part WADO replies &#8212; some servers "
 "answer in multipart form, which this version does not unpack yet. Patient names shown in the "
 "results come from the server and are not written to the index cache.</p>"},
{"Exporting images",
 "<p>Two ways out, both at the window/level currently on screen:</p>"
 "<ul>"
 "<li><b>File &#9656; Export image as PNG&#8230;</b> writes the slice you are looking at. The file "
 "holds exactly the pixels the canvas is showing, with no overlay text (no window numbers, no "
 "statistics) &#8212; if you want those, take a screenshot, because what is written is the image.</li>"
 "<li><b>File &#9656; Export series as PNGs&#8230;</b> writes every slice of the open series into a "
 "folder, named <code>slice-0001.png</code> upwards. Progress is shown and can be cancelled; slices "
 "that fail to decode are counted and named in the status bar rather than written as blank "
 "images.</li>"
 "</ul>"
 "<p>PNG carries the image, not the DICOM metadata, so an exported PNG has no patient identity and "
 "no geometry. That also means it cannot be measured afterwards, and its window/level is baked in. "
 "For a copy that stays measurable, export DICOM instead (below).</p>"},
{"Anonymising a study",
 "<p><b>File &#9656; Export de-identified copy&#8230;</b> writes a second copy of the open folder with "
 "patient identity removed from the metadata. The original is never modified.</p>"
 "<p><b>What is always done:</b> Patient Name becomes <code>ANONYMOUS</code>; Patient ID is replaced "
 "with a generated one; birth date and time, other patient IDs and names, address, telephone "
 "numbers, patient comments, accession number, study ID, institution name and address, referring and "
 "performing physician names, operators, station name and device serial number are removed; "
 "series/study descriptions and protocol names are cleared; a verification pass then re-reads every "
 "file it wrote and searches for the original values.</p>"
 "<p><b>What you choose:</b> remap Study/Series/SOP Instance UIDs (recommended: without it the copy "
 "is still linkable to the source system); shift dates by one random offset per export, so intervals "
 "inside the study are preserved but the absolute dates are not; remove private (vendor) tags, which "
 "are a common hiding place for identity; remove patient sex, a quasi-identifier.</p>"
 "<p><b>Read the report.</b> It lists what was removed, whether the verification passed, and anything "
 "that could not be done. A copy whose verification failed is a copy you should not share.</p>"
 "<p><b>What this does not do.</b> Pixel data is copied unchanged, so text burned into an image "
 "(annotations, ultrasound overlays, photographed films) survives &#8212; Clivus cannot detect it, and "
 "you must review the images yourself. De-identification is not the same as anonymity: a rare "
 "diagnosis, a distinctive implant, a visible face in a reconstruction can still identify someone. "
 "And this is not a legal determination &#8212; that call belongs to whoever is accountable for the "
 "data.</p>"},
{"Cache and Privacy",
 "<p>When you open a folder, Clivus writes a small metadata index so the next open of the "
 "same folder does not re-read every file header. The index lives in the operating system's "
 "application cache directory, <b>not</b> inside your DICOM folder: Windows "
 "%LOCALAPPDATA%\Clivus\index, macOS ~/Library/Caches/Clivus/index, Linux "
 "$XDG_CACHE_HOME/clivus/index. The file is named from a hash of the folder path, so the "
 "filename itself does not spell out where the study lives.</p>"
 "<p>What it stores: series and study identifiers, modality, study and series descriptions, "
 "image dimensions, the file paths the scan found, frame counts and per-slice geometry, plus "
 "a fingerprint of the folder used to notice changes. What it does <b>not</b> store: patient "
 "names, patient identifiers, or pixel data.</p>"
 "<p>Clearing it: View &#9656; Clear index cache forgets the currently open folder's index; "
 "deleting the cache directory above removes every folder's. A cache that is missing, "
 "outdated or unreadable is simply rebuilt on the next open — the viewer never depends on it. "
 "Clivus sends nothing over the network.</p>"
 "<p>Limitation, stated plainly: the fingerprint is file count, newest modification time and "
 "total size. A file replaced by one with the same size and timestamp would not be noticed. "
 "Clivus also removed the older cache it used to write inside DICOM folders, because that one "
 "stored the patient name.</p>"},
{"About Clivus",
 "<p>Clivus is a cross-platform DICOM viewer, GPL-3.0-or-later.</p>"
 "<p>Source and repository: the <b>Clivus</b> repository on GitHub (linked from the "
 "project's README). Built on Qt 6 (GPL option), DCMTK (BSD-style OFFIS), SQLite and the "
 "IJG/CharLS/OpenJPEG codec family — licences and notices live in the distribution's "
 "<b>THIRD-PARTY.md</b>.</p>"
 "<p>Clivus is intended for research and educational use and is not a medical device.</p>"},
};

}  // namespace

HelpWindow::HelpWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Clivus User Manual");
    resize(760, 520);

    m_nav = new QListWidget(this);
    for (const auto& chapter : kChapters) m_nav->addItem(QString::fromUtf8(chapter.title));

    // Roadmap 5.2: search above the navigation; matches on the chapter's content, not just its
    // title, because this manual is short enough that the cost is zero and the usefulness is real.
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search the manual…");
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip("Search chapter titles and their text");

    m_text = new QTextBrowser(this);
    m_text->setOpenExternalLinks(false);   // the manual is fully offline

    auto* left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(4);
    left->addWidget(m_search);
    left->addWidget(m_nav, 1);

    auto* layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addWidget(m_text, 2);

    connect(m_search, &QLineEdit::textChanged, this, &HelpWindow::applySearch);
    connect(m_nav, &QListWidget::currentRowChanged, this, &HelpWindow::showChapter);

    // The "(no matching chapter)" row is created once and toggled visible, so the list never
    // re-syncs offsets when one comes or goes.
    m_noMatches = new QListWidgetItem(QStringLiteral("(no matching chapter)"), m_nav);
    m_noMatches->setFlags(Qt::NoItemFlags);   // a state marker, not a chapter you can pick
    m_noMatches->setHidden(true);
}

void HelpWindow::showChapter(int row)
{
    const int count = int(sizeof(kChapters) / sizeof(kChapters[0]));
    // While the placeholder is visible it occupies row 0 and shifts every real chapter by one.
    const int chapter = m_noMatches && !m_noMatches->isHidden() ? row - 1 : row;
    if (chapter < 0 || chapter >= count) return;
    m_text->setHtml(QString("<h2>%1</h2>\n%2")
                        .arg(QString::fromUtf8(kChapters[chapter].title),
                             QString::fromUtf8(kChapters[chapter].body)));
    // With an active query, jump to the first hit inside the article: the reader arrived here
    // because of a phrase, so land on it instead of the top of the chapter.
    const QString query = m_search->text().trimmed();
    if (!query.isEmpty()) {
        QTextCursor cursor = m_text->textCursor();
        cursor.movePosition(QTextCursor::Start);
        m_text->setTextCursor(cursor);
        if (m_text->find(query, QTextDocument::FindCaseSensitively)) {
            // find() already selected the first hit; it is visible as the current selection.
        }
    }
}

void HelpWindow::applySearch(const QString& query)
{
    // The matching rules live in core (clivus/manual_search.h) so they are testable without a GUI:
    // title and visible body text, case-folded, markup stripped, whitespace collapsed.
    const auto articles = articlesForSearch();
    const auto hits = clivus::manual::matchingArticles(articles, query.toStdString());
    std::vector<bool> visible(articles.size(), false);
    for (const int hit : hits) visible[size_t(hit)] = true;

    const bool searching = !query.trimmed().isEmpty();
    m_noMatches->setHidden(true);
    int chapter = 0;
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem* item = m_nav->item(i);
        if (item == m_noMatches) continue;
        const bool match = !searching || visible[size_t(chapter)];
        item->setHidden(!match);
        if (match) {
            // Say why a body-only match is here: a short excerpt in the row's tooltip reads better
            // than a chapter title that has no obvious connection to the query.
            const std::string ex = clivus::manual::excerpt(articles[size_t(chapter)],
                                                           query.toStdString());
            item->setToolTip(ex.empty() ? QString() : QString::fromStdString(ex));
        }
        ++chapter;
    }

    if (hits.empty()) {
        m_noMatches->setHidden(false);
        m_nav->setCurrentRow(-1);
        m_text->setPlainText(QString());
        return;
    }
    if (m_nav->currentRow() < 0 || m_nav->currentItem()->isHidden())
        m_nav->setCurrentRow(hits.front());
    // Re-render the open chapter so the highlight follows the query.
    showChapter(m_nav->currentRow());
}

std::vector<clivus::manual::Article> HelpWindow::articlesForSearch() const
{
    std::vector<clivus::manual::Article> out;
    out.reserve(sizeof(kChapters) / sizeof(kChapters[0]));
    for (const auto& chapter : kChapters)
        out.push_back({chapter.title, chapter.body});
    return out;
}
