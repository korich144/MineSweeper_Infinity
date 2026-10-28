#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "minefieldwidget.h"
#include "counterwidget.h"
#include "settingspanel.h"
#include "worldmanager.h"
#include "solver.h"
#include "loader.h"
#include "saver.h"
#include "settings.h"
#include "world.h"
#include "scoring.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QApplication>
#include <QStyle>

namespace {

// Имя папки мира строится из времени с точностью до секунды, поэтому два мира,
// созданные подряд, попадали в одну папку и затирали друг друга.
QString uniqueWorldFolderName(const QString &worldsDir) {
    const QString base = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    QString name = base;
    for (int i = 2; QDir(worldsDir + "/" + name).exists(); ++i)
        name = QString("%1_%2").arg(base).arg(i);
    return name;
}

// Была ли в сохранении мира записана сложность. Ноль — законный уровень
// в текущей схеме, поэтому «не задано» определяется по отметке версии:
// поле difficulty появилось в схеме 1 (уровни 1..10) ещё без версии,
// а в самых старых сохранениях его нет вовсе.
bool worldHasDifficulty(const WorldInfo &info) {
    return info.difficultyVersion >= WorldManager::DIFFICULTY_SCHEME_VERSION
        || info.difficulty != 0;
}

// Приводит сложность из сохранения мира к текущей шкале
// (см. WorldManager::DIFFICULTY_SCHEME_VERSION).
int migratedDifficulty(const WorldInfo &info) {
    if (info.difficultyVersion >= WorldManager::DIFFICULTY_SCHEME_VERSION)
        return WorldManager::clampDifficulty(info.difficulty);
    return WorldManager::clampDifficulty(WorldManager::fromLevelScheme(info.difficulty));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // поле
    m_field = new MinefieldWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(ui->frameField);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(m_field);

    // загрузка настроек
    QString settingsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    Settings::instance().load(settingsPath);
    Settings::instance().difficulty = WorldManager::clampDifficulty(Settings::instance().difficulty);
    m_statusBarVisible = Settings::instance().showStatusBar;
    if (Settings::instance().maximized)
        setWindowState(Qt::WindowMaximized);
    else
        resize(Settings::instance().windowSize);

    // папка миров
    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QDir().mkpath(worldsDir);

    QString lastWorld = Settings::instance().lastWorld;
    bool loaded = false;
    if (!lastWorld.isEmpty()) {
        QString candidatePath = worldsDir + "/" + lastWorld;
        if (QDir(candidatePath).exists()) {
            loadWorld(candidatePath);
            loaded = true;
        } else {
            QStringList dirs = QDir(worldsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &d : dirs) {
                WorldInfo info = World::load(worldsDir + "/" + d);
                if (info.originalName == lastWorld) {
                    Settings::instance().lastWorld = d;
                    info.originalName = d;
                    World::save(worldsDir + "/" + d, info);
                    saveSettings();
                    loadWorld(worldsDir + "/" + d);
                    loaded = true;
                    break;
                }
            }
        }
    }
    if (!loaded) {
        initNewWorld(World::generateSeed(), Settings::instance().difficulty);
    }

    startEngine();

    // спрайты и палитра темы
    applyTheme();

    m_scoreWidget = new CounterWidget(m_digitSprites, ui->frameHeader);
    m_timeWidget = new CounterWidget(m_digitSprites, ui->frameHeader);
    m_scoreWidget->setValue("  0");
    m_timeWidget->setValue("000");
    m_scoreFrame = new QFrame(ui->frameHeader);
    m_scoreFrame->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_scoreFrame->setLineWidth(1);
    QVBoxLayout *scoreLay = new QVBoxLayout(m_scoreFrame);
    scoreLay->setContentsMargins(0,0,0,0);
    scoreLay->addWidget(m_scoreWidget);

    m_timeFrame = new QFrame(ui->frameHeader);
    m_timeFrame->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_timeFrame->setLineWidth(1);
    QVBoxLayout *timeLay = new QVBoxLayout(m_timeFrame);
    timeLay->setContentsMargins(0,0,0,0);
    timeLay->addWidget(m_timeWidget);

    ui->frameHeader->layout()->replaceWidget(ui->scoreLabel, m_scoreFrame);
    ui->frameHeader->layout()->replaceWidget(ui->timeLabel, m_timeFrame);
    delete ui->scoreLabel;
    delete ui->timeLabel;

    // стили для счётчиков (рамка 1px sunken)
    m_scoreWidget->setStyleSheet("border: 1px inset gray; background: palette(base);");
    m_timeWidget->setStyleSheet("border: 1px inset gray; background: palette(base);");

    QTimer *uiTimer = new QTimer(this);
    connect(uiTimer, &QTimer::timeout, this, &MainWindow::updateCounters);
    uiTimer->start(33);

    m_coordLabel = new QLabel("X: 0 Y: 0", this);
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->setVisible(m_statusBarVisible);

    // Если размер окна нигде не сохранён (первый запуск) — подгоняем его так,
    // чтобы в поле помещалось ровно 9x9 клеток.
    if (!Settings::instance().hasWindowSize)
        QTimer::singleShot(0, this, &MainWindow::applyDefaultWindowSize);
}

MainWindow::~MainWindow() {
    stopEngine();
    delete ui;
}

// Читает спрайтлист текущей темы (из ресурсов, иначе из папки sprites).
void MainWindow::loadThemeSheet() {
    const QString theme = Settings::instance().theme;
    m_themeSheet = QPixmap(QString(":/themes/%1.bmp").arg(theme));
    if (m_themeSheet.isNull())
        m_themeSheet.load("sprites/" + theme + ".bmp");
    if (m_themeSheet.isNull())
        qWarning() << "Cannot load theme:" << theme;
}

void MainWindow::applyTheme() {
    loadThemeSheet();

    // Палитра интерфейса и спрайты берутся из одного и того же спрайтлиста.
    m_palette = ThemePalette::fromSheet(m_themeSheet);

    if (!m_themeSheet.isNull()) {
        m_digitSprites.clear();
        for (int i = 0; i < 14; ++i)
            m_digitSprites.append(m_themeSheet.copy(i * 13, 16, 13, 23));

        m_smileSprites.clear();
        for (int i = 0; i < 5; ++i)
            m_smileSprites.append(m_themeSheet.copy(i * 24, 39, 24, 24));
        ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
        ui->smileButton->setIconSize(QSize(24, 24));
    }
    m_field->setTheme(Settings::instance().theme);

    // Обновляем цвета счётчиков
    if (m_scoreWidget) {
        m_scoreWidget->setSprite(m_digitSprites);
        m_scoreWidget->update();
    }
    if (m_timeWidget) {
        m_timeWidget->setSprite(m_digitSprites);
        m_timeWidget->update();
    }

    // frameHeader – 2px raised
    ui->frameHeader->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    ui->frameHeader->setLineWidth(2);
    ui->frameHeader->setMidLineWidth(0);

    // frameField – 3px sunken
    ui->frameField->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    ui->frameField->setLineWidth(4);
    ui->frameField->setMidLineWidth(0);

    // Фон окна и панелей — из палитры темы
    if (m_palette.valid) {
        QString style = QString(
                            "QMainWindow { background-color: %1; }"
                            "QFrame#frameHeader { background-color: %2; }"
                            "QFrame#frameField { background-color: %3; }"
                            ).arg(m_palette.panelBg.name(),
                                  m_palette.controlFace.name(),
                                  m_palette.fieldBg.name());
        this->setStyleSheet(style);
    }

    // Панель настроек, если открыта, обновляется без пересоздания
    applyPanelTheme();
}

void MainWindow::applyPanelTheme() {
    if (!m_settingsPanel) return;
    m_settingsPanel->applyTheme(m_palette);
    m_settingsPanel->setCloseIcon(loadCloseIcon());
}

QIcon MainWindow::loadCloseIcon() const {
    if (m_themeSheet.isNull()) return QIcon();

    QIcon icon;
    icon.addPixmap(m_themeSheet.copy(120, 39, 16, 14), QIcon::Normal);
    icon.addPixmap(m_themeSheet.copy(136, 39, 16, 14), QIcon::Active);
    icon.addPixmap(m_themeSheet.copy(136, 39, 16, 14), QIcon::Selected);
    return icon;
}

void MainWindow::startEngine() {
    m_solverThread = new QThread(this);
    m_solver = new Solver(m_wm);
    m_solver->moveToThread(m_solverThread);
    connect(m_solverThread, &QThread::finished, m_solver, &QObject::deleteLater);
    m_solverThread->start();

    m_loaderThread = new QThread(this);
    m_loader = new Loader(m_wm, m_solver, m_currentWorldDir);
    m_loader->moveToThread(m_loaderThread);
    connect(m_loaderThread, &QThread::finished, m_loader, &QObject::deleteLater);
    m_loaderThread->start();

    m_saverThread = new QThread(this);
    m_saver = new Saver(m_wm, m_currentWorldDir);
    m_saver->moveToThread(m_saverThread);
    connect(m_saverThread, &QThread::finished, m_saver, &QObject::deleteLater);
    m_saverThread->start();

    connect(m_field, &MinefieldWidget::startReveal, m_solver, &Solver::startReveal, Qt::QueuedConnection);
    connect(m_field, &MinefieldWidget::firstClick, this, &MainWindow::onFirstClick);
    connect(m_saver, &Saver::showSaveIndicator, this, &MainWindow::showSaveIndicator, Qt::QueuedConnection);
    connect(m_saver, &Saver::saveFinished, this, &MainWindow::onSaveFinished, Qt::QueuedConnection);
    connect(this, &MainWindow::requestSave, m_saver, &Saver::saveNow, Qt::QueuedConnection);
    connect(m_wm, &WorldManager::requestSave, m_saver, &Saver::saveNow, Qt::QueuedConnection);

    connect(ui->smileButton, &QPushButton::pressed, this, &MainWindow::onSmilePressed);
    connect(ui->smileButton, &QPushButton::released, this, &MainWindow::onSmileReleased);
    connect(ui->smileButton, &QPushButton::clicked, this, &MainWindow::onSmileClicked);

    connect(m_field, &MinefieldWidget::flagToggled, this, &MainWindow::onFlagToggled);
    connect(m_field, &MinefieldWidget::mouseCellChanged, this, &MainWindow::onMouseCellChanged);
    connect(m_wm, &WorldManager::mineExploded, this, &MainWindow::onMineExploded);
}

void MainWindow::stopEngine() {
    disconnect(m_field, &MinefieldWidget::startReveal, m_solver, &Solver::startReveal);
    disconnect(m_field, &MinefieldWidget::firstClick, this, &MainWindow::onFirstClick);
    disconnect(m_saver, &Saver::showSaveIndicator, this, &MainWindow::showSaveIndicator);
    disconnect(m_saver, &Saver::saveFinished, this, &MainWindow::onSaveFinished);
    disconnect(this, &MainWindow::requestSave, m_saver, &Saver::saveNow);
    disconnect(m_wm, &WorldManager::requestSave, m_saver, &Saver::saveNow);
    disconnect(ui->smileButton, &QPushButton::pressed, this, &MainWindow::onSmilePressed);
    disconnect(ui->smileButton, &QPushButton::released, this, &MainWindow::onSmileReleased);
    disconnect(ui->smileButton, &QPushButton::clicked, this, &MainWindow::onSmileClicked);
    disconnect(m_field, &MinefieldWidget::flagToggled, this, &MainWindow::onFlagToggled);
    disconnect(m_field, &MinefieldWidget::mouseCellChanged, this, &MainWindow::onMouseCellChanged);
    disconnect(m_wm, &WorldManager::mineExploded, this, &MainWindow::onMineExploded);

    if (m_solverThread) {
        m_solverThread->quit();
        m_solverThread->wait();
        m_solverThread->deleteLater();
        m_solverThread = nullptr;
    }
    if (m_loaderThread) {
        m_loaderThread->quit();
        m_loaderThread->wait();
        m_loaderThread->deleteLater();
        m_loaderThread = nullptr;
    }
    if (m_saverThread) {
        m_saverThread->quit();
        m_saverThread->wait();
        m_saverThread->deleteLater();
        m_saverThread = nullptr;
    }
    m_solver = nullptr;
    m_loader = nullptr;
    m_saver = nullptr;
    m_cameraRestored = false;
}

// Внимание: вызывающий код обязан сначала сохранить текущий мир (saveCurrentWorld),
// потому что здесь старый WorldManager уничтожается вместе с движком.
void MainWindow::initNewWorld(const QString &seed, int difficulty) {
    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QString folderName = uniqueWorldFolderName(worldsDir);
    QString worldPath = worldsDir + "/" + folderName;

    const int level = WorldManager::clampDifficulty(difficulty);

    WorldInfo info;
    info.seed = seed;
    info.originalName = folderName;
    info.lastViewX = 0.0;
    info.lastViewY = 0.0;
    info.lastScale = 1.0;
    info.difficulty = level;
    info.difficultyVersion = WorldManager::DIFFICULTY_SCHEME_VERSION;
    info.gameActive = true;
    info.elapsed = 0;
    info.score = 0;
    m_flagsPlaced = 0;
    m_gamePaused = false;
    info.folderName = folderName;
    World::save(worldPath, info);

    Settings::instance().lastWorld = folderName;
    Settings::instance().difficulty = level;
    saveSettings();

    if (m_wm) {
        stopEngine();
        m_wm->deleteLater();
    }

    m_wm = new WorldManager(seed, level, this);
    m_field->setWorldManager(m_wm);
    m_currentWorldDir = worldPath;
    m_currentSeed = seed;
    m_gameStarted = false;
    m_elapsedOffset = 0;
    m_score = 0;
    m_gameTimer.invalidate();
    updateStartIllumination();

    // новый мир всегда открывается в начале координат, прижатым к левому
    // верхнему углу поля
    m_restoreScale = 1.0;
    m_restoreCellX = 0.0;
    m_restoreCellY = 0.0;
    m_restoreAlignsToOrigin = true;
    scheduleCameraRestore();
}

// Внимание: вызывающий код обязан сначала сохранить текущий мир (saveCurrentWorld).
void MainWindow::loadWorld(const QString &worldPath) {
    WorldInfo info = World::load(worldPath);
    if (info.seed.isEmpty()) {
        initNewWorld(World::generateSeed(), Settings::instance().difficulty);
        return;
    }

    // Сложность берём из самого мира; для самых старых сохранений — из настроек.
    const int level = worldHasDifficulty(info)
        ? migratedDifficulty(info)
        : WorldManager::clampDifficulty(Settings::instance().difficulty);
    Settings::instance().difficulty = level;

    m_currentWorldDir = worldPath;
    m_currentSeed = info.seed;
    if (m_wm) {
        stopEngine();
        m_wm->deleteLater();
    }

    m_wm = new WorldManager(info.seed, level, this);
    m_field->setWorldManager(m_wm);

    m_elapsedOffset = info.elapsed;
    m_flagsPlaced = static_cast<int>(info.flagsPlaced);
    m_score = static_cast<qint64>(info.score);
    m_gamePaused = !info.gameActive;
    if (m_gamePaused) {
        m_gameStarted = false;
        m_gameTimer.invalidate();
    } else {
        m_gameStarted = true;
        m_gameTimer.start();
    }

    m_restoreScale = info.lastScale > 0.0 ? info.lastScale : 1.0;
    m_restoreCellX = info.lastViewX;
    m_restoreCellY = info.lastViewY;
    m_restoreAlignsToOrigin = false;
    updateStartIllumination();
    scheduleCameraRestore();
}

void MainWindow::saveCurrentWorld() {
    if (!m_wm || m_currentWorldDir.isEmpty()) return;

    // Сначала останавливаем незаконченное открытие клеток: всё, что стояло
    // в очереди, станет ACTIVE и сохранится вместе с миром. Очки за уже
    // открытые клетки успеваем учесть.
    interruptReveal();
    applyPendingOpens();

    // Затем синхронно сбрасываем регионы на диск. Без этого при быстром
    // переключении миров успевал записаться только world.ini, а состояния
    // клеток оставались в памяти и терялись вместе со старым миром.
    flushRegions();

    WorldInfo info = World::load(m_currentWorldDir);
    const QPointF center = m_field->screenToCell(QPointF(m_field->rect().center()));
    info.lastViewX = center.x();
    info.lastViewY = center.y();
    info.lastScale = m_field->scale();
    info.score = m_score;
    info.elapsed = m_elapsedOffset + (m_gameTimer.isValid() ? m_gameTimer.elapsed() : 0);
    info.gameActive = m_wm->isGameActive() && m_gameStarted;
    info.flagsPlaced = m_flagsPlaced;
    info.difficulty = m_wm->difficulty();
    info.difficultyVersion = WorldManager::DIFFICULTY_SCHEME_VERSION;
    World::save(m_currentWorldDir, info);
}

void MainWindow::interruptReveal() {
    if (!m_solver) return;
    // Возврат означает, что поток обработки освободился и регион больше не меняется.
    m_solver->requestCancel();
}

bool MainWindow::flushRegions() {
    if (!m_saver) return false;
    // Поток сохранения уже остановлен — блокирующий вызов повесил бы UI.
    if (m_saverThread && !m_saverThread->isRunning()) return false;
    if (QThread::currentThread() == m_saver->thread())
        return QMetaObject::invokeMethod(m_saver, "saveNow", Qt::DirectConnection);
    return QMetaObject::invokeMethod(m_saver, "saveNow", Qt::BlockingQueuedConnection);
}

void MainWindow::applyPendingOpens() {
    if (!m_solver || !m_wm) return;
    const int opened = m_solver->takeOpenedTotal();
    if (opened > 0)
        m_score += Scoring::revealScore(opened, m_wm->difficulty());
    if (m_score < 0) m_score = 0;
}

void MainWindow::saveSettings() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    Settings::instance().save(path);
}

