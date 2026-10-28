#include "minefieldwidget.h"
#include "worldmanager.h"
#include "theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <cstring>

MinefieldWidget::MinefieldWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // Отрисовка всегда закрашивает виджет целиком (цветом тумана или тайлами),
    // поэтому Qt не обязан под каждым кадром заливать фон своим цветом.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_offset = QPointF(0, 0);
    m_clickTimer.start();
    m_scrollClock.start();
}

void MinefieldWidget::setWorldManager(WorldManager *wm) {
    m_wm = wm;
    // Посчитанный туман и тайлы принадлежат прошлому миру.
    m_fog.setWorldManager(wm);
    m_tiles.clear();
    m_requested.clear();
    m_paintedRevision = 0;
    m_forceRepaint = true;
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
    ++m_themeGen;
    m_scaledForScale = -1.0;   // предмасштабированные спрайты устарели
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

bool MinefieldWidget::hasPendingVisuals() const {
    return m_forceRepaint || (m_wm && m_wm->fieldRevision() != m_paintedRevision);
}

void MinefieldWidget::shiftByCells(int64_t dx, int64_t dy) {
    const double cell = 16 * m_scale;
    m_offset += QPointF(dx * cell, dy * cell);
}

void MinefieldWidget::ensureScaledSprites() {
    if (!m_scaledSprites.isEmpty() && m_scaledForScale == m_scale) return;

    const int side = spriteSide();
    m_scaledSprites.clear();
    m_scaledSprites.reserve(16 * FogMap::TRANSITION_STAGES);
    for (int s = 0; s < 16; ++s) {
        for (int stage = 0; stage < FogMap::TRANSITION_STAGES; ++stage) {
            const QPixmap &src = (stage == 0)
                ? m_cellSprites[s]
                : m_fog.sprite(static_cast<CellState>(s), stage);
            // Без сглаживания — как и при обычной отрисовке; FastTransformation
            // даёт те же пиксели, что и масштабирование на лету.
            m_scaledSprites.append(src.isNull()
                ? QPixmap()
                : src.scaled(side, side, Qt::IgnoreAspectRatio, Qt::FastTransformation));
        }
    }
    m_scaledForScale = m_scale;
}

void MinefieldWidget::drawCell(QPainter &p, int64_t cellX, int64_t cellY, CellState st,
                               int64_t illumX, int64_t illumY, bool pressed) {
    const int stage = cellFogStage(cellX, cellY, illumX, illumY);
    if (stage >= FogMap::TRANSITION_STAGES) return;   // уже залито цветом тумана

    // Спрайты взяты на пиксель крупнее точного размера клетки: соседние клетки
    // чуть перекрываются, зато между ними не появляется щелей.
    const QPointF sp = cellToScreen(cellX, cellY);
    p.drawPixmap(sp, m_scaledSprites[static_cast<int>(st) * 4 + stage]);
    if (pressed) {
        const double cell = 16.0 * m_scale;
        p.fillRect(QRectF(sp, QSizeF(cell, cell)), QColor(0, 0, 0, 64));
    }
}

void MinefieldWidget::requestMissingChunk(int64_t chunkX, int64_t chunkY) {
    const ChunkCoord coord(chunkX, chunkY);
    if (m_requested.contains(coord)) return;
    m_requested.insert(coord);
    m_wm->requestChunk(chunkX, chunkY);
}

int MinefieldWidget::tileChunks() const {
    const double cell = 16.0 * m_scale;
    if (cell >= 8.0) return 1;
    if (cell >= 4.0) return 2;
    return MAX_TILE_CHUNKS;
}

int64_t MinefieldWidget::floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0)) --q;
    return q;
}

void MinefieldWidget::collectTileStamps(int64_t *stamps,
                                        int64_t tileX, int64_t tileY, int tileChunks) const {
    // Блок чанков тайла плюс кольцо соседей: открытая клетка в соседнем чанке
    // меняет туман у края тайла, значит тайл должен перестроиться.
    const int stride = TILE_STAMP_SIDE;
    for (int dy = -1; dy <= tileChunks; ++dy) {
        for (int dx = -1; dx <= tileChunks; ++dx) {
            const auto chunk = m_wm->getChunkUnsafe(tileX * tileChunks + dx,
                                                    tileY * tileChunks + dy);
            stamps[(dy + 1) * stride + (dx + 1)] = chunk ? chunk->useStamp : 0;
        }
    }
}

