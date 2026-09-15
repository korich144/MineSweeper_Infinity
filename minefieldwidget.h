#ifndef MINEFIELDWIDGET_H
#define MINEFIELDWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QPointF>
#include <QElapsedTimer>
#include <optional>

class WorldManager;

class MinefieldWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MinefieldWidget(QWidget *parent = nullptr);

    void setWorldManager(WorldManager *wm) { m_wm = wm; }
    void setTheme(const QString &themeName);
    double scale() const { return m_scale; }
    void setScale(double scale);
    void setOffset(int64_t x, int64_t y);
    // Ставит точку (x,y) в мировых координатах в центр виджета (дробные значения — доли клетки).
    void centerOn(double x, double y);

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
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPointF cellToScreen(double cx, double cy) const;

    WorldManager *m_wm = nullptr;
    QVector<QPixmap> m_cellSprites;
    double m_scale = 1.0;
    QPointF m_offset;

    bool m_dragging = false;
    QPointF m_dragStartPos;
    QPointF m_dragStartOffset;
    QElapsedTimer m_clickTimer;
    QPointF m_clickPos;
    bool m_mightDrag = false;

    std::optional<QPair<int64_t, int64_t>> m_pressedCell;
};

#endif // MINEFIELDWIDGET_H