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
    info.lastViewX = static_cast<qint64>(o.value("lastViewX").toDouble());
    info.lastViewY = static_cast<qint64>(o.value("lastViewY").toDouble());
    info.lastScale = o.value("lastScale").toDouble(1.0);
    info.score = static_cast<qint64>(o.value("score").toDouble());
    info.flagsPlaced = static_cast<qint64>(o.value("flagsPlaced").toDouble());
    info.elapsed = static_cast<qint64>(o.value("elapsed").toDouble());
    info.gameActive = o.value("gameActive").toBool(true);
    info.folderName = QDir(worldPath).dirName();
    return info;
}

void World::save(const QString &worldPath, const WorldInfo &info) {
    QJsonObject o;
    o["seed"] = info.seed;
    o["originalName"] = info.originalName;
    o["lastViewX"] = static_cast<double>(info.lastViewX);
    o["lastViewY"] = static_cast<double>(info.lastViewY);
    o["lastScale"] = info.lastScale;
    o["score"] = static_cast<double>(info.score);
    o["flagsPlaced"] = static_cast<double>(info.flagsPlaced);
    o["elapsed"] = static_cast<double>(info.elapsed);
    o["gameActive"] = info.gameActive;

    QDir().mkpath(worldPath);
    QFile f(worldPath + "/world.ini");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson());
        f.close();
    }
}

QString World::generateSeed() {
    quint64 v = QRandomGenerator::global()->generate64();
    return QString::number(v, 16).rightJustified(16, '0');
}