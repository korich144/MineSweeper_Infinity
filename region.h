#ifndef REGION_H
#define REGION_H

#include <QString>
#include <QByteArray>
#include <QHash>
#include "chunk.h"

class Region {
public:
    // Длина сигнатуры нового формата региона (без неё файл считается старым).
    static constexpr int MAGIC_SIZE = 8;

    static std::shared_ptr<Chunk> loadChunk(const QString &regionPath,
                                            int64_t chunkX, int64_t chunkY,
                                            int64_t regX, int64_t regY);
    static void saveRegion(const QString &regionPath, int64_t regX, int64_t regY,
                           const QHash<ChunkCoord, std::shared_ptr<Chunk>> &chunks);

private:
    static constexpr int CHUNKS_PER_SIDE = 64;
    static constexpr int HEADER_SIZE = (CHUNKS_PER_SIDE * CHUNKS_PER_SIDE) / 8;
    // Три битовые карты по 32 байта: открытые, флажки и ACTIVE
    // (клетки, ожидающие открытия в очереди).
    static constexpr int BITMAP_SIZE = 32;
    static constexpr int CHUNK_DATA_SIZE = BITMAP_SIZE * 3;
    static constexpr int HEADER_OFFSET = MAGIC_SIZE;
    static constexpr int LEGACY_CHUNK_DATA_SIZE = BITMAP_SIZE * 2;

    static int chunkIndex(int64_t chunkX, int64_t chunkY, int64_t regX, int64_t regY);
    static bool isChunkPresent(const QByteArray &header, int index);
    static void setChunkPresent(QByteArray &header, int index, bool present);
    static void decodeChunk(Chunk &chunk, const QByteArray &data, bool hasActiveMap);
};

#endif // REGION_H