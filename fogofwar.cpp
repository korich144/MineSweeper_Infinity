#include "fogofwar.h"
#include "worldmanager.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

// Клетка видна игроку, если она открыта или уже «отыграна» миной: по этим
// клеткам туман и отсчитывается.
bool isRevealed(CellState s) {
    switch (s) {
    case CellState::OPENED_8:
    case CellState::OPENED_7:
    case CellState::OPENED_6:
    case CellState::OPENED_5:
    case CellState::OPENED_4:
    case CellState::OPENED_3:
    case CellState::OPENED_2:
    case CellState::OPENED_1:
    case CellState::OPENED_0:
    case CellState::MINE_CLICKED:
    case CellState::MINE_WRONG:
    case CellState::MINE_EXPLODED:
        return true;
    default:
        return false;
    }
}

// Шаг расстояния с насыщением: 255+1 должно остаться 255, а не обернуться в 0.
int step(int v) { return v >= 255 ? 255 : v + 1; }

} // namespace

void FogMap::setWorldManager(WorldManager *wm) {
    m_wm = wm;
    // Ревизия принадлежит прошлому менеджеру, кэш расстояний недействителен.
    m_valid = false;
}

void FogMap::setTheme(const QVector<QPixmap> &cellSprites, const QColor &fogColor) {
    m_fogColor = fogColor;
    // Цвет используется и как цель смешивания, и как сплошная заливка 4-й стадии.
    m_fogColor.setAlpha(255);

    for (int s = 0; s < 16; ++s)
        for (int stage = 0; stage <= TRANSITION_STAGES; ++stage)
            m_fogged[s][stage] = QPixmap();

    if (cellSprites.size() < 16) return;

    for (int s = 0; s < 16; ++s) {
        const QPixmap &base = cellSprites.at(s);
        if (base.isNull()) continue;
        for (int stage = 1; stage < TRANSITION_STAGES; ++stage) {
            QColor blend = m_fogColor;
            blend.setAlpha(255 * stage / TRANSITION_STAGES);   // 25 / 50 / 75 %
            QPixmap tinted = base;
            QPainter p(&tinted);
            p.fillRect(tinted.rect(), blend);
            p.end();
            m_fogged[s][stage] = tinted;
        }
    }
    // Расстояния от темы не зависят, поэтому m_valid не трогаем.
}

void FogMap::update(int64_t minX, int64_t minY, int64_t maxX, int64_t maxY) {
    if (!m_wm) return;

    // Расстояние считается до ближайшей открытой клетки, в том числе за
    // пределами экрана, поэтому запрошенный прямоугольник расширяем на радиус.
    const int64_t reqMinX = minX - CLEAR_RADIUS;
    const int64_t reqMinY = minY - CLEAR_RADIUS;
    const int64_t reqMaxX = maxX + CLEAR_RADIUS;
    const int64_t reqMaxY = maxY + CLEAR_RADIUS;

    const bool inside = m_valid &&
                        reqMinX >= m_minX && reqMinY >= m_minY &&
                        reqMaxX < m_minX + m_w && reqMaxY < m_minY + m_h;

    if (!m_valid || m_wm->fieldRevision() != m_revision || !inside)
        rebuild(reqMinX, reqMinY, reqMaxX, reqMaxY);
}

