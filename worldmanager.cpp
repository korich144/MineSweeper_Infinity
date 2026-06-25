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
        return CellState::CLOSED;   // unloaded chunk = all closed
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
    QWriteLocker locker(&m_lock);
    ChunkCoord coord{chunk->chunkX, chunk->chunkY};
    if (m_chunks.contains(coord))
        return;
    m_chunks[coord] = chunk;
    touchChunk(coord);
    evictOneIfNeeded();
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
    int64_t baseX = regX << 6; // 64 chunks per region side
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
    return chunk;
}

void WorldManager::touchChunk(const ChunkCoord &coord) {
    auto mapIt = m_lruMap.find(coord);
    if (mapIt != m_lruMap.end())
        m_lru.erase(*mapIt);
    m_lru.push_front(coord);
    m_lruMap[coord] = m_lru.begin();
}

void WorldManager::evictOneIfNeeded() {
    while (m_chunks.size() > MAX_CACHE) {
        bool removed = false;
        for (auto it = m_lru.rbegin(); it != m_lru.rend(); ++it) {
            if (auto chunk = m_chunks.value(*it)) {
                if (!chunk->dirty) {
                    m_chunks.remove(*it);
                    m_lruMap.remove(*it);
                    m_lru.erase(std::find(m_lru.begin(), m_lru.end(), *it));
                    removed = true;
                    break;
                }
            }
        }
        if (!removed) break; // all loaded chunks are dirty – can't evict
    }
}