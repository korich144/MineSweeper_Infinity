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
    // Был ли в файле настроек сохранён размер окна. Если нет (первый запуск),
    // окно подгоняется под ровно 9x9 клеток.
    bool hasWindowSize = false;
    bool maximized = false;
    bool showStatusBar = false;
    QString lastWorld;
};

#endif // SETTINGS_H