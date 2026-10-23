// SPDX-License-Identifier: GPL-3.0-or-later
#include "series_summary.h"

#include <QStringList>

namespace clivus::ui {

// Series browser entry (roadmap 1.3): description prominent on the first line, then the
// modality · count · dimensions line. Any missing field gets an explicit fallback, never a guess.
QString seriesTitle(const clivus::SeriesInfo& s)
{
    const std::string& description =
        !s.seriesDescription.empty() ? s.seriesDescription : s.studyDescription;
    return description.empty() ? QString("(no description)")
                               : QString::fromStdString(description);
}

QString seriesDetails(const clivus::SeriesInfo& s)
{
    QStringList parts;
    if (!s.modality.empty()) parts << QString::fromStdString(s.modality);
    // Frames only when they say something the object count does not: a 15-frame RT Dose is one
    // object and fifteen slices, and the list should not pretend otherwise.
    const int frames = s.totalFrames();
    // Formats the count itself: arg() on a string with no placeholder both prints a Qt warning and
    // silently ignores the value ("1 image", 1).
    const auto countText = [](size_t n, const char* one, const char* many) {
        return n == 1 ? QString::fromUtf8(one) : QString::fromUtf8(many).arg(n);
    };
    if (frames > int(s.files.size()))
        parts << QString("%1 · %2 frames")
                     .arg(countText(s.files.size(), "1 object", "%1 objects"))
                     .arg(frames);
    else
        parts << countText(s.files.size(), "1 image", "%1 images");
    if (s.width > 0 && s.height > 0) parts << QString("%1 × %2").arg(s.width).arg(s.height);
    // QStringLiteral, not QLatin1String: the separator is a U+00B7 middle dot carried as UTF-8,
    // and QLatin1String reinterprets its bytes as Latin-1, painting "Â·" in every series row on
    // every platform (caught by the real macOS screenshot, invisible in the Pi's offscreen tests).
    return parts.join(QStringLiteral(" · "));
}

QString seriesLabel(const clivus::SeriesInfo& s)
{
    return QString("%1\n%2").arg(seriesTitle(s), seriesDetails(s));
}

}  // namespace clivus::ui
