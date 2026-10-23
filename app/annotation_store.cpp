// SPDX-License-Identifier: GPL-3.0-or-later
#include "annotation_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString annotationsDir()
{
    // Same switch as the index cache, so one environment variable relocates everything.
    const QByteArray override = qgetenv("CLIVUS_CACHE_DIR");
    const QString base = override.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             : QString::fromLocal8Bit(override);
    return (base.isEmpty() ? QDir::tempPath() : base) + QStringLiteral("/annotations");
}

// A UID is safe in a file name (digits and dots), but a corrupt or hand-written series UID might
// not be: anything outside [0-9.] is hashed so the path cannot escape the directory.
QString safeName(const QString& seriesUid)
{
    bool plain = !seriesUid.isEmpty();
    for (const QChar c : seriesUid)
        if (!c.isDigit() && c != QLatin1Char('.')) plain = false;
    if (plain) return seriesUid;
    return QStringLiteral("uid-") +
           QString::fromUtf8(QCryptographicHash::hash(seriesUid.toUtf8(), QCryptographicHash::Sha1)
                                 .toHex()
                                 .left(24));
}

QJsonObject readFile(const QString& path, QString* warning)
{
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        if (warning)
            *warning = QStringLiteral("Annotations could not be read (%1).").arg(file.errorString());
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (warning)
            *warning = QStringLiteral("The annotation file for this series was unreadable; starting "
                                      "empty. It will be replaced on the next edit.");
        return {};
    }
    return doc.object();
}

bool writeFile(const QString& path, const QJsonObject& root, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);   // atomic replace: a crash mid-write cannot truncate yesterday's file
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write annotations: %1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Could not save annotations: %1").arg(file.errorString());
        return false;
    }
    return true;
}

QJsonArray roisToJson(const QVector<CanvasWidget::RoiAnnotation>& rois)
{
    QJsonArray array;
    for (const auto& roi : rois) {
        QJsonObject item;
        item.insert(QStringLiteral("shape"), roi.ellipse ? QStringLiteral("ellipse")
                                                         : QStringLiteral("rectangle"));
        item.insert(QStringLiteral("row0"), roi.row0);
        item.insert(QStringLiteral("col0"), roi.col0);
        item.insert(QStringLiteral("row1"), roi.row1);
        item.insert(QStringLiteral("col1"), roi.col1);
        array.append(item);
    }
    return array;
}

}  // namespace

QString AnnotationStore::pathForSeries(const QString& seriesUid)
{
    return annotationsDir() + QLatin1Char('/') + safeName(seriesUid) + QStringLiteral(".json");
}

QVector<AnnotationStore::SliceRois> AnnotationStore::load(const QString& seriesUid, QString* warning)
{
    QVector<SliceRois> out;
    if (seriesUid.isEmpty()) return out;
    const QJsonObject root = readFile(pathForSeries(seriesUid), warning);
    const QJsonArray slices = root.value(QStringLiteral("slices")).toArray();
    for (const QJsonValue& value : slices) {
        const QJsonObject sliceObject = value.toObject();
        SliceRois entry;
        entry.slice = sliceObject.value(QStringLiteral("slice")).toInt(-1);
        if (entry.slice < 0) continue;
        for (const QJsonValue& roiValue : sliceObject.value(QStringLiteral("rois")).toArray()) {
            const QJsonObject item = roiValue.toObject();
            CanvasWidget::RoiAnnotation roi;
            roi.ellipse = item.value(QStringLiteral("shape")).toString() == QLatin1String("ellipse");
            roi.row0 = item.value(QStringLiteral("row0")).toInt();
            roi.col0 = item.value(QStringLiteral("col0")).toInt();
            roi.row1 = item.value(QStringLiteral("row1")).toInt();
            roi.col1 = item.value(QStringLiteral("col1")).toInt();
            // Statistics are deliberately absent from the file: they are recomputed from pixels.
            entry.rois.append(roi);
        }
        if (!entry.rois.isEmpty()) out.append(entry);
    }
    return out;
}

bool AnnotationStore::setSliceRois(const QString& seriesUid, const int slice,
                                   const QVector<CanvasWidget::RoiAnnotation>& rois, QString* error)
{
    if (seriesUid.isEmpty()) {
        if (error) *error = QStringLiteral("This series has no Series Instance UID, so annotations "
                                           "cannot be saved for it.");
        return false;
    }
    const QString path = pathForSeries(seriesUid);
    QJsonObject root = readFile(path, nullptr);
    QJsonArray slices = root.value(QStringLiteral("slices")).toArray();

    QJsonArray kept;
    for (const QJsonValue& value : slices)
        if (value.toObject().value(QStringLiteral("slice")).toInt(-1) != slice) kept.append(value);
    if (!rois.isEmpty()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slice"), slice);
        entry.insert(QStringLiteral("rois"), roisToJson(rois));
        kept.append(entry);
    }
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("slices"), kept);
    return writeFile(path, root, error);
}

bool AnnotationStore::clearSeries(const QString& seriesUid, QString* error)
{
    if (seriesUid.isEmpty()) return true;
    QFile file(pathForSeries(seriesUid));
    if (!file.exists()) return true;
    if (file.remove()) return true;
    if (error) *error = QStringLiteral("Could not remove annotations: %1").arg(file.errorString());
    return false;
}
