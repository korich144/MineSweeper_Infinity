#ifndef WORLD_H
#define WORLD_H

#include <QString>
#include <QtGlobal>

struct WorldInfo {
    QString seed;
    QString originalName;
    double lastViewX = 0.0;      // координата клетки в центре экрана
    double lastViewY = 0.0;
    double lastScale = 1.0;
    qint64 score = 0;
    qint64 flagsPlaced = 0;
    qint64 elapsed = 0;
    // 0 = уровень не сохранён (старый формат) — брать из настроек
    int difficulty = 0;
    // Версия схемы сложности, в которой записано поле difficulty
    // (см. WorldManager::DIFFICULTY_SCHEME_VERSION). 0 — самое старое сохранение.
    int difficultyVersion = 0;
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