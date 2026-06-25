#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <qdir.h>
#include <qstandardpaths.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "MineSweeper_Infinity_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