void MainWindow::scheduleCameraRestore() {
    m_cameraRestored = false;
    QTimer::singleShot(0, this, &MainWindow::restoreCamera);
}

void MainWindow::onFirstClick() {
    if (!m_gameStarted && m_wm && m_wm->isGameActive()) {
        m_gameStarted = true;
        m_gameTimer.start();
    }
    // Первый клик уже открыл клетку — подсветка центра больше не нужна.
    if (m_field) m_field->setStartIllumination(false);
}

// Пока в мире нет ни одной открытой клетки, туман закрывает всё поле и
// кликнуть было бы некуда, поэтому центр экрана подсвечивается. Заодно это
// признак «игра ещё не начиналась»: по нему первый клик подводит под курсор
// заведомо безопасную клетку.
void MainWindow::updateStartIllumination() {
    if (!m_field) return;
    m_field->setStartIllumination(!m_gameStarted && m_wm && !m_wm->hasOpenedCells());
}

void MainWindow::updateCounters() {
    if (!m_wm) return;

    // Очки за клетки, открытые решателем в фоновом потоке
    applyPendingOpens();

    // --- Время ---
    QString timeStr;
    qint64 ms = 0;
    if (m_gameStarted && m_wm->isGameActive()) {
        ms = m_elapsedOffset + m_gameTimer.elapsed();
    } else {
        ms = m_elapsedOffset;
    }
    int totalSeconds = ms / 1000;

    if (totalSeconds < 60) {
        timeStr = QString("%1").arg(totalSeconds, 3, 10, QChar('0'));
    } else if (totalSeconds < 3600) {
        int mins = totalSeconds / 60;
        int secs = totalSeconds % 60;
        timeStr = QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    } else if (totalSeconds < 86400) {
        int hours = totalSeconds / 3600;
        int mins = (totalSeconds % 3600) / 60;
        int secs = totalSeconds % 60;
        timeStr = QString("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    } else {
        int days = totalSeconds / 86400;
        int rem = totalSeconds % 86400;
        int hours = rem / 3600;
        int mins = (rem % 3600) / 60;
        int secs = rem % 60;
        timeStr = QString("%1:%2:%3:%4").arg(days).arg(hours, 2, 10, QChar('0'))
                      .arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    }
    m_timeWidget->setValue(timeStr);

    // --- Очки ---
    m_scoreWidget->setValue(Scoring::format(m_score));

    int opened, flagged;
    m_wm->countStats(opened, flagged);
    const QString density = QString::number(m_wm->mineProbability() * 100.0, 'f', 1) + "%";
    const QString status = QString("Сложность: %1/%2 | Плотность мин: %3 | Флажков: %4 | Открыто: %5")
                               .arg(m_wm->difficulty())
                               .arg(WorldManager::MAX_DIFFICULTY)
                               .arg(density)
                               .arg(m_flagsPlaced)
                               .arg(opened);
    statusBar()->showMessage(status);

    if (m_smileSprites.size() >= 5) {
        if (!m_wm->isGameActive())
            ui->smileButton->setIcon(QIcon(m_smileSprites[2]));
        else
            ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
    }

    m_field->update();
}

void MainWindow::onMineExploded() {
    if (!m_wm) return;
    m_score -= Scoring::minePenalty(m_wm->difficulty());
    if (m_score < 0) m_score = 0;
}

void MainWindow::onFlagToggled(int delta) {
    m_flagsPlaced += delta;
    if (m_flagsPlaced < 0) m_flagsPlaced = 0;
}

void MainWindow::onMouseCellChanged(int64_t x, int64_t y) {
    m_coordLabel->setText(QString("X: %1 Y: %2").arg(x).arg(y));
}

void MainWindow::restoreCamera() {
    if (m_cameraRestored || !m_field || !m_wm) return;
    m_cameraRestored = true;
    m_field->setOffset(0, 0);
    m_field->setScale(m_restoreScale);
    if (m_restoreAlignsToOrigin)
        return;      // новый мир: клетка (0,0) уже в левом верхнем углу поля
    m_field->centerOn(m_restoreCellX, m_restoreCellY);
}

// Первый запуск (размера окна в настройках нет): подгоняем окно так, чтобы
// поле было ровно 9x9 клеток. Обвес окна (рамка поля, шапка, отступы) измеряем
// по текущей геометрии, а не считаем константами раскладки.
void MainWindow::applyDefaultWindowSize() {
    if (isMaximized() || !m_field) return;

    const int chromeW = width() - m_field->width();
    const int chromeH = height() - m_field->height();
    if (chromeW <= 0 || chromeH <= 0) return;   // раскладка ещё не посчитана

    const int cellSize = qMax(1, qRound(16 * m_field->scale()));
    const int fieldSide = MinefieldWidget::DEFAULT_FIELD_CELLS * cellSize;
    resize(fieldSide + chromeW, fieldSide + chromeH);

    // Размер поля стал кратен клетке — прижимаем камеру к началу координат,
    // чтобы клетки прилегали к рамке ровно.
    m_field->setOffset(0, 0);
}

void MainWindow::toggleStatusBar() {
    m_statusBarVisible = !m_statusBarVisible;
    statusBar()->setVisible(m_statusBarVisible);
    adjustWindowSizeForStatusBar(m_statusBarVisible);
    Settings::instance().showStatusBar = m_statusBarVisible;
    saveSettings();
}

void MainWindow::onSmilePressed() {
    if (m_smileSprites.size() >= 5)
        ui->smileButton->setIcon(QIcon(m_smileSprites[0]));
}

void MainWindow::onSmileReleased() {
    if (m_smileSprites.size() >= 5) {
        if (m_wm && !m_wm->isGameActive())
            ui->smileButton->setIcon(QIcon(m_smileSprites[2]));
        else
            ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
    }
}

void MainWindow::onSmileClicked() {
    // Смайлик перезапускает игру: та же сложность, новый случайный сид.
    const int diff = panelDifficulty(Settings::instance().difficulty);
    const QString newSeed = World::generateSeed();

    saveCurrentWorld();
    stopEngine();
    if (m_wm) {
        m_wm->deleteLater();
        m_wm = nullptr;
    }
    initNewWorld(newSeed, diff);
    startEngine();

    if (m_settingsPanel) {
        m_settingsPanel->setSeed(newSeed);
        m_settingsPanel->setDifficulty(Settings::instance().difficulty);
        m_settingsPanel->clearWarnings();
        m_diffChanged = false;
        m_seedChanged = false;
    }
}

void MainWindow::onSaveFinished() {
    if (m_restartAfterSave) {
        m_restartAfterSave = false;
        // Сохраняем текущий мир до того, как остановим движок.
        saveCurrentWorld();
        stopEngine();
        if (m_wm) {
            m_wm->deleteLater();
            m_wm = nullptr;
        }
        const int diff = panelDifficulty(Settings::instance().difficulty);
        QString seed = World::generateSeed();
        if (m_settingsPanel) {
            seed = m_settingsPanel->seedEdit()->text().trimmed();
            if (seed.isEmpty()) seed = World::generateSeed();
        }
        initNewWorld(seed, diff);
        startEngine();
        if (m_settingsPanel) {
            m_settingsPanel->setSeed(m_currentSeed);
            m_settingsPanel->setDifficulty(Settings::instance().difficulty);
            m_settingsPanel->clearWarnings();
        }
    }
}

void MainWindow::showSaveIndicator() {
    statusBar()->showMessage("Сохранение...", 1000);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // saveCurrentWorld сам прерывает открытие клеток и сбрасывает регионы на диск
    saveCurrentWorld();

    Settings::instance().windowSize = size();
    Settings::instance().maximized = isMaximized();
    saveSettings();

    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_overlay)
        m_overlay->setGeometry(rect());
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_overlay) {
            m_overlay->deleteLater();
            m_overlay = nullptr;
            m_settingsPanel = nullptr;
        } else {
            setupOverlay();
        }
        return;
    } else if (event->key() == Qt::Key_F3) {
        toggleStatusBar();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// -------------------- Окно настроек --------------------
int MainWindow::panelDifficulty(int fallback) const {
    if (!m_settingsPanel) return WorldManager::clampDifficulty(fallback);

    bool ok = false;
    const int val = m_settingsPanel->difficultyEdit()->text().toInt(&ok);
    if (!ok || val < WorldManager::MIN_DIFFICULTY || val > WorldManager::MAX_DIFFICULTY)
        return WorldManager::clampDifficulty(fallback);
    return WorldManager::clampDifficulty(val);
}

void MainWindow::setupOverlay() {
    if (m_overlay) {
        m_overlay->raise();
        m_overlay->show();
        return;
    }

    // Затемнение поверх окна; панель настроек растягивается вместе с ним
    // и ужимается до минимума, после чего включается прокрутка.
    m_overlay = new QFrame(this);
    m_overlay->setObjectName("settingsOverlay");
    m_overlay->setStyleSheet("QFrame#settingsOverlay { background-color: rgba(0,0,0,150); }");
    m_overlay->setGeometry(rect());

    QGridLayout *overlayLayout = new QGridLayout(m_overlay);
    overlayLayout->setContentsMargins(16, 16, 16, 16);
    overlayLayout->setSpacing(0);

    m_settingsPanel = new SettingsPanel(m_overlay);
    overlayLayout->addWidget(m_settingsPanel, 0, 0);
    overlayLayout->setRowStretch(0, 1);
    overlayLayout->setColumnStretch(0, 1);

    // Значения ставим до подключения сигналов, чтобы не дёргать обработчики зря
    m_settingsPanel->setDifficulty(Settings::instance().difficulty);
    m_settingsPanel->setSeed(m_currentSeed);
    m_settingsPanel->themeCombo()->setCurrentText(Settings::instance().theme);
    applyPanelTheme();

    connect(m_settingsPanel, &SettingsPanel::closeRequested, this, &MainWindow::onOverlayClose);
    connect(m_settingsPanel->difficultyEdit(), &QLineEdit::textChanged, this, &MainWindow::onDifficultyTextChanged);
    connect(m_settingsPanel->seedEdit(), &QLineEdit::textChanged, this, &MainWindow::onSeedTextChanged);
    connect(m_settingsPanel->themeCombo(), &QComboBox::currentTextChanged, this, &MainWindow::onThemeChanged);
    connect(m_settingsPanel->regenerateButton(), &QPushButton::clicked, this, &MainWindow::onRegenerateWorld);
    connect(m_settingsPanel->loadWorldButton(), &QPushButton::clicked, this, &MainWindow::onLoadWorld);

    updateWorldList();
    m_diffChanged = false;
    m_seedChanged = false;
    onDifficultyTextChanged(m_settingsPanel->difficultyEdit()->text());
    onSeedTextChanged(m_settingsPanel->seedEdit()->text());

    m_overlay->show();
    m_overlay->raise();
}

void MainWindow::onOverlayClose() {
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
        m_settingsPanel = nullptr;
    }
}

