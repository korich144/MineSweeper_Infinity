#ifndef LOADER_H
#define LOADER_H

#include <QObject>
#include <QMutex>
#include <QSet>
#include <QPair>

class WorldManager;
class Solver;

class Loader : public QObject
{
    Q_OBJECT
public:
    explicit Loader(WorldManager *wm, Solver *solver, const QString &worldDir, QObject *parent = nullptr);

public slots:
    void requestChunk(int64_t chunkX, int64_t chunkY);
    void processQueue();

private:
    void loadChunk(int64_t chunkX, int64_t chunkY);
    void scanBorderForActive(int64_t chunkX, int64_t chunkY);

    WorldManager *m_wm;
    Solver *m_solver;
    QString m_worldDir;
    QMutex m_mutex;
    QSet<QPair<int64_t,int64_t>> m_pending;
};

#endif // LOADER_H