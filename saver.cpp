#include "saver.h"
#include "worldmanager.h"
#include "region.h"
#include <QDir>

Saver::Saver(WorldManager *wm, const QString &worldDir, QObject *parent)
    : QObject(parent), m_wm(wm), m_worldDir(worldDir)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Saver::onTimer);
    m_timer->start(60000); // every 60 seconds
}

void Saver::onTimer() {
    if (m_dirty) {
        saveNow();
        m_dirty = false;
    }
    // mark dirty if any regions are dirty? Actually we'll check at save time.
    // For periodic save, we can just check if there are any dirty regions.
    if (!m_wm->dirtyRegions().empty())
        m_dirty = true;
}

void Saver::saveNow() {
    auto dirtyRegions = m_wm->dirtyRegions();
    if (dirtyRegions.empty()) return;

    emit showSaveIndicator();

    for (const auto &reg : dirtyRegions) {
        int64_t rx = reg.first;
        int64_t ry = reg.second;
        auto chunks = m_wm->getChunksForRegion(rx, ry);
        if (!chunks.isEmpty()) {
            QString path = QString("%1/regions/r%2_r%3.dat").arg(m_worldDir).arg(rx).arg(ry);
            Region::saveRegion(path, rx, ry, chunks);
        }
    }
    m_wm->clearDirtyForRegions(dirtyRegions);
}