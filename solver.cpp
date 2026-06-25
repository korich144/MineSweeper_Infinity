#include "solver.h"
#include "worldmanager.h"

Solver::Solver(WorldManager *wm, QObject *parent)
    : QObject(parent), m_wm(wm)
{}

void Solver::startReveal(int64_t x, int64_t y) {
    if (!m_wm->isGameActive() || m_running) return;
    m_running = true;

    // initial cell might be a mine (handled before calling startReveal)
    // begin recursion
    processCell(x, y);

    // after recursion, handle any ACTIVE cells queued (they may have been queued due to missing chunks)
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
}

void Solver::addToQueue(int64_t x, int64_t y) {
    QMutexLocker lock(&m_queueMutex);
    m_activeQueue.insert({x, y});
}

void Solver::processCell(int64_t x, int64_t y, int depth) {
    if (depth > MAX_RECURSION || !m_wm->isGameActive()) return;

    CellState cur = m_wm->cellStateAt(x, y);
    if (cur != CellState::CLOSED && cur != CellState::ACTIVE)
        return;

    // verify chunk loaded
    int64_t cx = x >> 4;
    int64_t cy = y >> 4;
    if (!m_wm->getChunk(cx, cy)) {
        // chunk not loaded – mark ACTIVE and stop branch
        m_wm->setCellState(x, y, CellState::ACTIVE);
        m_wm->requestChunk(cx, cy);
        return;
    }

    // check if mine (should not happen, but safety)
    if (m_wm->isMine(x, y)) return;

    int mines = countNeighborMines(x, y);
    CellState newState;
    if (mines == 0)
        newState = CellState::OPENED_0;
    else
        newState = static_cast<CellState>(15 - mines); // OPENED_1..OPENED_8

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