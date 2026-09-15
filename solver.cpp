#include "solver.h"
#include "worldmanager.h"

Solver::Solver(WorldManager *wm, QObject *parent)
    : QObject(parent), m_wm(wm)
{}

void Solver::startReveal(int64_t x, int64_t y) {
    if (isCancelled() || !m_wm->isGameActive()) return;

    {
        QMutexLocker lock(&m_queueMutex);
        m_activeQueue.insert({x, y});
    }
    runQueue();
}

void Solver::addToQueue(int64_t x, int64_t y) {
    QMutexLocker lock(&m_queueMutex);
    m_activeQueue.insert({x, y});
}

void Solver::processQueueIfIdle() {
    runQueue();
}

void Solver::runQueue() {
    // Обработку ведёт только один поток: либо поток решателя, либо поток
    // загрузчика, который дошёл до границы с ACTIVE-клетками.
    if (m_running.exchange(true)) return;

    bool cancelled = false;

    while (true) {
        if (isCancelled()) {
            cancelled = true;
            break;
        }

        int64_t x = 0, y = 0;
        bool haveCell = false;

        if (!m_pending.isEmpty()) {
            auto it = m_pending.begin();
            x = it->first;
            y = it->second;
            m_pending.erase(it);
            haveCell = true;
        } else {
            QMutexLocker lock(&m_queueMutex);
            if (!m_activeQueue.isEmpty()) {
                auto it = m_activeQueue.begin();
                x = it->first;
                y = it->second;
                m_activeQueue.erase(it);
                haveCell = true;
            }
        }

        if (!haveCell) break;
        processCell(x, y);
    }

    if (cancelled)
        markPendingAsActive();
    m_pending.clear();

    m_running.store(false);

    {
        QMutexLocker lock(&m_idleMutex);
        m_idleCondition.wakeAll();
    }

    emit finished();
}

void Solver::markPendingAsActive() {
    // Клетки, которые ждали открытия, но не дождались: сохраняем их как ACTIVE,
    // чтобы после загрузки мира они снова попали в очередь. Уже открытые клетки
    // в очереди не трогаем — иначе открытая клетка стала бы «ждущей».
    auto mark = [this](int64_t x, int64_t y) {
        if (m_wm->cellStateAt(x, y) == CellState::CLOSED)
            m_wm->setCellStateIfLoaded(x, y, CellState::ACTIVE);
    };

    for (const auto &cell : m_pending)
        mark(cell.first, cell.second);

    QMutexLocker lock(&m_queueMutex);
    for (const auto &cell : m_activeQueue)
        mark(cell.first, cell.second);
    m_activeQueue.clear();
}

void Solver::requestCancel() {
    // Флаг не снимается: после прерывания мир всегда пересоздаётся вместе
    // с движком, а так исключены гонки с запоздавшими запросами на открытие.
    m_cancelRequested.store(true);

    // Ждём, пока поток обработки закончит текущую клетку: после этого
    // регион больше никто не меняет и его можно сохранять.
    QMutexLocker lock(&m_idleMutex);
    while (m_running.load())
        m_idleCondition.wait(&m_idleMutex, 100);
}

void Solver::processCell(int64_t x, int64_t y) {
    if (!m_wm->isGameActive()) return;

    int64_t cx = x >> 4;
    int64_t cy = y >> 4;
    auto chunk = m_wm->getChunk(cx, cy);
    if (!chunk) return;

    CellState cur = m_wm->cellStateAt(x, y);
    if (cur != CellState::CLOSED && cur != CellState::ACTIVE)
        return;

    if (m_wm->isMine(x, y)) return;

    bool allNeighborsLoaded = true;
    for (int dy = -1; dy <= 1 && allNeighborsLoaded; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int64_t ncx = (x + dx) >> 4;
            int64_t ncy = (y + dy) >> 4;
            if (!m_wm->getChunk(ncx, ncy)) {
                allNeighborsLoaded = false;
                break;
            }
        }
    }

    if (!allNeighborsLoaded) {
        // ставим ACTIVE на границе с непрогруженным чанком
        m_wm->setCellState(x, y, CellState::ACTIVE);
        return;
    }

    const int mines = countNeighborMines(x, y);
    const CellState newState = (mines == 0)
        ? CellState::OPENED_0
        : static_cast<CellState>(15 - mines);

    m_wm->setCellState(x, y, newState);
    ++m_openedTotal;

    if (mines == 0) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                m_pending.insert({x + dx, y + dy});
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
