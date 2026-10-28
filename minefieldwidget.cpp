#include "minefieldwidget.h"
#include "worldmanager.h"
#include "theme.h"
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
    m_scrollClock.start();
}

void MinefieldWidget::setWorldManager(WorldManager *wm) {
    m_wm = wm;
    // Посчитанный туман принадлежит прошлому миру и его ревизии.
    m_fog.setWorldManager(wm);
}

void MinefieldWidget::setTheme(const QString &themeName) {
    QString path = QString(":/themes/%1.bmp").arg(themeName);
    QPixmap sheet(path);
    if (sheet.isNull()) {
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

    // Клетки в тумане смешиваются с его цветом один раз на тему, а не на кадр.
    m_fog.setTheme(m_cellSprites, ThemePalette::fromSheet(sheet).fog);
    update();
}

void MinefieldWidget::setScale(double scale) {
    m_scale = qBound(0.1, scale, 10.0);
    update();
}

void MinefieldWidget::setOffset(int64_t x, int64_t y) {
    m_offset = QPointF(x, y);
    update();
}

void MinefieldWidget::setStartIllumination(bool on) {
    if (m_illumination == on) return;
    m_illumination = on;
    update();
}

void MinefieldWidget::centerOn(double x, double y) {
    QPointF center = rect().center();
    QPointF cellScreen = cellToScreen(x, y);
    m_offset += (center - cellScreen);
    update();
}

void MinefieldWidget::shiftByCells(int64_t dx, int64_t dy) {
    const double cell = 16 * m_scale;
    m_offset += QPointF(dx * cell, dy * cell);
}

void MinefieldWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.fillRect(rect(), Qt::darkGray);

    if (!m_wm || m_cellSprites.size() < 16) return;

    QPointF topLeft = screenToCell(QPointF(0, 0));
    QPointF bottomRight = screenToCell(QPointF(width(), height()));
    int64_t minX = static_cast<int64_t>(qFloor(topLeft.x())) - 1;
    int64_t minY = static_cast<int64_t>(qFloor(topLeft.y())) - 1;
    int64_t maxX = static_cast<int64_t>(qCeil(bottomRight.x())) + 1;
    int64_t maxY = static_cast<int64_t>(qCeil(bottomRight.y())) + 1;

    m_fog.update(minX, minY, maxX, maxY);

    // Клетки 4-й стадии — сплошной цвет тумана, поэтому всё поле заливается
    // им сразу, а сами клетки в цикле пропускаются.
    p.fillRect(rect(), m_fog.color());

    const QPair<int64_t, int64_t> illum = illuminationCell();
    const int64_t illumX = illum.first;
    const int64_t illumY = illum.second;

    auto drawCell = [&](int64_t cellX, int64_t cellY, CellState st) {
        const int stage = cellFogStage(cellX, cellY, illumX, illumY);
        if (stage >= FogMap::TRANSITION_STAGES) return;   // уже залито туманом

        QPointF sp = cellToScreen(cellX, cellY);
        QRectF targetRect(sp, QSizeF(16 * m_scale, 16 * m_scale));
        const QPixmap &sprite = (stage == 0)
            ? m_cellSprites[static_cast<int>(st)]
            : m_fog.sprite(st, stage);

        bool pressed = m_pressedCell.has_value() &&
                       m_pressedCell->first == cellX && m_pressedCell->second == cellY;
        if (pressed && st == CellState::CLOSED) {
            QPixmap tinted = sprite;
            QPainter pixp(&tinted);
            pixp.fillRect(tinted.rect(), QColor(0, 0, 0, 64));
            pixp.end();
            p.drawPixmap(targetRect, tinted, QRectF(0,0,16,16));
        } else {
            p.drawPixmap(targetRect, sprite, QRectF(0,0,16,16));
        }
    };

    const int64_t visMinCX = minX >> 4;
    const int64_t visMinCY = minY >> 4;
    const int64_t visMaxCX = maxX >> 4;
    const int64_t visMaxCY = maxY >> 4;

    // Обходим не только видимые чанки, но и PRELOAD_CHUNKS в каждую сторону:
    // их заранее заказываем у загрузчика. Рисуем при этом только видимые.
    for (int64_t cy = visMinCY - PRELOAD_CHUNKS; cy <= visMaxCY + PRELOAD_CHUNKS; ++cy) {
        for (int64_t cx = visMinCX - PRELOAD_CHUNKS; cx <= visMaxCX + PRELOAD_CHUNKS; ++cx) {
            const bool onScreen = (cx >= visMinCX && cx <= visMaxCX &&
                                   cy >= visMinCY && cy <= visMaxCY);

            auto chunk = m_wm->getChunkUnsafe(cx, cy);
            if (!chunk) {
                m_wm->requestChunk(cx, cy);
                if (!onScreen) continue;

                for (int i = 0; i < 16; ++i) {
                    for (int j = 0; j < 16; ++j) {
                        int64_t cellX = (cx << 4) + i;
                        int64_t cellY = (cy << 4) + j;
                        if (cellX < minX || cellX > maxX || cellY < minY || cellY > maxY) continue;
                        drawCell(cellX, cellY, CellState::CLOSED);
                    }
                }
            } else {
                if (!onScreen) continue;

                for (int i = 0; i < 16; ++i) {
                    for (int j = 0; j < 16; ++j) {
                        int64_t cellX = (cx << 4) + i;
                        int64_t cellY = (cy << 4) + j;
                        if (cellX < minX || cellX > maxX || cellY < minY || cellY > maxY) continue;
                        drawCell(cellX, cellY, chunk->cells[i][j]);
                    }
                }
            }
        }
    }
}

