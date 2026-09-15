#include "worldmanager.h"
#include <QThread>
#include <QtMath>
#include <cmath>

double WorldManager::mineProbabilityFor(int difficulty) {
    int level = clampDifficulty(difficulty);
    double t = static_cast<double>(level - MIN_DIFFICULTY) / (MAX_DIFFICULTY - MIN_DIFFICULTY);
    // Экспоненциальная интерполяция: 1 -> MIN, MAX_DIFFICULTY -> MAX
    return MIN_MINE_PROBABILITY * std::pow(MAX_MINE_PROBABILITY / MIN_MINE_PROBABILITY, t);
}

int WorldManager::difficultyForProbability(double probability) {
    if (!(probability > 0.0)) return MIN_DIFFICULTY;
    double p = qBound(MIN_MINE_PROBABILITY, probability, MAX_MINE_PROBABILITY);
    double t = std::log(p / MIN_MINE_PROBABILITY) / std::log(MAX_MINE_PROBABILITY / MIN_MINE_PROBABILITY);
    double level = MIN_DIFFICULTY + t * (MAX_DIFFICULTY - MIN_DIFFICULTY);
    return clampDifficulty(static_cast<int>(std::lround(level)));
}

WorldManager::WorldManager(const QString &seed, int difficulty, QObject *parent)
    : QObject(parent), m_seed(seed), m_difficulty(clampDifficulty(difficulty))
{
    m_mineProbability = mineProbabilityFor(m_difficulty);
    m_mineChanceNumerator = static_cast<uint64_t>(qRound64(m_mineProbability * CHANCE_SCALE));
    computeHash();
}

void WorldManager::setSeed(const QString &seed) {
    m_seed = seed;
    computeHash();
}

void WorldManager::computeHash() {
    // Простой хеш, можно использовать QCryptographicHash::sha256 и взять первые 8 байт
    QByteArray ba = m_seed.toUtf8();
    uint64_t h = 0;
    for (char c : ba) {
        h = (h * 131) + static_cast<uint8_t>(c);
    }
    // дополнительное перемешивание
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    m_hash = h;
}

bool WorldManager::isMine(int64_t x, int64_t y) const {
    if (m_mineChanceNumerator == 0) return false;
    uint64_t h = m_hash;
    // смешиваем с координатами
    h ^= static_cast<uint64_t>(x) * 0x9e3779b97f4a7c15ULL;
    h ^= static_cast<uint64_t>(y) * 0xbf58476d1ce4e5b9ULL;
    // дополнительное перемешивание
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (h % CHANCE_SCALE) < m_mineChanceNumerator;
}

CellState WorldManager::cellStateAt(int64_t x, int64_t y) const {
    QReadLocker locker(&m_lock);
    int64_t cx = chunkIdx(x);
    int64_t cy = chunkIdx(y);
    auto it = m_chunks.constFind({cx, cy});
    if (it == m_chunks.constEnd())
        return CellState::CLOSED;
    int lx = x & 15;
    int ly = y & 15;
    return it.value()->cells[lx][ly];
}

void WorldManager::setCellState(int64_t x, int64_t y, CellState state) {
    QWriteLocker locker(&m_lock);
    int64_t cx = chunkIdx(x);
    int64_t cy = chunkIdx(y);
    auto chunk = getOrCreateChunk(cx, cy);
    int lx = x & 15;
    int ly = y & 15;
    if (chunk->cells[lx][ly] != state) {
        chunk->cells[lx][ly] = state;
        markChunkDirtyLocked(chunk);
    }
}

void WorldManager::applyRevealBatch(const std::vector<RevealUpdate> &updates) {
    if (updates.empty()) return;
    QWriteLocker locker(&m_lock);
    for (const RevealUpdate &u : updates) {
        auto chunk = getOrCreateChunk(chunkIdx(u.x), chunkIdx(u.y));
        CellState &cell = chunk->cells[u.x & 15][u.y & 15];
        if (cell == u.state) continue;
        cell = u.state;
        markChunkDirtyLocked(chunk);
    }
}

void WorldManager::setCellStateIfLoaded(int64_t x, int64_t y, CellState state) {
    QWriteLocker locker(&m_lock);
    int64_t cx = chunkIdx(x);
    int64_t cy = chunkIdx(y);
    auto it = m_chunks.find({cx, cy});
    if (it == m_chunks.end()) return;   // chunk not loaded, ignore
    auto chunk = it.value();
    int lx = x & 15;
    int ly = y & 15;
    if (chunk->cells[lx][ly] != state) {
        chunk->cells[lx][ly] = state;
        markChunkDirtyLocked(chunk);
    }
}

