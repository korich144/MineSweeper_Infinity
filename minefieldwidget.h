#ifndef MINEFIELDWIDGET_H
#define MINEFIELDWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QPointF>
#include <QElapsedTimer>

class WorldManager;

class MinefieldWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MinefieldWidget(QWidget *parent = nullptr);

    void setWorldManager(WorldManager *wm) { m_wm = wm; }
    void setTheme(const QString &themeName);
    double scale() const { return m_scale; }
    void setOffset(int64_t x, int64_t y);
    void centerOn(int64_t x, int64_t y);
    QPointF screenToCell(const QPointF &pos) const;

signals:
    void startReveal(int64_t x, int64_t y);   // sent to Solver
    void firstClick();                        // to start game timer

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPointF cellToScreen(int64_t cx, int64_t cy) const;
    void requestVisibleChunks();

    WorldManager *m_wm = nullptr;
    QVector<QPixmap> m_cellSprites;   // index corresponds to CellState
    double m_scale = 1.0;
    QPointF m_offset;                 // screen coords of world cell (0,0)

    // drag/click state
    bool m_dragging = false;
    QPointF m_dragStartPos;
    QPointF m_dragStartOffset;
    QElapsedTimer m_clickTimer;
    QPointF m_clickPos;
    bool m_mightDrag = false;
};

#endif // MINEFIELDWIDGET_H