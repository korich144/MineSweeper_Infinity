#ifndef WORLD_H
#define WORLD_H

#include <QString>
#include <QtGlobal>

struct WorldInfo {
    QString seed;
    QString originalName;
    qint64 lastViewX = 0;
    qint64 lastViewY = 0;
    double lastScale = 1.0;
    qint64 score = 0;
    qint64 flagsPlaced = 0;
    qint64 elapsed = 0;
    bool gameActive = true;
    QString folderName;
};

class World {
public:
    static WorldInfo load(const QString &worldPath);
    static void save(const QString &worldPath, const WorldInfo &info);
    static QString generateSeed();
};

#endif // WORLD_H