void WorldManager::setMine(int64_t x, int64_t y) {
    QWriteLocker locker(&m_lock);
    int64_t cx = chunkIdx(x);
    int64_t cy = chunkIdx(y);
    auto chunk = getOrCreateChunk(cx, cy);
    int lx = x & 15;
    int ly = y & 15;
    chunk->mine[lx][ly] = true;
    markChunkDirtyLocked(chunk);
}

void WorldManager::markChunkDirty(const std::shared_ptr<Chunk> &chunk) {
    if (!chunk) return;
    QWriteLocker locker(&m_lock);
    markChunkDirtyLocked(chunk);
}

void WorldManager::markChunkDirtyLocked(const std::shared_ptr<Chunk> &chunk) {
    // Блокировка уже взята вызывающим.
    chunk->useStamp = ++m_useCounter;
    if (!chunk->dirty) {
        chunk->dirty = true;
        ++m_dirtyCount;
        checkDirtyRatioLocked();
    }
}

void WorldManager::checkDirtyRatioLocked() {
    // Считаем по счётчику, а не перебором всех чанков: вызывается на каждой
    // изменённой клетке.
    if (MAX_CACHE <= 0 || m_saveRequested) return;
    if (m_dirtyCount >= static_cast<int>(MAX_CACHE * DIRTY_SAVE_TRIGGER)) {
        m_saveRequested = true;
        emit requestSave();
    }
}

void WorldManager::loadWindow(int64_t chunkX, int64_t chunkY, ChunkWindow &window) const {
    window = ChunkWindow();
    window.chunkX = chunkX;
    window.chunkY = chunkY;

    QReadLocker locker(&m_lock);
    window.center = m_chunks.value({chunkX, chunkY}, nullptr);
    window.ready = window.center != nullptr;

    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            window.ring[dy + 1][dx + 1] = m_chunks.value({chunkX + dx, chunkY + dy}, nullptr);
        }
}

bool WorldManager::revealCell(int64_t x, int64_t y) {
    if (!m_gameActive) return false;
    if (isMine(x, y)) {
        m_gameActive = false;
        setCellState(x, y, CellState::MINE_EXPLODED);
        emit mineExploded();
        return false;
    }
    return true;
}

void WorldManager::requestChunk(int64_t chunkX, int64_t chunkY) {
    emit chunkNeeded(chunkX, chunkY);
}

std::shared_ptr<Chunk> WorldManager::getChunk(int64_t chunkX, int64_t chunkY) {
    QReadLocker locker(&m_lock);
    auto it = m_chunks.constFind({chunkX, chunkY});
    if (it != m_chunks.constEnd())
        return it.value();
    return nullptr;
}

void WorldManager::addLoadedChunk(std::shared_ptr<Chunk> chunk) {
    {
        QWriteLocker locker(&m_lock);
        ChunkCoord coord{chunk->chunkX, chunk->chunkY};
        if (m_chunks.contains(coord))
            return;
        m_chunks[coord] = chunk;
        touchChunk(chunk);
        evictIfNeeded();
    }
    fixupChunkCells(chunk);
}

void WorldManager::fixupChunkCells(std::shared_ptr<Chunk> chunk) {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            CellState &st = chunk->cells[x][y];
            if (st >= CellState::OPENED_8 && st <= CellState::OPENED_0) {
                int64_t worldX = (chunk->chunkX << 4) + x;
                int64_t worldY = (chunk->chunkY << 4) + y;
                int mines = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx != 0 || dy != 0)
                            if (isMine(worldX + dx, worldY + dy))
                                ++mines;
                if (mines == 0)
                    st = CellState::OPENED_0;
                else
                    st = static_cast<CellState>(15 - mines);
            }
        }
    }
}

CellState WorldManager::cellStateAtUnsafe(int64_t x, int64_t y) const {
    int64_t cx = chunkIdx(x);
    int64_t cy = chunkIdx(y);
    auto it = m_chunks.constFind({cx, cy});
    if (it == m_chunks.constEnd())
        return CellState::CLOSED;
    int lx = x & 15;
    int ly = y & 15;
    return it.value()->cells[lx][ly];
}

