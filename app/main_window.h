#pragma once
#include <QMainWindow>
#include <vector>

#include "hw/series_index.h"

class CanvasWidget;
class QLabel;
class QListWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFolder(const QString& dir);
    void runBenchmark(int frames, int seriesRow = 0);

private:
    void showSeries(int index);
    void showSlice(int delta);
    void updateInfo(const QString& extra = {});

    QListWidget* m_seriesList = nullptr;
    CanvasWidget* m_canvas = nullptr;
    QLabel* m_status = nullptr;
    std::vector<hw::SeriesInfo> m_series;
    int m_currentSeries = -1;
    int m_currentSlice = 0;
    QString m_timing;
    QString m_windowLevel;
};
