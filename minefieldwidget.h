#ifndef MINEFIELDWIDGET_H
#define MINEFIELDWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QtMath>
#include <optional>
#include "fogofwar.h"

class WorldManager;

class MinefieldWidget : public QWidget
{
    Q_OBJECT
public:
    // Сколько клеток по стороне должно помещаться в поле при первом запуске.
    static constexpr int DEFAULT_FIELD_CELLS = 9;

    explicit MinefieldWidget(QWidget *parent = nullptr);

    void setWorldManager(WorldManager *wm);
    void setTheme(const QString &themeName);
    double scale() const { return m_scale; }
    void setScale(double scale);
    void setOffset(int64_t x, int64_t y);
    // Ставит точку (x,y) в мировых координатах в центр виджета (дробные значения — доли клетки).
    void centerOn(double x, double y);

    // Подсветка центра экрана: пока в мире нет открытых клеток, одна клетка
    // в центре разгоняет туман так же, как открытая — иначе кликнуть было бы
    // некуда. Гаснет по первому клику игрока. Заодно это признак того, что
    // игра ещё не начата: по нему первый клик подводит под курсор безопасную
    // клетку (см. mouseReleaseEvent).
    void setStartIllumination(bool on);
    bool startIllumination() const { return m_illumination; }

    // Есть ли что рисовать: мир изменился с последнего нарисованного кадра.
    // Таймер интерфейса по этому признаку решает, звать ли update(), — иначе
    // поле перерисовывалось 30 раз в секунду даже в полном простое. Сдвиг,
    // зум, смена темы и нажатие клетки зовут update() сами.
    bool hasPendingVisuals() const;

    QPointF screenToCell(const QPointF &pos) const;

signals:
    void startReveal(int64_t x, int64_t y);
    void firstClick();
    void flagToggled(int delta);
    void mouseCellChanged(int64_t x, int64_t y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Сколько чанков за пределами экрана просим у загрузчика заранее,
    // чтобы при прокрутке не было пустых полос.
    static constexpr int PRELOAD_CHUNKS = 3;

    // Сколько миллисекунд после «дробного» шага прокрутка считается тачпадной:
    // переживает случайный кратный 120 шаг внутри жеста, но колесо мыши,
    // шлющее только кратные значения, это окно не открывает.
    static constexpr qint64 HIGH_RES_SCROLL_WINDOW_MS = 1000;

    // Тайлы строятся, только пока клетка не крупнее этого размера (в пикселях):
    // при сильном приближении клеток на экране мало и прямой путь дешевле кэша.
    static constexpr double MAX_TILED_CELL_PX = 32.0;
    // Бюджет памяти под тайлы. При мелком масштабе тайлы крошечные, поэтому
    // ограничение по байтам само разрешает их тысячи.
    static constexpr int64_t TILE_BUDGET_BYTES = 64 * 1024 * 1024;
    // Максимум чанков по стороне тайла: при мелком масштабе тайл 16x16 клеток
    // вырождается в пару десятков пикселей, и накладные расходы на каждый
    // перевешивают сам блит — тогда чанки сливаются в крупные тайлы.
    static constexpr int MAX_TILE_CHUNKS = 4;
    // Меток на тайл: блок чанков тайла плюс кольцо соседей вокруг него
    // (туман зависит от открытых клеток в 8 клетках — это соседний чанк).
    static constexpr int TILE_STAMP_SIDE = MAX_TILE_CHUNKS + 2;

    // Готовая картинка блока чанков и признаки, по которым она ещё
    // соответствует содержимому. Метки берутся у всех чанков блока и кольца
    // соседей вокруг него. Подсветка центра и нажатая клетка в тайл не
    // запекаются — они экранное, а не мировое. Картинка хранится QImage'ом
    // в формате RGB32: без альфы растеризатор копирует её, а не смешивает.
    struct ChunkTile {
        QImage pixmap;
        int64_t stamps[TILE_STAMP_SIDE * TILE_STAMP_SIDE] = {};
        int tileChunks = 0;
        double scale = 0.0;
        int themeGen = -1;
        int64_t lastUse = 0;
    };

    QPointF cellToScreen(double cx, double cy) const;

    // Клетка, которая сейчас подсвечивает центр экрана.
    QPair<int64_t, int64_t> illuminationCell() const;
    // Стадия тумана клетки с учётом подсветки. Позиция подсветки передаётся
    // снаружи: в цикле отрисовки её дешевле посчитать один раз на кадр.
    int cellFogStage(int64_t x, int64_t y, int64_t illumX, int64_t illumY) const;
    // Клетки 3 и 4 стадии закрыты туманом — по ним нельзя ни открыть клетку,
    // ни поставить флажок.
    bool isCellClickable(int64_t x, int64_t y) const;
    // Сдвигает поле на целое число клеток: все закрытые клетки выглядят
    // одинаково, поэтому подмена незаметна (кроме координат в строке состояния).
    void shiftByCells(int64_t dx, int64_t dy);

    // Спрайты, уже отмасштабированные под текущий масштаб: [состояние*4 + стадия].
    // Стадия 4 — сплошной цвет тумана, спрайт не нужен.
    void ensureScaledSprites();
    int spriteSide() const { return qCeil(16.0 * m_scale); }

    void drawCell(QPainter &p, int64_t cellX, int64_t cellY, CellState st,
                  int64_t illumX, int64_t illumY, bool pressed);
    // Просит чанк у загрузчика один раз: повторный запрос каждого кадра — это
    // десятки queued-событий в поток загрузчика в секунду.
    void requestMissingChunk(int64_t chunkX, int64_t chunkY);
    // Сторона тайла в чанках: растёт на мелких масштабах.
    int tileChunks() const;
    // Деление с округлением вниз: координаты чанков бывают отрицательными.
    static int64_t floorDiv(int64_t a, int64_t b);
    // Возвращает тайл блока чанков, при необходимости рисуя его заново.
    const QImage &tilePixmap(int64_t tileX, int64_t tileY);
    void collectTileStamps(int64_t *stamps, int64_t tileX, int64_t tileY, int tileChunks) const;
    void evictTilesIfNeeded(int64_t tileBytes);

    WorldManager *m_wm = nullptr;
    QVector<QPixmap> m_cellSprites;
    FogMap m_fog;
    double m_scale = 1.0;
    QPointF m_offset;

    QVector<QPixmap> m_scaledSprites;
    double m_scaledForScale = -1.0;
    int m_themeGen = 0;

    QHash<ChunkCoord, ChunkTile> m_tiles;
    int64_t m_tileUseCounter = 0;
    QSet<ChunkCoord> m_requested;

    uint64_t m_paintedRevision = 0;
    bool m_forceRepaint = false;
    bool m_illumination = false;

    bool m_dragging = false;
    QPointF m_dragStartPos;
    QPointF m_dragStartOffset;
    QElapsedTimer m_clickTimer;
    QElapsedTimer m_scrollClock;       // окно «тачпадного» колеса прокрутки
    QPointF m_clickPos;
    bool m_mightDrag = false;

    // До какого момента (по m_scrollClock) прокрутка считается тачпадной.
    // Windows не отличает тачпад от колеса мыши, кроме как по дробности дельты.
    qint64 m_highResScrollUntil = 0;

    std::optional<QPair<int64_t, int64_t>> m_pressedCell;
};

#endif // MINEFIELDWIDGET_H
