#ifndef CHUNK_H
#define CHUNK_H

#include <cstdint>
#include <cstring>
#include <QPair>

enum class CellState : uint8_t {
    CLOSED       = 0,
    FLAGGED      = 1,
    QUESTION     = 2,
    MINE_CLICKED = 3,
    MINE_WRONG   = 4,
    MINE_EXPLODED= 5,
    ACTIVE       = 6,
    OPENED_8     = 7,
    OPENED_7     = 8,
    OPENED_6     = 9,
    OPENED_5     = 10,
    OPENED_4     = 11,
    OPENED_3     = 12,
    OPENED_2     = 13,
    OPENED_1     = 14,
    OPENED_0     = 15
};

struct Chunk {
    int64_t chunkX = 0;
    int64_t chunkY = 0;
    CellState cells[16][16];
    bool mine[16][16];
    bool dirty = false;

    Chunk() {
        memset(cells, 0, sizeof(cells));
        memset(mine, 0, sizeof(mine));
    }
};

using ChunkCoord = QPair<int64_t, int64_t>;

#endif // CHUNK_H