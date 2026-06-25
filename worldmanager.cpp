#include "worldmanager.h"
#include <QThread>
#include <QtMath>

// Deterministic hash function for mine placement
static uint64_t seedHash(const QString &seed) {
    QByteArray ba = seed.toUtf8();
    uint64_t h = 0;
    for (char c : ba)
        h = (h * 131) + static_cast<uint8_t>(c);
    return h;
}

WorldManager::WorldManager(const QString &seed, int difficulty, QObject *parent)
    : QObject(parent), m_seed(seed), m_difficulty(difficulty)
{}

bool WorldManager::isMine(int64_t x, int64_t y) const {
    if (m_difficulty <= 0) return false;
    uint64_t h = seedHash(m_seed);
    h ^= static_cast<uint64_t>(x);
    h ^= static_cast<uint64_t>(y) << 1;
    h *= 0x9e3779b97f4a7c15ULL;
    return (h % m_difficulty) == 0;
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
        chunk->dirty = true;
        checkDirtyRatio();
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
        chunk->dirty = true;
        checkDirtyRatio();
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
    chunk->dirty = true;
    checkDirtyRatio();
}

bool WorldManager::revealCell(int64_t x, int64_t y) {
    if (!m_gameActive) return false;
    if (isMine(x, y)) {
        m_gameActive = false;
        setCellState(x, y, CellState::MINE_EXPLODED);
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
        touchChunk(coord);
        evictOneIfNeeded();
        checkDirtyRatio();
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
        if (regions.count({rx, ry}))
            it.value()->dirty = false;
    }
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
        touchChunk(coord);
        return it.value();
    }
    auto chunk = std::make_shared<Chunk>();
    chunk->chunkX = cx;
    chunk->chunkY = cy;
    m_chunks[coord] = chunk;
    touchChunk(coord);
    evictOneIfNeeded();
    checkDirtyRatio();
    return chunk;
}

void WorldManager::touchChunk(const ChunkCoord &coord) {
    m_lru.removeAll(coord);
    m_lru.push_front(coord);
}

void WorldManager::evictOneIfNeeded() {
    while (m_chunks.size() > MAX_CACHE) {
        bool removed = false;
        for (int i = m_lru.size() - 1; i >= 0; --i) {
            const ChunkCoord &coord = m_lru.at(i);
            if (auto chunk = m_chunks.value(coord)) {
                if (!chunk->dirty) {
                    m_chunks.remove(coord);
                    m_lru.removeAt(i);
                    removed = true;
                    break;
                }
            } else {
                m_lru.removeAt(i);
                removed = true;
                break;
            }
        }
        if (!removed) break;
    }
}

void WorldManager::checkDirtyRatio() {
    if (MAX_CACHE == 0) return;
    int dirtyCount = 0;
    for (const auto &chunk : m_chunks) {
        if (chunk->dirty) ++dirtyCount;
    }
    double ratio = static_cast<double>(dirtyCount) / MAX_CACHE;
    if (ratio >= DIRTY_SAVE_TRIGGER) {
        emit requestSave();
    }
}