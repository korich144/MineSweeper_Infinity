#ifndef SOLVER_H
#define SOLVER_H

#include <QObject>
#include <QSet>
#include <QPair>
#include <QMutex>
#include <atomic>

class WorldManager;

class Solver : public QObject
{
    Q_OBJECT
public:
    explicit Solver(WorldManager *wm, QObject *parent = nullptr);

public slots:
    void startReveal(int64_t x, int64_t y);
    void addToQueue(int64_t x, int64_t y);
    void processQueueIfIdle();

signals:
    void finished();
    void cellsOpened(int count);

private:
    void processCell(int64_t x, int64_t y, int depth = 0);
    int countNeighborMines(int64_t x, int64_t y);

    WorldManager *m_wm;
    std::atomic<bool> m_running{false};
    QMutex m_queueMutex;
    QSet<QPair<int64_t, int64_t>> m_activeQueue;
    int m_openedCounter = 0;
    static constexpr int MAX_RECURSION = 10000;
};

#endif // SOLVER_H