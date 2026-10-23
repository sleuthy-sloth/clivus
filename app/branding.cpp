// SPDX-License-Identifier: GPL-3.0-or-later
#include "branding.h"

#include "style.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace clivus::branding {
namespace {

QStringList candidatePaths(const QString& baseName)
{
    QStringList paths;
    const QString appDir = QCoreApplication::applicationDirPath();
    paths << appDir + QStringLiteral("/assets/") + baseName;
    paths << appDir + QStringLiteral("/../assets/") + baseName;
#ifdef CLIVUS_SOURCE_DIR
    paths << QStringLiteral(CLIVUS_SOURCE_DIR "/assets/") + baseName;
#endif
    return paths;
}

// One place for the fallback mark: a restrained geometric glyph drawn with the palette's accent —
// three nested chevrons reading as stacked planes (the shape of a three-plane study), no font
// dependency, no external image. It exists so a build without the logo asset still looks
// intentional; it is replaced the moment the real mark is dropped in.
QPixmap fallbackMark(int px)
{
    QPixmap pix(px, px);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = clivus::style::accent();
    const double inset = px * 0.14;
    const double w = px - 2 * inset;
    for (int i = 0; i < 3; ++i) {
        const double off = i * px * 0.13;
        QPainterPath path;
        path.moveTo(inset, inset + off + w * 0.30);
        path.lineTo(inset + w / 2.0, inset + off);
        path.lineTo(inset + w, inset + off + w * 0.30);
        QColor stroke = accent;
        stroke.setAlpha(230 - i * 55);
        QPen pen(stroke, std::max(1.5, px * 0.055));
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawPath(path);
    }
    p.end();
    return pix;
}

}  // namespace

QPixmap brandMark(const int px)
{
    for (const QString& base : {QStringLiteral("clivus-mark.svg"), QStringLiteral("clivus-mark.png")}) {
        for (const QString& path : candidatePaths(base)) {
            QPixmap pix;
            if (!pix.load(path)) continue;   // SVG loads through the Qt SVG image plugin when present
            if (pix.isNull()) continue;
            return pix.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    QPixmap embedded(QStringLiteral(":/assets/clivus-mark.svg"));
    if (!embedded.isNull())
        return embedded.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return fallbackMark(px);
}

bool hasBrandMark()
{
    for (const QString& base : {QStringLiteral("clivus-mark.svg"), QStringLiteral("clivus-mark.png")}) {
        for (const QString& path : candidatePaths(base)) {
            QPixmap probe;
            if (probe.load(path) && !probe.isNull()) return true;
        }
    }
    return !QPixmap(QStringLiteral(":/assets/clivus-mark.svg")).isNull();
}

QWidget* makeBrandHeader(const int markPx, QWidget* parent)
{
    auto* header = new QWidget(parent);
    auto* layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* mark = new QLabel(header);
    mark->setAlignment(Qt::AlignCenter);
    mark->setPixmap(brandMark(markPx));
    layout->addWidget(mark);

    // The name always shows; the mark is the picture, the wordmark is the guarantee it is Clivus.
    auto* name = new QLabel(QStringLiteral("Clivus"), header);
    name->setAlignment(Qt::AlignCenter);
    QFont nameFont = name->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 2.1);
    nameFont.setBold(true);
    nameFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    name->setFont(nameFont);
    layout->addWidget(name);
    return header;
}

}  // namespace clivus::branding
