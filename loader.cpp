#include "loader.h"
#include "worldmanager.h"
#include "solver.h"
#include "region.h"
#include <QTimer>

Loader::Loader(WorldManager *wm, Solver *solver, const QString &worldDir, QObject *parent)
    : QObject(parent), m_wm(wm), m_solver(solver), m_worldDir(worldDir)
{
    connect(wm, &WorldManager::chunkNeeded, this, &Loader::requestChunk, Qt::QueuedConnection);
    // periodic queue flush
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Loader::processQueue);
    timer->start(50);
}

void Loader::requestChunk(int64_t chunkX, int64_t chunkY) {
    QMutexLocker lock(&m_mutex);
    m_pending.insert({chunkX, chunkY});
}

void Loader::processQueue() {
    QSet<QPair<int64_t,int64_t>> toLoad;
    {
        QMutexLocker lock(&m_mutex);
        toLoad = m_pending;
        m_pending.clear();
    }
    for (const auto &p : toLoad)
        loadChunk(p.first, p.second);
}

void Loader::loadChunk(int64_t chunkX, int64_t chunkY) {
    if (m_wm->getChunk(chunkX, chunkY)) return;

    int64_t regX = chunkX >> 6;   // 64 chunks per region
    int64_t regY = chunkY >> 6;
    QString path = QString("%1/regions/r%2_r%3.dat").arg(m_worldDir).arg(regX).arg(regY);

    auto chunk = Region::loadChunk(path, chunkX, chunkY, regX, regY);
    m_wm->addLoadedChunk(chunk);
    scanForActive(chunkX, chunkY);
}

void Loader::scanForActive(int64_t chunkX, int64_t chunkY) {
    auto check = [this](int64_t x, int64_t y) {
        if (m_wm->cellStateAt(x, y) == CellState::ACTIVE) {
            m_solver->addToQueue(x, y);
            m_solver->processQueueIfIdle();   // <-- запрос на обработку
        }
    };

    // Клетки внутри самого чанка: они могли быть сохранены как ACTIVE,
    // если открытие прервали (игра закрыта или создан новый мир).
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
            check(chunkX * 16 + i, chunkY * 16 + j);

    // Клетки соседних чанков, которые ждали загрузки этого.
    for (int i = 0; i < 16; ++i) check(chunkX * 16 - 1, chunkY * 16 + i);
    for (int i = 0; i < 16; ++i) check((chunkX + 1) * 16, chunkY * 16 + i);
    for (int i = 0; i < 16; ++i) check(chunkX * 16 + i, chunkY * 16 - 1);
    for (int i = 0; i < 16; ++i) check(chunkX * 16 + i, (chunkY + 1) * 16);
}