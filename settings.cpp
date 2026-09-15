#include "settings.h"
#include "worldmanager.h"
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
    // Сложность приводим к текущей шкале (см. WorldManager::DIFFICULTY_SCHEME_VERSION).
    const int version = o.value("difficultyVersion").toInt(0);
    const int stored = o.value("difficulty").toInt(WorldManager::DEFAULT_DIFFICULTY);
    if (version >= WorldManager::DIFFICULTY_SCHEME_VERSION) {
        difficulty = WorldManager::clampDifficulty(stored);
    } else if (version == 1) {
        // Были уровни 1..10 — переводим сдвигом.
        difficulty = WorldManager::clampDifficulty(WorldManager::fromLevelScheme(stored));
    } else if (stored > 0) {
        // Самый старый формат: сложность была знаменателем плотности (1/N мин).
        // Подбираем уровень с той же плотностью.
        difficulty = WorldManager::difficultyForProbability(1.0 / stored);
    } else {
        difficulty = WorldManager::DEFAULT_DIFFICULTY;
    }
    theme = o.value("theme").toString("light");
    lastWorld = o.value("lastWorld").toString();
    maximized = o.value("maximized").toBool(false);
    if (o.contains("windowSize")) {
        QJsonObject sz = o.value("windowSize").toObject();
        windowSize = QSize(sz.value("width").toInt(800), sz.value("height").toInt(600));
        hasWindowSize = true;
    }
    showStatusBar = o.value("showStatusBar").toBool(false);
}

void Settings::save(const QString &path) {
    QJsonObject o;
    o["difficulty"] = difficulty;
    o["difficultyVersion"] = WorldManager::DIFFICULTY_SCHEME_VERSION;
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