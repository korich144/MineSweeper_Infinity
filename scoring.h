#ifndef SCORING_H
#define SCORING_H

#include <QString>
#include <QtGlobal>

// Формула очков вынесена в одно место, чтобы её можно было править
// без изменений в UI и в движке.
namespace Scoring {

constexpr qint64 POINTS_PER_CELL     = 10;  // за каждую открытую клетку
constexpr int    AREA_BONUS_MIN_CELLS = 5;  // с какого разлёта даётся бонус
constexpr qint64 AREA_BONUS_PER_CELL  = 4;  // добавка за клетку сверх порога
constexpr qint64 MINE_PENALTY_BASE    = 50; // штраф за подрыв на мину

// Награда пропорциональна сложности: на нулевом уровне очки не начисляются,
// на отрицательном (более разреженном поле) — тоже, чтобы счёт не уходил в минус.
constexpr double SCORE_PER_LEVEL = 0.5;

// Множитель за сложность: 0 на уровне 0, 0.5 на уровне 1, 2.5 на уровне 5.
double difficultyMultiplier(int difficultyLevel);

// Очки за одно открытие: cellsOpened клеток на уровне difficultyLevel.
qint64 revealScore(int cellsOpened, int difficultyLevel);

// Штраф за открытую мину (никогда не бывает наградой).
qint64 minePenalty(int difficultyLevel);

// Строка для счётчика: младшие значения добиваются пробелами до 3 знаков.
QString format(qint64 score);

} // namespace Scoring

#endif // SCORING_H