void MainWindow::onDifficultyTextChanged(const QString &text) {
    if (!m_settingsPanel) return;

    bool ok = false;
    const int val = text.toInt(&ok);
    const bool valid = ok && val >= WorldManager::MIN_DIFFICULTY && val <= WorldManager::MAX_DIFFICULTY;

    if (valid) {
        const QString density = QString::number(WorldManager::mineProbabilityFor(val) * 100.0, 'f', 1) + "%";
        m_settingsPanel->setDifficultyInfo(QString("Плотность мин на этом уровне: %1").arg(density));
        if (val <= 0)
            m_settingsPanel->setDifficultyInfo(QString("Плотность мин на этом уровне: %1, очки не начисляются")
                                                   .arg(density));
    } else {
        m_settingsPanel->setDifficultyInfo("Уровень 0 — базовый, выше — плотнее, ниже — разреженнее");
    }

    if (!valid) {
        m_settingsPanel->setDifficultyWarning(text.isEmpty()
            ? QString()
            : QString("⚠ Допустимо от %1 до %2")
                  .arg(WorldManager::MIN_DIFFICULTY)
                  .arg(WorldManager::MAX_DIFFICULTY));
        m_diffChanged = false;
        return;
    }

    if (val != Settings::instance().difficulty) {
        m_diffChanged = true;
        m_settingsPanel->setDifficultyWarning("⚠ Изменения вступят в силу только для следующего мира");
    } else {
        m_diffChanged = false;
        m_settingsPanel->setDifficultyWarning(QString());
    }
}

