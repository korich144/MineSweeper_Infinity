#include "scoring.h"
#include "worldmanager.h"
#include <QtMath>

namespace {

int clampLevel(int level) {
    return WorldManager::clampDifficulty(level);
}

} // namespace

double Scoring::difficultyMultiplier(int difficultyLevel) {
    const double multiplier = SCORE_PER_LEVEL * clampLevel(difficultyLevel);
    return multiplier > 0.0 ? multiplier : 0.0;
}

qint64 Scoring::revealScore(int cellsOpened, int difficultyLevel) {
    if (cellsOpened <= 0) return 0;

    qint64 base = static_cast<qint64>(cellsOpened) * POINTS_PER_CELL;

    // Крупные разлёты награждаются дополнительно: на плотном поле
    // они чаще заканчиваются миной.
    qint64 areaBonus = 0;
    if (cellsOpened >= AREA_BONUS_MIN_CELLS)
        areaBonus = static_cast<qint64>(cellsOpened - AREA_BONUS_MIN_CELLS + 1) * AREA_BONUS_PER_CELL;

    return qRound64(static_cast<double>(base + areaBonus) * difficultyMultiplier(difficultyLevel));
}

qint64 Scoring::minePenalty(int difficultyLevel) {
    // Множитель нулевой на уровнях <= 0, поэтому штраф тоже нулевой —
    // подрыв никогда не приносит очков.
    return qRound64(MINE_PENALTY_BASE * difficultyMultiplier(difficultyLevel));
}

QString Scoring::format(qint64 score) {
    if (score < 0) score = 0;
    if (score < 1000) return QString("%1").arg(score, 3, 10, QChar(' '));
    return QString::number(score);
}
