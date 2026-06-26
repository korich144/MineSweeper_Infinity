#include "solver.h"
#include "worldmanager.h"

Solver::Solver(WorldManager *wm, QObject *parent)
    : QObject(parent), m_wm(wm)
{}

void Solver::startReveal(int64_t x, int64_t y) {
    if (!m_wm->isGameActive() || m_running) return;
    m_running = true;

    processCell(x, y);

    // обрабатываем очередь "отложенных" ACTIVE-клеток
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

    // на случай, если во время работы добавили ещё
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

    // Работаем только с клетками, чей чанк уже загружен
    int64_t cx = x >> 4;
    int64_t cy = y >> 4;
    auto chunk = m_wm->getChunk(cx, cy);
    if (!chunk) {
        // Такого быть не должно, но если чанка нет – просто выходим
        return;
    }

    CellState cur = m_wm->cellStateAt(x, y);
    if (cur != CellState::CLOSED && cur != CellState::ACTIVE)
        return;

    // Мина не должна открываться рекурсивно, но проверим
    if (m_wm->isMine(x, y)) return;

    // Проверяем, все ли соседние чанки загружены
    bool allNeighborsLoaded = true;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int64_t nx = x + dx;
            int64_t ny = y + dy;
            int64_t ncx = nx >> 4;
            int64_t ncy = ny >> 4;
            if (!m_wm->getChunk(ncx, ncy)) {
                allNeighborsLoaded = false;
                break;
            }
        }
        if (!allNeighborsLoaded) break;
    }

    // Если хотя бы один соседний чанк не загружен,
    // ставим текущую клетку в ACTIVE и останавливаем ветку
    if (!allNeighborsLoaded) {
        m_wm->setCellState(x, y, CellState::ACTIVE);
        return;
    }

    // Все соседние чанки загружены – можно определить число мин
    int mines = countNeighborMines(x, y);
    CellState newState;
    if (mines == 0)
        newState = CellState::OPENED_0;
    else
        newState = static_cast<CellState>(15 - mines);

    m_wm->setCellState(x, y, newState);

    // Если открыли пустую клетку – рекурсивно обходим соседей
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