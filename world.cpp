#include "world.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

WorldInfo World::load(const QString &worldPath) {
    WorldInfo info;
    QFile f(worldPath + "/world.ini");
    if (!f.open(QIODevice::ReadOnly)) return info;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return info;
    QJsonObject o = doc.object();
    info.seed = o.value("seed").toString();
    info.originalName = o.value("originalName").toString();
    info.lastViewX = o.value("lastViewX").toDouble(0.0);
    info.lastViewY = o.value("lastViewY").toDouble(0.0);
    info.lastScale = o.value("lastScale").toDouble(1.0);
    info.score = static_cast<qint64>(o.value("score").toDouble());
    info.flagsPlaced = static_cast<qint64>(o.value("flagsPlaced").toDouble());
    info.elapsed = static_cast<qint64>(o.value("elapsed").toDouble());
    info.difficulty = o.value("difficulty").toInt(0);
    info.difficultyVersion = o.value("difficultyVersion").toInt(0);
    info.gameActive = o.value("gameActive").toBool(true);
    info.folderName = QDir(worldPath).dirName();
    return info;
}

void World::save(const QString &worldPath, const WorldInfo &info) {
    QJsonObject o;
    o["seed"] = info.seed;
    o["originalName"] = info.originalName;
    o["lastViewX"] = info.lastViewX;
    o["lastViewY"] = info.lastViewY;
    o["lastScale"] = info.lastScale;
    o["score"] = static_cast<double>(info.score);
    o["flagsPlaced"] = static_cast<double>(info.flagsPlaced);
    o["elapsed"] = static_cast<double>(info.elapsed);
    o["difficulty"] = info.difficulty;
    o["difficultyVersion"] = info.difficultyVersion;
    o["gameActive"] = info.gameActive;

    QDir().mkpath(worldPath);
    QFile f(worldPath + "/world.ini");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson());
        f.close();
    }
}

QString World::generateSeed() {
    QString seed;
    for (int i = 0; i < 32; ++i) {
        int r = QRandomGenerator::global()->bounded(16);
        seed += QString::number(r, 16);
    }
    return seed;
}