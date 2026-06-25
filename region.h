#ifndef REGION_H
#define REGION_H

#include <QString>
#include <QByteArray>
#include <QHash>
#include "chunk.h"

class Region {
public:
    static std::shared_ptr<Chunk> loadChunk(const QString &regionPath,
                                            int64_t chunkX, int64_t chunkY,
                                            int64_t regX, int64_t regY);
    static void saveRegion(const QString &regionPath, int64_t regX, int64_t regY,
                           const QHash<ChunkCoord, std::shared_ptr<Chunk>> &chunks);

private:
    static constexpr int CHUNKS_PER_SIDE = 64;
    static constexpr int HEADER_SIZE = (CHUNKS_PER_SIDE * CHUNKS_PER_SIDE) / 8; // 512
    static constexpr int CHUNK_DATA_SIZE = 64;

    static int chunkIndex(int64_t chunkX, int64_t chunkY, int64_t regX, int64_t regY);
    static bool isChunkPresent(const QByteArray &header, int index);
    static void setChunkPresent(QByteArray &header, int index, bool present);
};

#endif // REGION_H