void MainWindow::onSeedTextChanged(const QString &text) {
    if (!m_settingsPanel) return;

    if (text != m_currentSeed) {
        m_seedChanged = true;
        m_settingsPanel->setSeedWarning("⚠ Изменения вступят в силу только для следующего мира");
    } else {
        m_seedChanged = false;
        m_settingsPanel->setSeedWarning(QString());
    }
}

void MainWindow::onThemeChanged(const QString &theme) {
    Settings::instance().theme = theme;
    saveSettings();
    applyTheme();   // спрайтлист, палитра и стили перечитываются целиком
}

void MainWindow::onRegenerateWorld() {
    // Сохраняем текущий мир
    saveCurrentWorld();

    const int diff = panelDifficulty(Settings::instance().difficulty);
    QString seed = m_settingsPanel ? m_settingsPanel->seedEdit()->text().trimmed() : QString();
    if (seed.isEmpty()) seed = World::generateSeed();

    // Пересоздаём мир с новыми параметрами
    stopEngine();
    if (m_wm) {
        m_wm->deleteLater();
        m_wm = nullptr;
    }
    initNewWorld(seed, diff);
    startEngine();

    // Обновляем список миров (появилась новая папка) и поля
    updateWorldList();
    if (m_settingsPanel) {
        m_settingsPanel->setSeed(m_currentSeed);
        m_settingsPanel->setDifficulty(Settings::instance().difficulty);
        m_settingsPanel->clearWarnings();
    }
    m_diffChanged = false;
    m_seedChanged = false;
}