void FogMap::rebuild(int64_t minX, int64_t minY, int64_t maxX, int64_t maxY) {
    // Запас на мелкие сдвиги камеры. Для крупных областей он не нужен и только
    // раздувал бы буфер.
    int64_t bw = maxX - minX + 1;
    int64_t bh = maxY - minY + 1;
    if (bw > 0 && bh > 0 && double(bw) * double(bh) <= MARGIN_AREA_LIMIT) {
        minX -= MARGIN; minY -= MARGIN;
        maxX += MARGIN; maxY += MARGIN;
        bw += 2 * MARGIN; bh += 2 * MARGIN;
    }

    // Жёсткий предел буфера. Область подрезаем вокруг центра, сохраняя то,
    // что видно в середине экрана.
    if (double(bw) * double(bh) > MAX_CELLS) {
        const double k = std::sqrt(MAX_CELLS / (double(bw) * double(bh)));
        const int64_t nw = std::max<int64_t>(1, static_cast<int64_t>(double(bw) * k));
        const int64_t nh = std::max<int64_t>(1, static_cast<int64_t>(double(bh) * k));
        const int64_t ccx = minX + bw / 2;
        const int64_t ccy = minY + bh / 2;
        minX = ccx - nw / 2;
        minY = ccy - nh / 2;
        maxX = minX + nw - 1;
        maxY = minY + nh - 1;
        bw = nw;
        bh = nh;
    }

    m_minX = minX;
    m_minY = minY;
    m_w = static_cast<int>(bw);
    m_h = static_cast<int>(bh);

    // Ревизию читаем до построения: если мир изменится во время работы, кэш
    // будет опознан устаревшим на следующем кадре.
    const uint64_t rev = m_wm->fieldRevision();

    m_dist.resize(static_cast<size_t>(m_w) * static_cast<size_t>(m_h));
    std::fill(m_dist.begin(), m_dist.end(), uint8_t(255));

    // Нули — открытые клетки, по чанкам и только внутри области.
    const int64_t cxMin = m_minX >> 4;
    const int64_t cxMax = (m_minX + m_w - 1) >> 4;
    const int64_t cyMin = m_minY >> 4;
    const int64_t cyMax = (m_minY + m_h - 1) >> 4;
    for (int64_t cy = cyMin; cy <= cyMax; ++cy) {
        for (int64_t cx = cxMin; cx <= cxMax; ++cx) {
            const std::shared_ptr<Chunk> chunk = m_wm->getChunkUnsafe(cx, cy);
            if (!chunk) continue;   // незагруженный чанк остаётся в тумане

            const int64_t baseX = cx << 4;
            const int64_t baseY = cy << 4;
            const int ix0 = static_cast<int>(std::max<int64_t>(0, m_minX - baseX));
            const int iy0 = static_cast<int>(std::max<int64_t>(0, m_minY - baseY));
            const int ix1 = static_cast<int>(std::min<int64_t>(15, m_minX + m_w - 1 - baseX));
            const int iy1 = static_cast<int>(std::min<int64_t>(15, m_minY + m_h - 1 - baseY));
            for (int iy = iy0; iy <= iy1; ++iy) {
                const size_t row = static_cast<size_t>(baseY + iy - m_minY) * m_w;
                for (int ix = ix0; ix <= ix1; ++ix) {
                    if (isRevealed(chunk->cells[ix][iy]))
                        m_dist[row + static_cast<size_t>(baseX + ix - m_minX)] = 0;
                }
            }
        }
    }

    // Два прохода чамфера по 8 соседям (ход короля) — точное расстояние
    // Чебышёва за O(N).
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            const size_t i = static_cast<size_t>(y) * m_w + x;
            int best = m_dist[i];
            if (x > 0)                        best = std::min(best, step(m_dist[i - 1]));
            if (y > 0)                        best = std::min(best, step(m_dist[i - m_w]));
            if (x > 0 && y > 0)               best = std::min(best, step(m_dist[i - m_w - 1]));
            if (x + 1 < m_w && y > 0)         best = std::min(best, step(m_dist[i - m_w + 1]));
            m_dist[i] = static_cast<uint8_t>(best);
        }
    }
    for (int y = m_h - 1; y >= 0; --y) {
        for (int x = m_w - 1; x >= 0; --x) {
            const size_t i = static_cast<size_t>(y) * m_w + x;
            int best = m_dist[i];
            if (x + 1 < m_w)                  best = std::min(best, step(m_dist[i + 1]));
            if (y + 1 < m_h)                  best = std::min(best, step(m_dist[i + m_w]));
            if (x > 0 && y + 1 < m_h)         best = std::min(best, step(m_dist[i + m_w - 1]));
            if (x + 1 < m_w && y + 1 < m_h)   best = std::min(best, step(m_dist[i + m_w + 1]));
            m_dist[i] = static_cast<uint8_t>(best);
        }
    }

    m_revision = rev;
    m_valid = true;
}

int FogMap::stageAt(int64_t x, int64_t y) const {
    if (!m_valid) return TRANSITION_STAGES;
    if (x < m_minX || y < m_minY || x >= m_minX + m_w || y >= m_minY + m_h)
        return TRANSITION_STAGES;

    const uint8_t d = m_dist[static_cast<size_t>(y - m_minY) * m_w +
                             static_cast<size_t>(x - m_minX)];
    if (d <= CLEAR_RADIUS) return 0;
    return qMin(TRANSITION_STAGES, static_cast<int>(d) - CLEAR_RADIUS);
}

const QPixmap &FogMap::sprite(CellState state, int stage) const {
    static const QPixmap nullPixmap;
    const int s = static_cast<int>(state);
    if (s < 0 || s >= 16 || stage < 1 || stage >= TRANSITION_STAGES)
        return nullPixmap;
    return m_fogged[s][stage];
}
