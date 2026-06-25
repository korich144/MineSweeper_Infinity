#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>
#include <QSize>

class Settings {
public:
    static Settings& instance();

    void load(const QString &path);
    void save(const QString &path);

    int difficulty = 6;
    QString theme = "light";
    QSize windowSize{800, 600};
    QString lastWorld;
};

#endif // SETTINGS_H