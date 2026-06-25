#include "minefieldwidget.h"
#include "worldmanager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

MinefieldWidget::MinefieldWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_offset = QPointF(0, 0);
    m_clickTimer.start();
}

void MinefieldWidget::setTheme(const QString &themeName) {
    QString path = QString(":/themes/%1.bmp").arg(themeName);
    QPixmap sheet(path);
    if (sheet.isNull()) {
        // fallback: try to load from file system
        sheet.load("sprites/" + themeName + ".bmp");
    }
    if (sheet.isNull()) {
        qWarning() << "Cannot load theme:" << themeName;
        return;
    }

    m_cellSprites.clear();
    for (int i = 0; i < 16; ++i) {
        m_cellSprites.append(sheet.copy(i * 16, 0, 16, 16));
    }
    // digit and smile sprites are handled by MainWindow
    update();
}

void MinefieldWidget::setOffset(int64_t x, int64_t y) {
    m_offset = QPointF(x, y);
    update();
}

void MinefieldWidget::centerOn(int64_t x, int64_t y) {
    QPointF center = rect().center();
    QPointF cellScreen = cellToScreen(x, y);
    m_offset += (center - cellScreen);
    update();
}

void MinefieldWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.fillRect(rect(), Qt::darkGray);

    if (!m_wm || m_cellSprites.size() < 16)
        return;

    QPointF topLeft = screenToCell(QPointF(0, 0));
    QPointF bottomRight = screenToCell(QPointF(width(), height()));
    int64_t minX = static_cast<int64_t>(qFloor(topLeft.x())) - 1;
    int64_t minY = static_cast<int64_t>(qFloor(topLeft.y())) - 1;
    int64_t maxX = static_cast<int64_t>(qCeil(bottomRight.x())) + 1;
    int64_t maxY = static_cast<int64_t>(qCeil(bottomRight.y())) + 1;

    int64_t chunkMinX = minX >> 4;
    int64_t chunkMinY = minY >> 4;
    int64_t chunkMaxX = maxX >> 4;
    int64_t chunkMaxY = maxY >> 4;

    for (int64_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
        for (int64_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
            auto chunk = m_wm->getChunk(cx, cy);
            if (!chunk) {
                m_wm->requestChunk(cx, cy); // async load
                // draw all as CLOSED
                for (int i = 0; i < 16; ++i) {
                    for (int j = 0; j < 16; ++j) {
                        int64_t cellX = (cx << 4) + i;
                        int64_t cellY = (cy << 4) + j;
                        if (cellX < minX || cellX > maxX || cellY < minY || cellY > maxY) continue;
                        QPointF sp = cellToScreen(cellX, cellY);
                        QRectF targetRect(sp, QSizeF(16 * m_scale, 16 * m_scale));
                        p.drawPixmap(targetRect, m_cellSprites[static_cast<int>(CellState::CLOSED)], QRectF(0,0,16,16));
                    }
                }
            } else {
                for (int i = 0; i < 16; ++i) {
                    for (int j = 0; j < 16; ++j) {
                        int64_t cellX = (cx << 4) + i;
                        int64_t cellY = (cy << 4) + j;
                        if (cellX < minX || cellX > maxX || cellY < minY || cellY > maxY) continue;
                        CellState st = chunk->cells[i][j];
                        QPointF sp = cellToScreen(cellX, cellY);
                        QRectF targetRect(sp, QSizeF(16 * m_scale, 16 * m_scale));
                        // visual feedback for pressed closed cell
                        if (m_pressedCell.x() == cellX && m_pressedCell.y() == cellY && st == CellState::CLOSED) {
                            // draw a bit darker (no dedicated sprite) by tinting
                            QPixmap tinted = m_cellSprites[static_cast<int>(CellState::CLOSED)];
                            QPainter pixp(&tinted);
                            pixp.fillRect(tinted.rect(), QColor(0, 0, 0, 64));
                            pixp.end();
                            p.drawPixmap(targetRect, tinted, QRectF(0,0,16,16));
                        } else {
                            p.drawPixmap(targetRect, m_cellSprites[static_cast<int>(st)], QRectF(0,0,16,16));
                        }
                    }
                }
            }
        }
    }
}

QPointF MinefieldWidget::cellToScreen(int64_t cx, int64_t cy) const {
    return QPointF(m_offset.x() + cx * 16 * m_scale,
                   m_offset.y() + cy * 16 * m_scale);
}

QPointF MinefieldWidget::screenToCell(const QPointF &pos) const {
    return QPointF((pos.x() - m_offset.x()) / (16 * m_scale),
                   (pos.y() - m_offset.y()) / (16 * m_scale));
}

void MinefieldWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_mightDrag = false;
        m_clickPos = event->pos();
        m_clickTimer.restart();
        QPointF cell = screenToCell(event->pos());
        int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
        int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
        // remember pressed cell for visual feedback
        m_pressedCell = QPoint(cx, cy);
        update();   // repaint to show pressed state
    } else if (event->button() == Qt::RightButton) {
        if (!m_wm || !m_wm->isGameActive()) return;
        QPointF cell = screenToCell(event->pos());
        int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
        int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
        CellState st = m_wm->cellStateAt(cx, cy);
        if (st == CellState::CLOSED)
            m_wm->setCellState(cx, cy, CellState::FLAGGED);
        else if (st == CellState::FLAGGED)
            m_wm->setCellState(cx, cy, CellState::QUESTION);
        else if (st == CellState::QUESTION)
            m_wm->setCellState(cx, cy, CellState::CLOSED);
        update();
    } else if (event->button() == Qt::MiddleButton) {
        m_dragging = true;
        m_dragStartPos = event->pos();
        m_dragStartOffset = m_offset;
        setCursor(Qt::ClosedHandCursor);
    }
}

void MinefieldWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // clear pressed cell feedback
        m_pressedCell = QPoint(-1, -1);
        update();

        if (m_mightDrag) return;
        if (m_clickTimer.elapsed() < 300 && (event->pos() - m_clickPos).manhattanLength() < 5) {
            if (!m_wm || !m_wm->isGameActive()) return;
            QPointF cell = screenToCell(event->pos());
            int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
            int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
            CellState st = m_wm->cellStateAt(cx, cy);
            if (st == CellState::CLOSED) {
                bool ok = m_wm->revealCell(cx, cy);
                if (ok) {
                    emit firstClick();
                    emit startReveal(cx, cy);
                }
                update();
            }
        }
    } else if (event->button() == Qt::MiddleButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void MinefieldWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        QPointF delta = event->pos() - m_dragStartPos;
        m_offset = m_dragStartOffset + delta;
        update();
    } else if (event->buttons() & Qt::LeftButton) {
        if (!m_mightDrag && (event->pos() - m_clickPos).manhattanLength() > 3)
            m_mightDrag = true;
    }
}

void MinefieldWidget::wheelEvent(QWheelEvent *event) {
    double factor = 1.0 + event->angleDelta().y() / 1200.0;
    double oldScale = m_scale;
    m_scale = qBound(0.1, m_scale * factor, 10.0);
    QPointF mousePos = event->position();
    QPointF worldBefore = screenToCell(mousePos);
    m_offset = mousePos - QPointF(worldBefore.x() * 16 * m_scale,
                                  worldBefore.y() * 16 * m_scale);
    update();
}