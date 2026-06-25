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
    if (!m_wm->dirtyRegions().empty())
        saveNow();
}

void Saver::saveNow() {
    auto dirtyRegions = m_wm->dirtyRegions();
    if (dirtyRegions.empty()) {
        emit saveFinished();
        return;
    }

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
    emit saveFinished();
}