void MainWindow::onLoadWorld() {
    if (!m_settingsPanel) return;

    const QString folder = m_settingsPanel->selectedWorld();
    if (folder.isEmpty()) return;

    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QString worldPath = worldsDir + "/" + folder;
    if (!QDir(worldPath).exists()) return;

    // Сохраняем текущий мир
    saveCurrentWorld();

    // Загружаем выбранный: позиция и сложность берутся из его сохранения
    loadWorld(worldPath);
    startEngine();

    Settings::instance().lastWorld = folder;
    saveSettings();

    // Обновляем поля панели
    m_settingsPanel->setSeed(m_currentSeed);
    m_settingsPanel->setDifficulty(Settings::instance().difficulty);
    m_settingsPanel->clearWarnings();
    m_diffChanged = false;
    m_seedChanged = false;

    updateWorldList();
    onDifficultyTextChanged(m_settingsPanel->difficultyEdit()->text());
    onSeedTextChanged(m_settingsPanel->seedEdit()->text());
}

void MainWindow::updateWorldList() {
    if (!m_settingsPanel) return;

    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QStringList dirs = QDir(worldsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    m_settingsPanel->setWorldList(dirs, Settings::instance().lastWorld);
}

void MainWindow::adjustWindowSizeForStatusBar(bool show) {
    if (!statusBar()||isMaximized()) return;
    int sbHeight = statusBar()->sizeHint().height();
    if (sbHeight <= 0) sbHeight = 20;
    QSize currentSize = size();
    if (show) {
        resize(currentSize.width(), currentSize.height() + sbHeight);
    } else {
        resize(currentSize.width(), currentSize.height() - sbHeight);
    }
}