QPointF MinefieldWidget::cellToScreen(double cx, double cy) const {
    return QPointF(m_offset.x() + cx * 16 * m_scale,
                   m_offset.y() + cy * 16 * m_scale);
}

QPointF MinefieldWidget::screenToCell(const QPointF &pos) const {
    return QPointF((pos.x() - m_offset.x()) / (16 * m_scale),
                   (pos.y() - m_offset.y()) / (16 * m_scale));
}

QPair<int64_t, int64_t> MinefieldWidget::illuminationCell() const {
    const QPointF center = screenToCell(QPointF(rect().center()));
    return QPair<int64_t, int64_t>(static_cast<int64_t>(qFloor(center.x())),
                                   static_cast<int64_t>(qFloor(center.y())));
}

int MinefieldWidget::cellFogStage(int64_t x, int64_t y, int64_t illumX, int64_t illumY) const {
    const int stage = m_fog.stageAt(x, y);
    if (stage == 0 || !m_illumination) return stage;

    // Подсветка центра — та же открытая клетка, только не в мире, а на экране.
    const int64_t d = qMax(qAbs(x - illumX), qAbs(y - illumY));
    if (d > FogMap::CLEAR_RADIUS + FogMap::TRANSITION_STAGES) return stage;
    const int lit = static_cast<int>(d) <= FogMap::CLEAR_RADIUS
        ? 0
        : static_cast<int>(d) - FogMap::CLEAR_RADIUS;
    return qMin(stage, lit);
}

bool MinefieldWidget::isCellClickable(int64_t x, int64_t y) const {
    const QPair<int64_t, int64_t> illum = illuminationCell();
    return cellFogStage(x, y, illum.first, illum.second) <= FogMap::LAST_CLICKABLE_STAGE;
}

void MinefieldWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_mightDrag = false;
        m_clickPos = event->pos();
        m_clickTimer.restart();
        QPointF cell = screenToCell(event->pos());
        int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
        int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
        if (isCellClickable(cx, cy))
            m_pressedCell = QPair<int64_t, int64_t>(cx, cy);
        else
            m_pressedCell = std::nullopt;
        update();
    } else if (event->button() == Qt::RightButton) {
        if (!m_wm || !m_wm->isGameActive()) return;
        QPointF cell = screenToCell(event->pos());
        int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
        int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
        if (!isCellClickable(cx, cy)) return;
        CellState st = m_wm->cellStateAt(cx, cy);
        if (st == CellState::CLOSED) {
            m_wm->setCellState(cx, cy, CellState::FLAGGED);
            emit flagToggled(1);
        } else if (st == CellState::FLAGGED) {
            m_wm->setCellState(cx, cy, CellState::QUESTION);
            emit flagToggled(-1);
        } else if (st == CellState::QUESTION) {
            m_wm->setCellState(cx, cy, CellState::CLOSED);
        }
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
        m_pressedCell = std::nullopt;
        update();

        if (m_mightDrag) return;
        if (m_clickTimer.elapsed() < 300 && (event->pos() - m_clickPos).manhattanLength() < 5) {
            if (!m_wm || !m_wm->isGameActive()) return;
            QPointF cell = screenToCell(event->pos());
            int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
            int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
            if (!isCellClickable(cx, cy)) return;
            CellState st = m_wm->cellStateAt(cx, cy);
            if (st == CellState::CLOSED) {
                int64_t rx = cx;
                int64_t ry = cy;
                // Первый клик: под курсор подводится заведомо безопасная клетка,
                // чтобы игра всегда начиналась с открытой области. Сдвиг идёт
                // на целое число клеток, поэтому подмена поля незаметна.
                if (m_illumination) {
                    int64_t sx = cx;
                    int64_t sy = cy;
                    if (m_wm->findSafeStartCell(cx, cy, sx, sy)) {
                        shiftByCells(cx - sx, cy - sy);
                        rx = sx;
                        ry = sy;
                    }
                }

                bool ok = m_wm->revealCell(rx, ry);
                if (ok) {
                    emit firstClick();
                    emit startReveal(rx, ry);
                }
                update();
            }
        }
    } else if (event->button() == Qt::MiddleButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

// «Помощник»: двойной клик по открытой цифре.
//   * флажков вокруг ровно по цифре — открываем оставшихся закрытых соседей
//     (если среди них окажется мина, флажок стоял неверно, игра заканчивается);
//   * флажков вместе с закрытыми соседями ровно по цифре — значит закрытые
//     соседи и есть мины, помечаем их флажками.
void MinefieldWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    m_pressedCell = std::nullopt;
    update();

    if (!m_wm || !m_wm->isGameActive()) return;

    const QPointF cell = screenToCell(event->pos());
    const int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
    const int64_t cy = static_cast<int64_t>(qFloor(cell.y()));

    std::vector<QPair<int64_t, int64_t>> targets;
    switch (m_wm->chordAction(cx, cy, targets)) {
    case WorldManager::ChordAction::Reveal:
        for (const auto &target : targets) {
            if (!m_wm->isGameActive()) break;
            if (!m_wm->revealCell(target.first, target.second)) break;   // попали на мину
            emit firstClick();
            emit startReveal(target.first, target.second);
        }
        break;

    case WorldManager::ChordAction::Flag: {
        int placed = 0;
        for (const auto &target : targets) {
            if (m_wm->cellStateAt(target.first, target.second) != CellState::CLOSED)
                continue;
            m_wm->setCellState(target.first, target.second, CellState::FLAGGED);
            ++placed;
        }
        if (placed > 0)
            emit flagToggled(placed);
        break;
    }

    case WorldManager::ChordAction::None:
        break;
    }
    update();
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
    QPointF cell = screenToCell(event->pos());
    int64_t cx = static_cast<int64_t>(qFloor(cell.x()));
    int64_t cy = static_cast<int64_t>(qFloor(cell.y()));
    emit mouseCellChanged(cx, cy);
}

void MinefieldWidget::wheelEvent(QWheelEvent *event) {
    // На Windows Qt не отличает тачпад от колеса мыши: плагин платформы не
    // умеет DirectManipulation, и прокрутка тачпада приходит теми же
    // сообщениями, что и колесо, — без пиксельной дельты и без фаз жеста.
    // Другие платформы отдают жест честно, а на Windows остаётся один
    // признак — дробность дельты: колесо мыши всегда кратно 120 (зубец),
    // тачпад шлёт поток мелких значений. Кратный 120 шаг внутри жеста
    // переживается открытым окном, а колесо мыши его не открывает.
    const QPoint angle = event->angleDelta();
    const qint64 now = m_scrollClock.elapsed();
    if (angle.x() % 120 != 0 || angle.y() % 120 != 0)
        m_highResScrollUntil = now + HIGH_RES_SCROLL_WINDOW_MS;

    const bool touchpadScroll = !event->pixelDelta().isNull()
                             || event->phase() != Qt::NoScrollPhase
                             || now < m_highResScrollUntil;

    // Ctrl оставлен за масштабом: им же приходят и щипки тачпада.
    if (touchpadScroll && !(event->modifiers() & Qt::ControlModifier)) {
        // Как в обычном списке: прокрутка вниз показывает то, что ниже, то есть
        // поле уезжает вверх. Естественная прокрутка («палец — за контентом»)
        // учитывается системой сама. Инерция после отпускания пальцев идёт
        // теми же событиями и попадает сюда же.
        m_offset -= event->pixelDelta().isNull()
            ? QPointF(angle)               // Windows: единица дельты ≈ пиксель
            : QPointF(event->pixelDelta());
        update();
        return;
    }

    QPointF mousePos = event->position();
    QPointF worldBefore = screenToCell(mousePos);   // старый масштаб!
    double factor = 1.0 + event->angleDelta().y() / 1200.0;
    m_scale = qBound(0.1, m_scale * factor, 10.0);
    m_offset = mousePos - QPointF(worldBefore.x() * 16 * m_scale,
                                  worldBefore.y() * 16 * m_scale);
    update();
}
