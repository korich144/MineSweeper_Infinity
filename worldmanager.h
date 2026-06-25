#ifndef WORLDMANAGER_H
#define WORLDMANAGER_H

#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QList>
#include <memory>
#include <set>
#include "chunk.h"

class WorldManager : public QObject
{
    Q_OBJECT
public:
    explicit WorldManager(const QString &seed, int difficulty, QObject *parent = nullptr);

    void setSeed(const QString &seed) { m_seed = seed; }
    QString seed() const { return m_seed; }
    int difficulty() const { return m_difficulty; }

    bool isMine(int64_t x, int64_t y) const;
    CellState cellStateAt(int64_t x, int64_t y) const;

    void setCellState(int64_t x, int64_t y, CellState state);
    void setMine(int64_t x, int64_t y);

    bool revealCell(int64_t x, int64_t y);   // check for mine; returns true if not mine
    void requestChunk(int64_t chunkX, int64_t chunkY); // emit chunkNeeded

    std::shared_ptr<Chunk> getChunk(int64_t chunkX, int64_t chunkY);
    void addLoadedChunk(std::shared_ptr<Chunk> chunk);

    std::set<QPair<int64_t,int64_t>> dirtyRegions() const;
    QHash<ChunkCoord, std::shared_ptr<Chunk>> getChunksForRegion(int64_t regX, int64_t regY) const;
    void clearDirtyForRegions(const std::set<QPair<int64_t,int64_t>> &regions);

    void countStats(int &opened, int &flagged) const;

    bool isGameActive() const { return m_gameActive; }
    void setGameActive(bool active) { m_gameActive = active; }

signals:
    void chunkNeeded(int64_t chunkX, int64_t chunkY);

private:
    static constexpr int REGION_SHIFT = 10;
    static constexpr int CHUNK_SHIFT  = 4;
    static constexpr int MAX_CACHE    = 512;

    int64_t chunkIdx(int64_t cell) const { return cell >> CHUNK_SHIFT; }
    int64_t regionIdx(int64_t cell) const { return cell >> REGION_SHIFT; }

    std::shared_ptr<Chunk> getOrCreateChunk(int64_t cx, int64_t cy);
    void touchChunk(const ChunkCoord &coord);
    void evictOneIfNeeded();

    mutable QReadWriteLock m_lock;
    QHash<ChunkCoord, std::shared_ptr<Chunk>> m_chunks;
    QList<ChunkCoord> m_lru;

    QString m_seed;
    int m_difficulty;
    bool m_gameActive = true;
};

#endif // WORLDMANAGER_H