#ifndef SAVER_H
#define SAVER_H

#include <QObject>
#include <QTimer>

class WorldManager;

class Saver : public QObject
{
    Q_OBJECT
public:
    explicit Saver(WorldManager *wm, const QString &worldDir, QObject *parent = nullptr);

public slots:
    void saveNow();

signals:
    void showSaveIndicator();

private:
    WorldManager *m_wm;
    QString m_worldDir;
    QTimer *m_timer;
    bool m_dirty = false;

private slots:
    void onTimer();
};

#endif // SAVER_H