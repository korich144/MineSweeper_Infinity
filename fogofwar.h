#ifndef FOGOFWAR_H
#define FOGOFWAR_H

#include <QColor>
#include <QVector>
#include <QPixmap>
#include <cstdint>
#include <vector>
#include "chunk.h"

class WorldManager;

// Туман войны — производное от поля состояние: расстояние Чебышёва до
// ближайшей уже открытой клетки. Нигде не хранится и не сохраняется.
class FogMap
{
public:
    static constexpr int CLEAR_RADIUS = 4;            // чистый квадрат 9x9
    static constexpr int TRANSITION_STAGES = 4;       // 4-я стадия — сплошной туман
    static constexpr int LAST_CLICKABLE_STAGE = 2;    // 3 и 4 стадии некликабельны

    void setWorldManager(WorldManager *wm);

    // Спрайты клеток в тумане строятся один раз при смене темы: смешивать
    // цвет на каждой отрисовке было бы заметно дороже.
    void setTheme(const QVector<QPixmap> &cellSprites, const QColor &fogColor);

    // Пересчитывает туман для запрошенной области (в мировых координатах клеток,
    // границы включительно), если мир изменился или камера ушла за пределы
    // уже посчитанного. Вызывается из paintEvent.
    void update(int64_t minX, int64_t minY, int64_t maxX, int64_t maxY);

    // Стадия клетки: 0..TRANSITION_STAGES. За пределами посчитанной области — TRANSITION_STAGES.
    int stageAt(int64_t x, int64_t y) const;

    // Спрайт клетки, приглушённый туманом. Валиден для stage 1..TRANSITION_STAGES-1
    // и любого из 16 состояний. Для stage 0 берите обычный спрайт темы, для
    // stage 4 спрайт не нужен — клетка заливается color().
    const QPixmap &sprite(CellState state, int stage) const;

    const QColor &color() const { return m_fogColor; }

private:
    // Запас вокруг запрошенной области, чтобы мелкие сдвиги камеры не вызывали
    // пересчёт. Даётся только областям разумного размера — иначе при сильном
    // отдалении буфер раздувался бы вместе с запасом.
    static constexpr int64_t MARGIN = 16;
    static constexpr double MARGIN_AREA_LIMIT = 40000.0;
    // Предел размера буфера расстояний: 4M клеток — это 4 МБ и уже далеко
    // за пределами любого реального вида.
    static constexpr double MAX_CELLS = 4000000.0;

    WorldManager *m_wm = nullptr;
    QColor m_fogColor;
    QPixmap m_fogged[16][TRANSITION_STAGES + 1];

    // Посчитанная область и расстояния Чебышёва до ближайшей открытой клетки.
    int64_t m_minX = 0, m_minY = 0;
    int m_w = 0, m_h = 0;
    std::vector<uint8_t> m_dist;
    uint64_t m_revision = 0;
    bool m_valid = false;

    void rebuild(int64_t minX, int64_t minY, int64_t maxX, int64_t maxY);
};
#endif // FOGOFWAR_H
