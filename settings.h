#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>
#include <QSize>

class Settings {
public:
    static Settings& instance();

    void load(const QString &path);
    void save(const QString &path);

    // Уровень сложности в текущей шкале (см. WorldManager::DEFAULT_DIFFICULTY).
    int difficulty = 2;
    QString theme = "light";
    QSize windowSize{800, 600};
    bool maximized = false;
    bool showStatusBar = false;
    QString lastWorld;
};

#endif // SETTINGS_H