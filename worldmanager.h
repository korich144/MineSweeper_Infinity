#ifndef WORLDMANAGER_H
#define WORLDMANAGER_H

#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QList>
#include <memory>
#include <set>
#include <vector>
#include "chunk.h"

// Одно изменение клетки в пачке (см. applyRevealBatch).
struct RevealUpdate {
    int64_t x = 0;
    int64_t y = 0;
    CellState state = CellState::CLOSED;
};

// Окно 3x3 чанков вокруг заданного. Решатель держит его, чтобы читать клетки
// и соседей без блокировки на каждую клетку: блокировка берётся один раз
// при переходе в новый чанк.
struct ChunkWindow {
    int64_t chunkX = 0;
    int64_t chunkY = 0;
    bool ready = false;          // центральный чанк загружен
    std::shared_ptr<Chunk> center;
    std::shared_ptr<Chunk> ring[3][3];   // ring[dy+1][dx+1], центр не используется

    // Сосед ли это чанк окна (для клеток на краю чанка), иначе — центральный.
    std::shared_ptr<Chunk> chunkAt(int64_t chunkX_, int64_t chunkY_) const {
        if (chunkX_ == chunkX && chunkY_ == chunkY) return center;
        const int64_t dx = chunkX_ - chunkX;
        const int64_t dy = chunkY_ - chunkY;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return nullptr;
        return ring[dy + 1][dx + 1];
    }
};

class WorldManager : public QObject
{
    Q_OBJECT
public:
    // Сложность — это уровень, где 0 — базовая (около 9.8% мин), выше — плотнее,
    // ниже нуля — разреженнее. Экспоненциальная шкала между MIN и MAX.
    static constexpr int MIN_DIFFICULTY = -4;
    static constexpr int MAX_DIFFICULTY = 5;
    static constexpr int DEFAULT_DIFFICULTY = 2;   // ≈ плотность старого значения 1/6

    // Версия схемы сложности:
    //   0 — старое значение было знаменателем плотности (1/N мин);
    //   1 — уровни 1..10 (плотность от 4% до 30%);
    //   2 — текущая шкала: уровень 0 = бывший уровень 5, то есть сдвиг на -5.
    static constexpr int DIFFICULTY_SCHEME_VERSION = 2;
    static constexpr int LEVEL_SCHEME_OFFSET = 5;

    static int clampDifficulty(int difficulty) { return qBound(MIN_DIFFICULTY, difficulty, MAX_DIFFICULTY); }
    // Перевод старого уровня 1..10 в текущую шкалу.
    static int fromLevelScheme(int level) { return level - LEVEL_SCHEME_OFFSET; }
    // Вероятность мины для уровня. Экспоненциальный рост между MIN и MAX,
    // считается один раз и дальше используется как порог в isMine().
    static double mineProbabilityFor(int difficulty);
    // Обратное преобразование: ближайший уровень для заданной плотности мин.
    static int difficultyForProbability(double probability);

    explicit WorldManager(const QString &seed, int difficulty, QObject *parent = nullptr);

    void setSeed(const QString &seed);
    QString seed() const { return m_seed; }
    int difficulty() const { return m_difficulty; }
    double mineProbability() const { return m_mineProbability; }

    bool isMine(int64_t x, int64_t y) const;
    CellState cellStateAt(int64_t x, int64_t y) const;

    void setCellState(int64_t x, int64_t y, CellState state);
    void setCellStateIfLoaded(int64_t x, int64_t y, CellState state);
    void setMine(int64_t x, int64_t y);

    // Записывает пачку изменений под одной блокировкой. Порядок важен:
    // если для клетки есть несколько записей, побеждает последняя.
    // Виртуальный: решатель вызывает его единственным для записи ходов игры.
    virtual void applyRevealBatch(const std::vector<RevealUpdate> &updates);

    // Заполняет окно 3x3 чанков под одной блокировкой чтения.
    void loadWindow(int64_t chunkX, int64_t chunkY, ChunkWindow &window) const;

