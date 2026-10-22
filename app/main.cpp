#include <QApplication>
#include <QCommandLineParser>

#include "main_window.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("horoswin");

    QCommandLineParser parser;
    const QCommandLineOption bench("bench", "Render N frames headlessly and report timings.", "frames");
    parser.addOption(bench);
    parser.addPositionalArgument("folder", "DICOM folder to open", "[folder]");
    parser.process(app);

    MainWindow window;
    window.show();
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) window.openFolder(args.first());
    if (parser.isSet(bench)) window.runBenchmark(parser.value(bench).toInt());
    return app.exec();
}
