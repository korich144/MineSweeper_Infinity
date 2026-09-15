#include "settings.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

Settings& Settings::instance() {
    static Settings s;
    return s;
}

void Settings::load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    QJsonObject o = doc.object();
    difficulty = o.value("difficulty").toInt(6);
    theme = o.value("theme").toString("light");
    lastWorld = o.value("lastWorld").toString();
    maximized = o.value("maximized").toBool(false);
    if (o.contains("windowSize")) {
        QJsonObject sz = o.value("windowSize").toObject();
        windowSize = QSize(sz.value("width").toInt(800), sz.value("height").toInt(600));
    }
    showStatusBar = o.value("showStatusBar").toBool(false);
}

void Settings::save(const QString &path) {
    QJsonObject o;
    o["difficulty"] = difficulty;
    o["theme"] = theme;
    o["lastWorld"] = lastWorld;
    o["maximized"] = maximized;
    QJsonObject sz;
    sz["width"] = windowSize.width();
    sz["height"] = windowSize.height();
    o["windowSize"] = sz;
    o["showStatusBar"] = showStatusBar;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson());
        f.close();
    }
}