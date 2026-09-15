#include "solver.h"
#include "worldmanager.h"

namespace {
// Начальный размер стека обхода — по периметру крупного разлёта.
constexpr size_t PENDING_RESERVE = 4096;

// Клетке нужны соседние чанки только если она лежит на краю своего чанка:
// в середине все восемь соседей находятся в том же чанке. Проверяем ровно
// то, что нужно, а не всё окно 3x3 — иначе внутренние клетки у границы
// загруженной области зря помечались бы ожидающими.
bool neighboursLoaded(const ChunkWindow &window, int64_t x, int64_t y) {
    const int lx = static_cast<int>(x & 15);
    const int ly = static_cast<int>(y & 15);
    const bool left = (lx == 0);
    const bool right = (lx == 15);
    const bool top = (ly == 0);
    const bool bottom = (ly == 15);
    if (!left && !right && !top && !bottom)
        return true;   // внутренняя клетка — хватает своего чанка

    if (left && !window.ring[1][0]) return false;
    if (right && !window.ring[1][2]) return false;
    if (top && !window.ring[0][1]) return false;
    if (bottom && !window.ring[2][1]) return false;
    if (left && top && !window.ring[0][0]) return false;
    if (right && top && !window.ring[0][2]) return false;
    if (left && bottom && !window.ring[2][0]) return false;
    if (right && bottom && !window.ring[2][2]) return false;
    return true;
}
} // namespace

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

    m_pending.clear();
    if (m_pending.capacity() < PENDING_RESERVE)
        m_pending.reserve(PENDING_RESERVE);

    // Окно чанков кэшируется: блокировка берётся при переходе в другой чанк,
    // а не на каждую клетку. Обход в глубину держится внутри чанка подолгу.
    ChunkWindow window;
    int64_t windowChunkX = 0;
    int64_t windowChunkY = 0;
    bool haveWindow = false;

    bool cancelled = false;
    while (true) {
        if (isCancelled()) {
            cancelled = true;
            break;
        }

        int64_t x = 0, y = 0;
        bool haveCell = false;

        if (!m_pending.empty()) {
            x = m_pending.back().first;
            y = m_pending.back().second;
            m_pending.pop_back();
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

        const int64_t cx = x >> 4;
        const int64_t cy = y >> 4;
        if (!haveWindow || cx != windowChunkX || cy != windowChunkY) {
            m_wm->loadWindow(cx, cy, window);
            windowChunkX = cx;
            windowChunkY = cy;
            haveWindow = true;
        }
        if (window.ready)
            processCell(x, y, window);
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

void Solver::processCell(int64_t x, int64_t y, ChunkWindow &window) {
    if (!m_wm->isGameActive()) return;

    const CellState cur = window.center->cells[x & 15][y & 15];
    if (cur != CellState::CLOSED && cur != CellState::ACTIVE) return;
    if (m_wm->isMine(x, y)) return;

    if (!neighboursLoaded(window, x, y)) {
        // Граница с непрогруженным чанком: клетка ждёт своей очереди.
        writeCell(window, x, y, CellState::ACTIVE);
        return;
    }

    const int mines = countNeighborMines(x, y);
    const CellState opened = (mines == 0)
        ? CellState::OPENED_0
        : static_cast<CellState>(15 - mines);

    writeCell(window, x, y, opened);
    ++m_openedTotal;

    if (mines != 0) return;

    // Соседи кладутся в стек в обратном порядке: первым будет обработан
    // (-1,-1) и его окрестность — тот же порядок, что давала исходная рекурсия.
    for (int dy = 1; dy >= -1; --dy) {
        for (int dx = 1; dx >= -1; --dx) {
            if (dx == 0 && dy == 0) continue;
            const int64_t nx = x + dx;
            const int64_t ny = y + dy;

            // Чанк соседа: тот же, если клетка не на краю, иначе — из окна.
            const auto &neighbourChunk = window.chunkAt(nx >> 4, ny >> 4);
            if (!neighbourChunk) continue;

            const CellState state = neighbourChunk->cells[nx & 15][ny & 15];
            if (state != CellState::CLOSED && state != CellState::ACTIVE) continue;
            if (m_wm->isMine(nx, ny)) continue;

            m_pending.push_back({nx, ny});
        }
    }
}

void Solver::writeCell(ChunkWindow &window, int64_t x, int64_t y, CellState state) {
    CellState &cell = window.center->cells[x & 15][y & 15];
    if (cell == state) return;
    cell = state;

    // Пишем в чанк напрямую (как это делает и отрисовка), а менеджеру
    // сообщаем один раз на чанк: он ведёт счётчик грязных чанков и не даёт
    // вытеснить чанк из кэша, пока в него пишут.
    if (!window.center->dirty)
        m_wm->markChunkDirty(window.center);
}

void Solver::markPendingAsActive() {
    // Клетки, которые ждали открытия, но не дождались: сохраняем их как ACTIVE,
    // чтобы после загрузки мира они снова попали в очередь. Уже открытые клетки
    // в очереди не трогаем — иначе открытая клетка стала бы «ждущей».
    ChunkWindow window;
    int64_t windowChunkX = 0;
    int64_t windowChunkY = 0;
    bool haveWindow = false;

    auto mark = [&](int64_t x, int64_t y) {
        const int64_t cx = x >> 4;
        const int64_t cy = y >> 4;
        if (!haveWindow || cx != windowChunkX || cy != windowChunkY) {
            m_wm->loadWindow(cx, cy, window);
            windowChunkX = cx;
            windowChunkY = cy;
            haveWindow = true;
        }
        if (!window.ready) return;
        if (window.center->cells[x & 15][y & 15] == CellState::CLOSED)
            writeCell(window, x, y, CellState::ACTIVE);
    };

    for (const auto &cell : m_pending)
        mark(cell.first, cell.second);

    QMutexLocker lock(&m_queueMutex);
    for (const auto &cell : m_activeQueue)
        mark(cell.first, cell.second);
    m_activeQueue.clear();
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