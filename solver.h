#ifndef SOLVER_H
#define SOLVER_H

#include <QObject>
#include <QSet>
#include <QPair>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <vector>
#include "worldmanager.h"

class Solver : public QObject
{
    Q_OBJECT
public:
    explicit Solver(WorldManager *wm, QObject *parent = nullptr);

public slots:
    void startReveal(int64_t x, int64_t y);
    void addToQueue(int64_t x, int64_t y);
    void processQueueIfIdle();

public:
    // Прерывает открытие клеток и дожидается, пока поток решателя освободится.
    // Все клетки, до которых очередь не дошла, помечаются ACTIVE — при следующей
    // загрузке мира они снова встанут в очередь. Вызывается из потока UI,
    // поэтому после возврата из функции регион можно безопасно сохранять.
    void requestCancel();

    // Забирает число открытых с прошлого вызова клеток. Счётчик атомарный,
    // поэтому UI может узнать результат сразу перед сохранением мира,
    // не дожидаясь очереди событий.
    int takeOpenedTotal() { return m_openedTotal.exchange(0); }

signals:
    void finished();

protected:
    // Единственная точка записи хода игры. Виртуальная: через неё тесты
    // сверяют порядок открытия клеток с исходным рекурсивным алгоритмом.
    virtual void writeCell(ChunkWindow &window, int64_t x, int64_t y, CellState state);

private:
    // Единый цикл обработки очереди: рекурсии нет, поэтому открытие можно
    // прервать в любой момент, а стек не переполняется на больших разлётах.
    void runQueue();
    bool isCancelled() const { return m_cancelRequested.load(); }
    // Клетки, которые ждали открытия, но не дождались, сохраняем как ACTIVE.
    void markPendingAsActive();

    // Открывает одну клетку. Соседи кладутся в стек в обратном порядке,
    // поэтому обход идёт вглубь ровно так же, как в исходной рекурсии.
    void processCell(int64_t x, int64_t y, ChunkWindow &window);
    int countNeighborMines(int64_t x, int64_t y);

    WorldManager *m_wm;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelRequested{false};
    QMutex m_queueMutex;
    QSet<QPair<int64_t, int64_t>> m_activeQueue;        // наполняется из потока загрузчика
    std::vector<std::pair<int64_t, int64_t>> m_pending; // стек обхода, только поток обработки
    std::atomic<int> m_openedTotal{0};                  // забирает поток UI

    // Для ожидания завершения обработки при прерывании.
    QMutex m_idleMutex;
    QWaitCondition m_idleCondition;
};

#endif // SOLVER_H