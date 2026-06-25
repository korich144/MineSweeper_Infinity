#include "region.h"
#include <QFile>
#include <QDir>

int Region::chunkIndex(int64_t chunkX, int64_t chunkY, int64_t regX, int64_t regY) {
    int lx = static_cast<int>(chunkX - regX * CHUNKS_PER_SIDE);
    int ly = static_cast<int>(chunkY - regY * CHUNKS_PER_SIDE);
    return ly * CHUNKS_PER_SIDE + lx;
}

bool Region::isChunkPresent(const QByteArray &header, int index) {
    int byteIdx = index / 8;
    int bitIdx = index % 8;
    return (static_cast<uint8_t>(header[byteIdx]) & (1 << bitIdx)) != 0;
}

void Region::setChunkPresent(QByteArray &header, int index, bool present) {
    int byteIdx = index / 8;
    int bitIdx = index % 8;
    if (present)
        header[byteIdx] = static_cast<char>(header[byteIdx] | (1 << bitIdx));
    else
        header[byteIdx] = static_cast<char>(header[byteIdx] & ~(1 << bitIdx));
}

std::shared_ptr<Chunk> Region::loadChunk(const QString &regionPath,
                                         int64_t chunkX, int64_t chunkY,
                                         int64_t regX, int64_t regY) {
    auto chunk = std::make_shared<Chunk>();
    chunk->chunkX = chunkX;
    chunk->chunkY = chunkY;

    QFile file(regionPath);
    if (!file.open(QIODevice::ReadOnly))
        return chunk;

    QByteArray header = file.read(HEADER_SIZE);
    if (header.size() < HEADER_SIZE) {
        file.close();
        return chunk;
    }

    int idx = chunkIndex(chunkX, chunkY, regX, regY);
    if (!isChunkPresent(header, idx)) {
        file.close();
        return chunk;
    }

    int preceding = 0;
    for (int i = 0; i < idx; ++i) {
        if (isChunkPresent(header, i))
            ++preceding;
    }
    qint64 offset = HEADER_SIZE + static_cast<qint64>(preceding) * CHUNK_DATA_SIZE;
    file.seek(offset);
    QByteArray data = file.read(CHUNK_DATA_SIZE);
    file.close();

    if (data.size() < CHUNK_DATA_SIZE)
        return chunk;

    const uint8_t *opened = reinterpret_cast<const uint8_t*>(data.constData());
    const uint8_t *flagged = reinterpret_cast<const uint8_t*>(data.constData() + 32);

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            int bitIdx = y * 16 + x;
            int byte = bitIdx / 8;
            int mask = 1 << (bitIdx % 8);
            if (opened[byte] & mask)
                chunk->cells[x][y] = CellState::OPENED_0; // placeholder; number will be computed on demand
            else if (flagged[byte] & mask)
                chunk->cells[x][y] = CellState::FLAGGED;
            else
                chunk->cells[x][y] = CellState::CLOSED;
        }
    }
    return chunk;
}

void Region::saveRegion(const QString &regionPath, int64_t regX, int64_t regY,
                        const QHash<ChunkCoord, std::shared_ptr<Chunk>> &chunks) {
    QByteArray header(HEADER_SIZE, 0);
    QVector<QByteArray> chunkData;

    for (int ly = 0; ly < CHUNKS_PER_SIDE; ++ly) {
        for (int lx = 0; lx < CHUNKS_PER_SIDE; ++lx) {
            int64_t cx = regX * CHUNKS_PER_SIDE + lx;
            int64_t cy = regY * CHUNKS_PER_SIDE + ly;
            ChunkCoord coord{cx, cy};
            auto it = chunks.find(coord);
            if (it != chunks.end() && it.value()->dirty) {
                uint8_t opened[32] = {};
                uint8_t flagged[32] = {};
                const auto &cells = it.value()->cells;
                for (int y = 0; y < 16; ++y) {
                    for (int x = 0; x < 16; ++x) {
                        int bit = y * 16 + x;
                        CellState s = cells[x][y];
                        if (s >= CellState::OPENED_8 && s <= CellState::OPENED_0)
                            opened[bit / 8] |= (1 << (bit % 8));
                        else if (s == CellState::FLAGGED)
                            flagged[bit / 8] |= (1 << (bit % 8));
                    }
                }
                QByteArray cd;
                cd.append(reinterpret_cast<const char*>(opened), 32);
                cd.append(reinterpret_cast<const char*>(flagged), 32);
                chunkData.append(cd);
                int idx = ly * CHUNKS_PER_SIDE + lx;
                setChunkPresent(header, idx, true);
            }
        }
    }

    QDir().mkpath(regionPath.section('/', 0, -2));
    QFile file(regionPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(header);
        for (const auto &cd : chunkData)
            file.write(cd);
        file.close();
    }
}