    // Отмечает чанк изменённым (блокировка берётся внутри). Решатель вызывает
    // это один раз на чанк, а не на каждую клетку.
    void markChunkDirty(const std::shared_ptr<Chunk> &chunk);

    bool revealCell(int64_t x, int64_t y);
    void requestChunk(int64_t chunkX, int64_t chunkY);

    // «Помощник» по двойному клику на открытую клетку.
    enum class ChordAction {
        None,     // условие не выполнено — ничего не делаем
        Reveal,   // флажков ровно по цифре — открыть оставшихся закрытых соседей
        Flag      // флажки плюс закрытые соседи дают цифру — поставить на них флажки
    };

    // Определяет, что делать по двойному клику, и заполняет targets:
    // для Reveal — закрытые соседи, для Flag — те, что надо пометить.
    // Флажки и клетки с вопросом в targets не попадают, сама клетка тоже.
    ChordAction chordAction(int64_t x, int64_t y,
                            std::vector<QPair<int64_t, int64_t>> &targets) const;

    std::shared_ptr<Chunk> getChunk(int64_t chunkX, int64_t chunkY);
    void addLoadedChunk(std::shared_ptr<Chunk> chunk);
    void fixupChunkCells(std::shared_ptr<Chunk> chunk);

    CellState cellStateAtUnsafe(int64_t x, int64_t y) const;
    std::shared_ptr<Chunk> getChunkUnsafe(int64_t chunkX, int64_t chunkY) const;

    std::set<QPair<int64_t,int64_t>> dirtyRegions() const;
    QHash<ChunkCoord, std::shared_ptr<Chunk>> getChunksForRegion(int64_t regX, int64_t regY) const;
    void clearDirtyForRegions(const std::set<QPair<int64_t,int64_t>> &regions);

    void countStats(int &opened, int &flagged) const;

    bool isGameActive() const { return m_gameActive; }
    void setGameActive(bool active) { m_gameActive = active; }

signals:
    void chunkNeeded(int64_t chunkX, int64_t chunkY);
    void requestSave();   // emitted when too many dirty chunks
    void mineExploded();  // игрок открыл мину

private:
    static constexpr int REGION_SHIFT = 10;
    static constexpr int CHUNK_SHIFT  = 4;
    static constexpr int MAX_CACHE    = 4096;
    static constexpr double DIRTY_SAVE_TRIGGER = 0.8;

    // Границы плотности мин для уровней 1 и MAX_DIFFICULTY.
    static constexpr double MIN_MINE_PROBABILITY = 0.04;
    static constexpr double MAX_MINE_PROBABILITY = 0.30;
    // Порог сравнения с хешем: h % CHANCE_SCALE < m_mineChanceNumerator
    static constexpr uint64_t CHANCE_SCALE = 100000;

    int64_t chunkIdx(int64_t cell) const { return cell >> CHUNK_SHIFT; }
    int64_t regionIdx(int64_t cell) const { return cell >> REGION_SHIFT; }

    std::shared_ptr<Chunk> getOrCreateChunk(int64_t cx, int64_t cy);
    void touchChunk(const std::shared_ptr<Chunk> &chunk);
    void evictIfNeeded();
    // Учёт изменений чанка. Версия с суффиксом Locked требует уже взятой блокировки.
    void markChunkDirtyLocked(const std::shared_ptr<Chunk> &chunk);
    void checkDirtyRatioLocked();

    mutable QReadWriteLock m_lock;
    QHash<ChunkCoord, std::shared_ptr<Chunk>> m_chunks;

    int64_t m_useCounter = 0;   // для вытеснения по давности
    int m_dirtyCount = 0;       // сколько чанков сейчас грязные
    bool m_saveRequested = false;

    QString m_seed;
    int m_difficulty;
    double m_mineProbability = 0.0;
    uint64_t m_mineChanceNumerator = 0;
    bool m_gameActive = true;

    uint64_t m_hash = 0;
    void computeHash();
};

#endif // WORLDMANAGER_H