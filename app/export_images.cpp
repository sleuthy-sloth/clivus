// SPDX-License-Identifier: GPL-3.0-or-later
#include "export_images.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <string>
#include <vector>

#include "clivus/image_frame.h"

bool exportDisplayedPng(const QImage image, const QString& path, QString* error)
{
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("There is nothing to export: the canvas has not rendered an image "
                                    "yet (or this system has no OpenGL context).");
        return false;
    }
    if (image.save(path, "PNG")) return true;
    if (error) *error = QStringLiteral("Could not write %1").arg(path);
    return false;
}

SeriesExportResult exportSeriesPngs(const clivus::SeriesInfo& series, const double center,
                                    const double width, const QString& folder,
                                    const std::function<bool()>& cancelled)
{
    SeriesExportResult result;
    if (QDir().mkpath(folder) == false && !QFileInfo(folder).isDir()) {
        result.error = QStringLiteral("Could not create %1").arg(folder);
        return result;
    }
    int fileIndex = 0;
    int frame = 0;
    for (int index = 0; index < series.totalFrames(); ++index) {
        if (cancelled && cancelled()) {
            result.cancelled = true;
            return result;
        }
        if (!series.locate(index, fileIndex, frame)) continue;
        const std::string& file = series.files[size_t(fileIndex)];
        clivus::ImageFrame decoded;
        if (!clivus::loadFrame(file, decoded, frame) || !decoded.valid()) {
            ++result.failed;
            result.failures << QStringLiteral("slice %1: %2")
                                   .arg(index + 1)
                                   .arg(QFileInfo(QString::fromStdString(file)).fileName());
            continue;
        }
        // Same render path as the screen: one byte per pixel, window applied, MONOCHROME1 inverted.
        std::vector<uint8_t> bytes(decoded.byteCount());
        decoded.render(bytes.data(), center, width);
        const QImage::Format format =
            decoded.isColour() ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
        QImage image(bytes.data(), decoded.width, decoded.height, decoded.width * decoded.channels,
                     format);
        const QString path = QStringLiteral("%1/slice-%2.png").arg(folder).arg(index + 1, 4, 10,
                                                                            QLatin1Char('0'));
        if (!image.copy().save(path, "PNG")) {   // copy(): the QImage does not own `bytes`
            ++result.failed;
            result.failures << QStringLiteral("slice %1: could not write %2").arg(index + 1).arg(path);
            continue;
        }
        ++result.written;
    }
    return result;
}
