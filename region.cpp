#include "region.h"
#include <QFile>
#include <QDir>
#include <QMap>

namespace {

// Сигнатура нового формата региона. Старые файлы её не содержат,
// поэтому их можно прочитать по прежней раскладке.
const char REGION_MAGIC[Region::MAGIC_SIZE + 1] = "MSWREG02";

inline int bitByte(int bit) { return bit / 8; }
inline int bitMask(int bit) { return 1 << (bit % 8); }

} // namespace

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

void Region::decodeChunk(Chunk &chunk, const QByteArray &data, bool hasActiveMap) {
    const uint8_t *opened = reinterpret_cast<const uint8_t*>(data.constData());
    const uint8_t *flagged = reinterpret_cast<const uint8_t*>(data.constData() + BITMAP_SIZE);
    const uint8_t *active = hasActiveMap
        ? reinterpret_cast<const uint8_t*>(data.constData() + BITMAP_SIZE * 2)
        : nullptr;

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            int bit = y * 16 + x;
            if (opened[bitByte(bit)] & bitMask(bit))
                chunk.cells[x][y] = CellState::OPENED_0;   // точное число мин посчитает fixupChunkCells
            else if (flagged[bitByte(bit)] & bitMask(bit))
                chunk.cells[x][y] = CellState::FLAGGED;
            else if (active && (active[bitByte(bit)] & bitMask(bit)))
                chunk.cells[x][y] = CellState::ACTIVE;     // ждёт открытия, встанет в очередь
            else
                chunk.cells[x][y] = CellState::CLOSED;
        }
    }
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

    const QByteArray magic = file.read(MAGIC_SIZE);
    const bool currentFormat = (magic.size() == MAGIC_SIZE && magic == QByteArray(REGION_MAGIC, MAGIC_SIZE));
    if (!currentFormat)
        file.seek(0);   // старое сохранение: заголовок начинается с нуля

    const int chunkDataSize = currentFormat ? CHUNK_DATA_SIZE : LEGACY_CHUNK_DATA_SIZE;

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
    const qint64 base = (currentFormat ? HEADER_OFFSET : 0) + HEADER_SIZE;
    file.seek(base + static_cast<qint64>(preceding) * chunkDataSize);
    QByteArray data = file.read(chunkDataSize);
    file.close();

    if (data.size() < chunkDataSize)
        return chunk;

    decodeChunk(*chunk, data, currentFormat);
    return chunk;
}

void Region::saveRegion(const QString &regionPath, int64_t regX, int64_t regY,
                        const QHash<ChunkCoord, std::shared_ptr<Chunk>> &chunks) {
    // Загружаем старый файл региона, чтобы не потерять чанки, которых нет в памяти
    QByteArray header(HEADER_SIZE, 0);
    QMap<int, QByteArray> existingData;  // индекс чанка -> его данные

    QFile oldFile(regionPath);
    if (oldFile.open(QIODevice::ReadOnly)) {
        const QByteArray oldMagic = oldFile.read(MAGIC_SIZE);
        const bool oldCurrentFormat =
            (oldMagic.size() == MAGIC_SIZE && oldMagic == QByteArray(REGION_MAGIC, MAGIC_SIZE));
        if (!oldCurrentFormat)
            oldFile.seek(0);

        const int oldChunkDataSize = oldCurrentFormat ? CHUNK_DATA_SIZE : LEGACY_CHUNK_DATA_SIZE;

        QByteArray oldHeader = oldFile.read(HEADER_SIZE);
        if (oldHeader.size() == HEADER_SIZE) {
            header = oldHeader;
            // Считываем данные всех присутствующих чанков в порядке их индексов
            for (int i = 0; i < CHUNKS_PER_SIDE * CHUNKS_PER_SIDE; ++i) {
                if (isChunkPresent(header, i)) {
                    QByteArray data = oldFile.read(oldChunkDataSize);
                    if (data.size() == oldChunkDataSize) {
                        if (oldCurrentFormat) {
                            existingData[i] = data;
                        } else {
                            // Дополняем старое сохранение пустой картой ACTIVE
                            QByteArray upgraded = data;
                            upgraded.append(QByteArray(BITMAP_SIZE, 0));
                            existingData[i] = upgraded;
                        }
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
        uint8_t opened[BITMAP_SIZE] = {};
        uint8_t flagged[BITMAP_SIZE] = {};
        uint8_t active[BITMAP_SIZE] = {};
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                int bit = y * 16 + x;
                CellState s = cells[x][y];
                if (s >= CellState::OPENED_8 && s <= CellState::OPENED_0)
                    opened[bitByte(bit)] |= bitMask(bit);
                else if (s == CellState::FLAGGED)
                    flagged[bitByte(bit)] |= bitMask(bit);
                else if (s == CellState::ACTIVE)
                    active[bitByte(bit)] |= bitMask(bit);
            }
        }
        QByteArray newData;
        newData.append(reinterpret_cast<const char*>(opened), BITMAP_SIZE);
        newData.append(reinterpret_cast<const char*>(flagged), BITMAP_SIZE);
        newData.append(reinterpret_cast<const char*>(active), BITMAP_SIZE);
        existingData[idx] = newData;
        setChunkPresent(header, idx, true);
    }

    // Записываем новый файл региона
    QDir().mkpath(QFileInfo(regionPath).absolutePath());
    QFile file(regionPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(REGION_MAGIC, MAGIC_SIZE);
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