const QImage &MinefieldWidget::tilePixmap(int64_t tileX, int64_t tileY) {
    const int tc = tileChunks();
    const double cell = 16.0 * m_scale;
    const int cells = tc * 16;
    // На пиксель больше точного размера блока: крайние клетки перекрывают
    // границу тайла, а не обрезаются.
    const int side = qCeil(cells * cell) + 1;
    const int64_t tileBytes = int64_t(side) * side * 4;

    int64_t stamps[TILE_STAMP_SIDE * TILE_STAMP_SIDE];
    collectTileStamps(stamps, tileX, tileY, tc);

    const ChunkCoord coord(tileX, tileY);
    ChunkTile &tile = m_tiles[coord];
    tile.lastUse = ++m_tileUseCounter;

    bool fresh = !tile.pixmap.isNull() && tile.pixmap.width() == side
        && tile.scale == m_scale && tile.themeGen == m_themeGen && tile.tileChunks == tc;
    for (int i = 0; fresh && i < TILE_STAMP_SIDE * TILE_STAMP_SIDE; ++i)
        if (tile.stamps[i] != stamps[i]) { fresh = false; break; }
    if (fresh) return tile.pixmap;

    // Клетки 4-й стадии — сплошной цвет тумана, поэтому тайл целиком заливается
    // им, а такие клетки просто пропускаются. RGB32 — формат без альфы: для
    // растеризатора это гарантия непрозрачности, и блит тайла превращается
    // в простое копирование без попиксельного смешивания.
    if (tile.pixmap.width() != side)
        tile.pixmap = QImage(side, side, QImage::Format_RGB32);
    tile.pixmap.fill(m_fog.color());

    QPainter tp(&tile.pixmap);
    tp.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int scy = 0; scy < tc; ++scy) {
        for (int scx = 0; scx < tc; ++scx) {
            const auto chunk = m_wm->getChunkUnsafe(tileX * tc + scx, tileY * tc + scy);
            const int baseX = scx * 16;
            const int baseY = scy * 16;
            for (int i = 0; i < 16; ++i) {
                for (int j = 0; j < 16; ++j) {
                    const CellState st = chunk ? chunk->cells[i][j] : CellState::CLOSED;
                    const int64_t worldX = (tileX * tc + scx) * 16 + i;
                    const int64_t worldY = (tileY * tc + scy) * 16 + j;
                    const int stage = m_fog.stageAt(worldX, worldY);
                    if (stage >= FogMap::TRANSITION_STAGES) continue;
                    tp.drawPixmap(QPointF((baseX + i) * cell, (baseY + j) * cell),
                                  m_scaledSprites[static_cast<int>(st) * 4 + stage]);
                }
            }
        }
    }
    tp.end();

    tile.scale = m_scale;
    tile.themeGen = m_themeGen;
    tile.tileChunks = tc;
    memcpy(tile.stamps, stamps, sizeof(stamps));
    evictTilesIfNeeded(tileBytes);
    return tile.pixmap;
}

void MinefieldWidget::evictTilesIfNeeded(int64_t tileBytes) {
    int64_t maxTiles = TILE_BUDGET_BYTES / qMax<int64_t>(tileBytes, 1);
    maxTiles = qBound<int64_t>(int64_t(16), maxTiles, int64_t(8192));
    if (m_tiles.size() <= maxTiles) return;

    // Освобождаем сразу десятую часть, чтобы не сканировать кэш на каждом тайле.
    const int64_t target = maxTiles * 9 / 10;
    while (m_tiles.size() > target) {
        ChunkCoord oldest;
        int64_t oldestUse = 0;
        bool found = false;
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
            if (!found || it->lastUse < oldestUse) {
                oldestUse = it->lastUse;
                oldest = it.key();
                found = true;
            }
        }
        if (!found) break;
        m_tiles.remove(oldest);
    }
}

void MinefieldWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (!m_wm || m_cellSprites.size() < 16) {
        p.fillRect(rect(), Qt::darkGray);
        return;
    }

    ensureScaledSprites();

    QPointF topLeft = screenToCell(QPointF(0, 0));
    QPointF bottomRight = screenToCell(QPointF(width(), height()));
    int64_t minX = static_cast<int64_t>(qFloor(topLeft.x())) - 1;
    int64_t minY = static_cast<int64_t>(qFloor(topLeft.y())) - 1;
    int64_t maxX = static_cast<int64_t>(qCeil(bottomRight.x())) + 1;
    int64_t maxY = static_cast<int64_t>(qCeil(bottomRight.y())) + 1;

    const int64_t visMinCX = minX >> 4;
    const int64_t visMinCY = minY >> 4;
    const int64_t visMaxCX = maxX >> 4;
    const int64_t visMaxCY = maxY >> 4;

    // В тайловом пути заливка не нужна: тайлы непрозрачны и накрывают весь
    // виджет целиком. Она нужна прямому пути — там клетки 4-й стадии
    // пропускаются, и сквозь них должен быть виден цвет тумана.
    const bool tiled = 16.0 * m_scale <= MAX_TILED_CELL_PX;

    // Туману нужен запас: тайлы выступают за экран на размер блока чанков,
    // а стадия клетки зависит от открытых клеток в 8 клетках от неё — их
    // добавит сам FogMap.
    const int64_t pad = int64_t(tiled ? tileChunks() : 1) * 16;
    m_fog.update(minX - pad, minY - pad, maxX + pad, maxY + pad);

    const QPair<int64_t, int64_t> illum = illuminationCell();
    const int64_t illumX = illum.first;
    const int64_t illumY = illum.second;

    // Обходим не только видимые чанки, но и PRELOAD_CHUNKS в каждую сторону:
    // их заранее заказываем у загрузчика — один раз, а не каждый кадр.
    for (int64_t cy = visMinCY - PRELOAD_CHUNKS; cy <= visMaxCY + PRELOAD_CHUNKS; ++cy) {
        for (int64_t cx = visMinCX - PRELOAD_CHUNKS; cx <= visMaxCX + PRELOAD_CHUNKS; ++cx) {
            auto chunk = m_wm->getChunkUnsafe(cx, cy);
            if (chunk)
                m_requested.remove(ChunkCoord(cx, cy));
            else
                requestMissingChunk(cx, cy);
        }
    }

    if (tiled) {
        // Кадр — блиты готовых тайлов. Перерисовывается только тайл, у
        // которого изменился какой-то из его чанков или соседних с ними —
        // от них зависит туман на клетках тайла.
        //
        // Позиция округляется до целых: субпиксельный блит — это попиксельный
        // цикл, а сдвиг на полпикселя глаз не отличит. Тайл чуть шире своего
        // блока, поэтому стыки накрываются соседними тайлами: они рисуются
        // слева направо и сверху вниз, каждый следующий перекрывает хвост
        // предыдущего.
        //
        // Тайлы непрозрачны и рисуются первыми, поэтому они копируют себя на
        // приёмник (Source), а не смешиваются — это заметно быстрее. Режим
        // возвращается на SourceOver для подсветок поверх тайлов.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        const int tc = tileChunks();
        const int64_t tMinX = floorDiv(visMinCX, tc);
        const int64_t tMaxX = floorDiv(visMaxCX, tc);
        const int64_t tMinY = floorDiv(visMinCY, tc);
        const int64_t tMaxY = floorDiv(visMaxCY, tc);
        for (int64_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int64_t tx = tMinX; tx <= tMaxX; ++tx) {
                const QPointF spF = cellToScreen(tx * tc * 16.0, ty * tc * 16.0);
                const QPoint sp(qRound(spF.x()), qRound(spF.y()));
                const QImage &tile = tilePixmap(tx, ty);
                p.drawImage(sp, tile);
            }
        }
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        for (int64_t cy = visMinCY; cy <= visMaxCY; ++cy) {
            for (int64_t cx = visMinCX; cx <= visMaxCX; ++cx) {
                auto chunk = m_wm->getChunkUnsafe(cx, cy);
                for (int i = 0; i < 16; ++i) {
                    for (int j = 0; j < 16; ++j) {
                        const int64_t cellX = (cx << 4) + i;
                        const int64_t cellY = (cy << 4) + j;
                        if (cellX < minX || cellX > maxX || cellY < minY || cellY > maxY) continue;
                        drawCell(p, cellX, cellY, chunk ? chunk->cells[i][j] : CellState::CLOSED,
                                 illumX, illumY, false);
                    }
                }
            }
        }
    }

    // Подсветка центра не запекается в тайлы: она держится за центр экрана,
    // а не за мировые координаты. В прямом пути она уже учтена в drawCell.
    if (tiled)
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (tiled && m_illumination) {
        const int64_t reach = FogMap::CLEAR_RADIUS + FogMap::TRANSITION_STAGES;
        for (int64_t cy = illumY - reach; cy <= illumY + reach; ++cy) {
            for (int64_t cx = illumX - reach; cx <= illumX + reach; ++cx) {
                auto chunk = m_wm->getChunkUnsafe(cx >> 4, cy >> 4);
                const CellState st = chunk ? chunk->cells[cx & 15][cy & 15]
                                           : CellState::CLOSED;
                drawCell(p, cx, cy, st, illumX, illumY, false);
            }
        }
    }

    // Нажатая клетка подсвечивается поверх тайлов: затемнение одной клетки
    // дорисовывается каждый кадр, а не запекается в кэш.
    if (m_pressedCell) {
        auto chunk = m_wm->getChunkUnsafe(m_pressedCell->first >> 4,
                                          m_pressedCell->second >> 4);
        const CellState st = chunk
            ? chunk->cells[m_pressedCell->first & 15][m_pressedCell->second & 15]
            : CellState::CLOSED;
        drawCell(p, m_pressedCell->first, m_pressedCell->second, st,
                 illumX, illumY, true);
    }

    m_paintedRevision = m_wm->fieldRevision();
    m_forceRepaint = false;
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
        // Поле едет за пальцами, как на карте, а не против них, как в списке:
        // обе оси инвертированы относительно прокрутки списка. Инерция после
        // отпускания пальцев идёт теми же событиями и попадает сюда же.
        m_offset += event->pixelDelta().isNull()
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
