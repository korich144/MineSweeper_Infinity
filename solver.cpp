#include "solver.h"
#include "worldmanager.h"

Solver::Solver(WorldManager *wm, QObject *parent)
    : QObject(parent), m_wm(wm)
{}

void Solver::startReveal(int64_t x, int64_t y) {
    if (!m_wm->isGameActive() || m_running) return;
    m_running = true;

    processCell(x, y);

    while (true) {
        QMutexLocker lock(&m_queueMutex);
        if (m_activeQueue.isEmpty()) break;
        auto cell = *m_activeQueue.begin();
        m_activeQueue.erase(m_activeQueue.begin());
        lock.unlock();
        processCell(cell.first, cell.second);
    }

    m_running = false;
    emit finished();

    processQueueIfIdle();
}

void Solver::addToQueue(int64_t x, int64_t y) {
    QMutexLocker lock(&m_queueMutex);
    m_activeQueue.insert({x, y});
}

void Solver::processQueueIfIdle() {
    if (m_running) return;
    m_running = true;
    while (true) {
        QMutexLocker lock(&m_queueMutex);
        if (m_activeQueue.isEmpty()) break;
        auto cell = *m_activeQueue.begin();
        m_activeQueue.erase(m_activeQueue.begin());
        lock.unlock();
        processCell(cell.first, cell.second);
    }
    m_running = false;
}

void Solver::processCell(int64_t x, int64_t y, int depth) {
    if (depth > MAX_RECURSION || !m_wm->isGameActive()) return;

    CellState cur = m_wm->cellStateAt(x, y);
    if (cur != CellState::CLOSED && cur != CellState::ACTIVE)
        return;

    int64_t cx = x >> 4;
    int64_t cy = y >> 4;
    if (!m_wm->getChunk(cx, cy)) {
        // чанк не загружен – ставим ACTIVE и останавливаем ветку (без запроса)
        m_wm->setCellStateIfLoaded(x, y, CellState::ACTIVE); // не создаёт чанк
        return;
    }

    if (m_wm->isMine(x, y)) return;

    int mines = countNeighborMines(x, y);
    CellState newState;
    if (mines == 0)
        newState = CellState::OPENED_0;
    else
        newState = static_cast<CellState>(15 - mines);

    m_wm->setCellState(x, y, newState);

    if (mines == 0) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                processCell(x + dx, y + dy, depth + 1);
            }
        }
    }
}

int Solver::countNeighborMines(int64_t x, int64_t y) {
    int cnt = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx != 0 || dy != 0)
                if (m_wm->isMine(x + dx, y + dy))
                    ++cnt;
    return cnt;
}