std::shared_ptr<Chunk> WorldManager::getChunkUnsafe(int64_t chunkX, int64_t chunkY) const {
    auto it = m_chunks.constFind({chunkX, chunkY});
    if (it != m_chunks.constEnd())
        return it.value();
    return nullptr;
}

std::set<QPair<int64_t,int64_t>> WorldManager::dirtyRegions() const {
    QReadLocker locker(&m_lock);
    std::set<QPair<int64_t,int64_t>> regs;
    for (auto it = m_chunks.cbegin(); it != m_chunks.cend(); ++it) {
        if (it.value()->dirty) {
            int64_t rx = it.key().first >> (REGION_SHIFT - CHUNK_SHIFT);
            int64_t ry = it.key().second >> (REGION_SHIFT - CHUNK_SHIFT);
            regs.insert({rx, ry});
        }
    }
    return regs;
}

QHash<ChunkCoord, std::shared_ptr<Chunk>> WorldManager::getChunksForRegion(int64_t regX, int64_t regY) const {
    QReadLocker locker(&m_lock);
    QHash<ChunkCoord, std::shared_ptr<Chunk>> result;
    int64_t baseX = regX << 6;
    int64_t baseY = regY << 6;
    for (auto it = m_chunks.cbegin(); it != m_chunks.cend(); ++it) {
        int64_t cx = it.key().first;
        int64_t cy = it.key().second;
        if (cx >= baseX && cx < baseX + 64 && cy >= baseY && cy < baseY + 64)
            result.insert(it.key(), it.value());
    }
    return result;
}

void WorldManager::clearDirtyForRegions(const std::set<QPair<int64_t,int64_t>> &regions) {
    QWriteLocker locker(&m_lock);
    for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
        int64_t rx = it.key().first >> (REGION_SHIFT - CHUNK_SHIFT);
        int64_t ry = it.key().second >> (REGION_SHIFT - CHUNK_SHIFT);
        if (regions.count({rx, ry}) && it.value()->dirty) {
            it.value()->dirty = false;
            if (m_dirtyCount > 0) --m_dirtyCount;
        }
    }
    // После сброса счётчика запрос на сохранение снова может сработать.
    if (m_dirtyCount < static_cast<int>(MAX_CACHE * DIRTY_SAVE_TRIGGER))
        m_saveRequested = false;
}

void WorldManager::countStats(int &opened, int &flagged) const {
    QReadLocker locker(&m_lock);
    opened = 0;
    flagged = 0;
    for (const auto &chunk : m_chunks) {
        const auto &cells = chunk->cells;
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                CellState s = cells[i][j];
                if (s >= CellState::OPENED_8 && s <= CellState::OPENED_0)
                    ++opened;
                else if (s == CellState::FLAGGED)
                    ++flagged;
            }
        }
    }
}

std::shared_ptr<Chunk> WorldManager::getOrCreateChunk(int64_t cx, int64_t cy) {
    ChunkCoord coord{cx, cy};
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        touchChunk(it.value());
        return it.value();
    }
    auto chunk = std::make_shared<Chunk>();
    chunk->chunkX = cx;
    chunk->chunkY = cy;
    m_chunks[coord] = chunk;
    touchChunk(chunk);
    evictIfNeeded();
    return chunk;
}

void WorldManager::touchChunk(const std::shared_ptr<Chunk> &chunk) {
    // O(1): раньше здесь был линейный поиск и удаление из списка LRU,
    // а вызывалось это на каждую изменённую клетку.
    chunk->useStamp = ++m_useCounter;
}

void WorldManager::evictIfNeeded() {
    if (m_chunks.size() <= MAX_CACHE) return;

    // Освобождаем сразу десятую часть кэша, чтобы не сканировать его
    // при каждом новом чанке.
    const int target = MAX_CACHE * 9 / 10;
    while (m_chunks.size() > target) {
        ChunkCoord oldest;
        int64_t oldestStamp = 0;
        bool found = false;
        for (auto it = m_chunks.cbegin(); it != m_chunks.cend(); ++it) {
            if (it.value()->dirty) continue;      // грязные не вытесняем
            if (!found || it.value()->useStamp < oldestStamp) {
                oldestStamp = it.value()->useStamp;
                oldest = it.key();
                found = true;
            }
        }
        if (!found) return;                       // все чанки грязные
        m_chunks.remove(oldest);
    }
}