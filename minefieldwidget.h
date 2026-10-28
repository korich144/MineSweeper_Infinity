#ifndef MINEFIELDWIDGET_H
#define MINEFIELDWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QPointF>
#include <QElapsedTimer>
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

    WorldManager *m_wm = nullptr;
    QVector<QPixmap> m_cellSprites;
    FogMap m_fog;
    double m_scale = 1.0;
    QPointF m_offset;

    bool m_illumination = false;

    bool m_dragging = false;
    QPointF m_dragStartPos;
    QPointF m_dragStartOffset;
    QElapsedTimer m_clickTimer;
    QPointF m_clickPos;
    bool m_mightDrag = false;

    std::optional<QPair<int64_t, int64_t>> m_pressedCell;
};

#endif // MINEFIELDWIDGET_H
