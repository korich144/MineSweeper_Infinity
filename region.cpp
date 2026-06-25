#include "region.h"
#include <QFile>
#include <QDir>
#include <QMap>

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
                chunk->cells[x][y] = CellState::OPENED_0; // будет исправлено позже
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
    // Загружаем старый файл региона, чтобы не потерять чанки, которых нет в памяти
    QByteArray header(HEADER_SIZE, 0);
    QMap<int, QByteArray> existingData;  // индекс чанка -> его 64-байтные данные

    QFile oldFile(regionPath);
    if (oldFile.open(QIODevice::ReadOnly)) {
        QByteArray oldHeader = oldFile.read(HEADER_SIZE);
        if (oldHeader.size() == HEADER_SIZE) {
            header = oldHeader;
            // Считываем данные всех присутствующих чанков в порядке их индексов
            for (int i = 0; i < CHUNKS_PER_SIDE * CHUNKS_PER_SIDE; ++i) {
                if (isChunkPresent(header, i)) {
                    QByteArray data = oldFile.read(CHUNK_DATA_SIZE);
                    if (data.size() == CHUNK_DATA_SIZE) {
                        existingData[i] = data;
                    }
                }
            }
        }
        oldFile.close();
    }

    // Обновляем данные для чанков, переданных в кэше (они более актуальные)
    for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
        int64_t cx = it.key().first;
        int64_t cy = it.key().second;
        int idx = chunkIndex(cx, cy, regX, regY);
        if (idx < 0 || idx >= CHUNKS_PER_SIDE * CHUNKS_PER_SIDE) continue;

        const auto &cells = it.value()->cells;
        uint8_t opened[32] = {};
        uint8_t flagged[32] = {};
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
        QByteArray newData;
        newData.append(reinterpret_cast<const char*>(opened), 32);
        newData.append(reinterpret_cast<const char*>(flagged), 32);
        existingData[idx] = newData;
        setChunkPresent(header, idx, true);
    }

    // Записываем новый файл региона
    QDir().mkpath(QFileInfo(regionPath).absolutePath());
    QFile file(regionPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(header);
    for (int i = 0; i < CHUNKS_PER_SIDE * CHUNKS_PER_SIDE; ++i) {
        if (isChunkPresent(header, i)) {
            if (existingData.contains(i)) {
                file.write(existingData[i]);
            } else {
                // такого не должно случиться, но на всякий случай запишем нули
                file.write(QByteArray(CHUNK_DATA_SIZE, 0));
            }
        }
    }
    file.close();
}