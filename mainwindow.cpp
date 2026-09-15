#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "minefieldwidget.h"
#include "counterwidget.h"
#include "worldmanager.h"
#include "solver.h"
#include "loader.h"
#include "saver.h"
#include "settings.h"
#include "world.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
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
#include <QListView>
#include <QStringListModel>
#include <QPainter>
#include <QApplication>
#include <QStyle>

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
        initNewWorld(World::generateSeed());
    }

    startEngine();

    // спрайты темы
    loadThemeColors();
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
    QTimer::singleShot(0, this, &MainWindow::restoreCamera);
}

MainWindow::~MainWindow() {
    stopEngine();
    delete ui;
}

// В функции loadThemeColors:
void MainWindow::loadThemeColors() {
    m_themeColors.clear();
    QString theme = Settings::instance().theme;
    QString path = QString(":/themes/%1.bmp").arg(theme);
    QPixmap sheet(path);
    if (sheet.isNull()) {
        sheet.load("sprites/" + theme + ".bmp");
    }
    if (sheet.isNull()) {
        qWarning() << "Cannot load theme for colors:" << theme;
        return;
    }
    QImage img = sheet.toImage();
    if (img.height() <= 63) return;
    for (int x = 0; x < qMin(16, img.width()); ++x) {
        m_themeColors.append(img.pixelColor(x, 63));
    }
    // дополняем
    while (m_themeColors.size() < 16) {
        m_themeColors.append(Qt::gray);
    }
}

void MainWindow::applyTheme() {
    // Загружаем спрайты для счётчиков и смайликов (как было)
    QString theme = Settings::instance().theme;
    QString path = QString(":/themes/%1.bmp").arg(theme);
    QPixmap sheet(path);
    if (sheet.isNull()) sheet.load("sprites/" + theme + ".bmp");
    if (!sheet.isNull()) {
        m_digitSprites.clear();
        for (int i = 0; i < 14; ++i)
            m_digitSprites.append(sheet.copy(i * 13, 16, 13, 23));

        m_smileSprites.clear();
        for (int i = 0; i < 5; ++i) {
            QPixmap sp = sheet.copy(i * 24, 39, 24, 24);
            m_smileSprites.append(sp);
        }
        ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
        ui->smileButton->setIconSize(QSize(24, 24));
    }
    m_field->setTheme(theme);

    // Обновляем цвета счётчиков
    if (m_scoreWidget) {
        m_scoreWidget->setSprite(m_digitSprites);
        m_scoreWidget->update();
    }
    if (m_timeWidget) {
        m_timeWidget->setSprite(m_digitSprites);
        m_timeWidget->update();
    }

    // Применяем стили XP
    // frameHeader – 2px raised
    ui->frameHeader->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    ui->frameHeader->setLineWidth(2);
    ui->frameHeader->setMidLineWidth(0);

    // frameField – 3px sunken
    ui->frameField->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    ui->frameField->setLineWidth(4);
    ui->frameField->setMidLineWidth(0);

    // Цвета из m_themeColors (если есть)
    if (m_themeColors.size() >= 8) {
        QColor bg = m_themeColors[0];
        QColor panelBg = m_themeColors[1];
        QColor textColor = m_themeColors[2];
        QColor lightBorder = m_themeColors[4];
        QColor darkBorder = m_themeColors[5];
        QColor fieldBg = m_themeColors[6];

        QString style = QString(
                            "QMainWindow { background-color: %1; }"
                            "QFrame#frameHeader { background-color: %2; }"
                            "QFrame#frameField { background-color: %3; }"
                            ).arg(bg.name(), panelBg.name(), fieldBg.name());

        // Можно добавить стили для других элементов
        this->setStyleSheet(style);
    }

    // Обновляем overlay, если открыт
    if (m_overlay && m_overlay->isVisible()) {
        // обновим цвета градиента и т.д. (см. setupOverlay)
        // для простоты пересоздадим overlay
        m_overlay->deleteLater();
        m_overlay = nullptr;
        setupOverlay();
    }
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

    connect(m_solver, &Solver::cellsOpened, this, &MainWindow::onCellsOpened, Qt::QueuedConnection);
    connect(m_field, &MinefieldWidget::flagToggled, this, &MainWindow::onFlagToggled);
    connect(m_field, &MinefieldWidget::mouseCellChanged, this, &MainWindow::onMouseCellChanged);
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
    disconnect(m_solver, &Solver::cellsOpened, this, &MainWindow::onCellsOpened);
    disconnect(m_field, &MinefieldWidget::flagToggled, this, &MainWindow::onFlagToggled);
    disconnect(m_field, &MinefieldWidget::mouseCellChanged, this, &MainWindow::onMouseCellChanged);

    m_solverThread->quit();
    m_loaderThread->quit();
    m_saverThread->quit();
    m_solverThread->wait();
    m_loaderThread->wait();
    m_saverThread->wait();
    m_solverThread->deleteLater();
    m_loaderThread->deleteLater();
    m_saverThread->deleteLater();
    m_solverThread = m_loaderThread = m_saverThread = nullptr;
    m_solver = nullptr;
    m_loader = nullptr;
    m_saver = nullptr;
    m_cameraRestored = false;
}

void MainWindow::initNewWorld(const QString &seed, bool fromOverlay) {
    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QString folderName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    QString worldPath = worldsDir + "/" + folderName;

    WorldInfo info;
    info.seed = seed;
    info.originalName = folderName;
    info.lastViewX = 0;
    info.lastViewY = 0;
    info.lastScale = 1.0;
    info.gameActive = true;
    info.elapsed = 0;
    info.score = 0;
    m_flagsPlaced = 0;
    m_gamePaused = false;
    info.folderName = folderName;
    World::save(worldPath, info);

    Settings::instance().lastWorld = folderName;
    if (!fromOverlay) saveSettings();

    if (m_wm) {
        stopEngine();
        m_wm->deleteLater();
    }

    m_wm = new WorldManager(seed, Settings::instance().difficulty, this);
    m_field->setWorldManager(m_wm);
    m_currentWorldDir = worldPath;
    m_currentSeed = seed;
    m_gameStarted = false;
    m_elapsedOffset = 0;
    m_score = 0;
    m_gameTimer.invalidate();

    m_restoreScale = 1.0;
    m_restoreCellX = 0;
    m_restoreCellY = 0;
}

void MainWindow::loadWorld(const QString &worldPath) {
    WorldInfo info = World::load(worldPath);
    if (info.seed.isEmpty()) {
        initNewWorld(World::generateSeed());
        return;
    }

    m_currentWorldDir = worldPath;
    m_currentSeed = info.seed;
    if (m_wm) {
        stopEngine();
        m_wm->deleteLater();
    }

    m_wm = new WorldManager(info.seed, Settings::instance().difficulty, this);
    m_field->setWorldManager(m_wm);

    m_gameStarted = info.gameActive;
    m_elapsedOffset = info.elapsed;
    m_flagsPlaced = static_cast<int>(info.flagsPlaced);
    m_score = static_cast<int>(info.score);
    m_gamePaused = !info.gameActive;
    if (m_gamePaused) {
        m_gameStarted = false;
    } else {
        m_gameStarted = info.gameActive;
        if (m_gameStarted)
            m_gameTimer.start();
        else
            m_gameTimer.invalidate();
    }

    m_restoreScale = info.lastScale;
    m_restoreCellX = info.lastViewX;
    m_restoreCellY = info.lastViewY;
}

void MainWindow::saveCurrentWorld() {
    if (m_wm && !m_currentWorldDir.isEmpty()) {
        WorldInfo info = World::load(m_currentWorldDir);
        QPointF center = m_field->screenToCell(m_field->rect().center());
        info.lastViewX = static_cast<qint64>(qFloor(center.x()));
        info.lastViewY = static_cast<qint64>(qFloor(center.y()));
        info.lastScale = m_field->scale();
        info.score = m_score;
        info.elapsed = m_elapsedOffset + (m_gameTimer.isValid() ? m_gameTimer.elapsed() : 0);
        info.gameActive = m_wm->isGameActive() && m_gameStarted;
        info.flagsPlaced = m_flagsPlaced;
        World::save(m_currentWorldDir, info);
    }
}

void MainWindow::saveSettings() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    Settings::instance().save(path);
}

void MainWindow::onFirstClick() {
    if (!m_gameStarted && m_wm && m_wm->isGameActive()) {
        m_gameStarted = true;
        m_gameTimer.start();
    }
}

void MainWindow::updateCounters() {
    if (!m_wm) return;

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
    qint64 score = m_score;
    QString scoreStr;
    if (score < 1000) {
        scoreStr = QString("%1").arg(score, 3, 10, QChar(' '));
    } else {
        scoreStr = QString::number(score);
    }
    m_scoreWidget->setValue(scoreStr);

    int opened, flagged;
    m_wm->countStats(opened, flagged);
    QString status = QString("Сложность: %1 | Флажков: %2 | Открыто: %3")
                         .arg(m_wm->difficulty()).arg(m_flagsPlaced).arg(opened);
    statusBar()->showMessage(status);

    if (m_smileSprites.size() >= 5) {
        if (!m_wm->isGameActive())
            ui->smileButton->setIcon(QIcon(m_smileSprites[2]));
        else
            ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
    }

    m_field->update();
}

void MainWindow::onCellsOpened(int count) {
    if (!m_wm) return;
    int diff = m_wm->difficulty();
    if (diff > 0) {
        m_score += count * diff / 10;
    }
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
    m_field->centerOn(m_restoreCellX, m_restoreCellY);
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
    // При нажатии смайлика перезапускаем с новой сложностью (из поля, если открыто) и случайным сидом
    int diff = Settings::instance().difficulty;
    if (m_overlay && m_overlay->isVisible() && m_diffEdit) {
        bool ok;
        int val = m_diffEdit->text().toInt(&ok);
        if (ok && val > 0) diff = val;
    }
    // генерируем случайный сид
    QString newSeed = World::generateSeed();
    // сохраняем текущий мир
    saveCurrentWorld();
    // создаём новый мир
    stopEngine();
    if (m_wm) {
        m_wm->deleteLater();
        m_wm = nullptr;
    }
    initNewWorld(newSeed, false);
    startEngine();
    // если overlay открыт, обновим поле сида
    if (m_overlay && m_overlay->isVisible() && m_seedEdit) {
        m_seedEdit->setText(newSeed);
        m_seedChanged = false;
        hideWarningLabels();
    }
}

void MainWindow::onSaveFinished() {
    if (m_restartAfterSave) {
        m_restartAfterSave = false;
        stopEngine();
        if (m_wm) {
            m_wm->deleteLater();
            m_wm = nullptr;
        }
        // Используем текущие настройки из overlay (если открыт) или из Settings
        int diff = Settings::instance().difficulty;
        QString seed = World::generateSeed();
        if (m_overlay && m_overlay->isVisible()) {
            bool ok;
            int val = m_diffEdit->text().toInt(&ok);
            if (ok && val > 0) diff = val;
            seed = m_seedEdit->text().trimmed();
            if (seed.isEmpty()) seed = World::generateSeed();
        }
        // создаём новый мир с этими параметрами
        initNewWorld(seed, true);
        startEngine();
        hideWarningLabels();
    }
}

void MainWindow::showSaveIndicator() {
    statusBar()->showMessage("Сохранение...", 1000);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrentWorld();

    Settings::instance().windowSize = size();
    Settings::instance().maximized = isMaximized();
    saveSettings();

    emit requestSave();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_overlay) {
            m_overlay->deleteLater();
            m_overlay = nullptr;
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

// -------------------- Overlay --------------------
// В setupOverlay() – переделано окно параметров
void MainWindow::setupOverlay() {
    if (m_overlay) {
        m_overlay->raise();
        m_overlay->show();
        return;
    }

    m_overlay = new QFrame(this);
    m_overlay->setStyleSheet("background-color: rgba(0,0,0,150);");
    m_overlay->setGeometry(rect());
    m_overlay->show();
    m_overlay->raise();

    // Главная рамка – выпуклая
    QFrame *mainFrame = new QFrame(m_overlay);
    mainFrame->setFrameStyle(QFrame::Panel | QFrame::Raised);
    mainFrame->setLineWidth(2);
    QColor bgColor = m_themeColors.size() > 0 ? m_themeColors[0] : palette().window().color();
    mainFrame->setStyleSheet(QString("background-color: %1;").arg(bgColor.name()));
    mainFrame->setFixedSize(650, 350);
    mainFrame->move((width() - 650) / 2, (height() - 350) / 2);
    mainFrame->show();
    mainFrame->raise();

    QVBoxLayout *mainLayout = new QVBoxLayout(mainFrame);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);

    // Верхняя панель с градиентом
    QFrame *titleFrame = new QFrame(mainFrame);
    titleFrame->setMaximumHeight(24);
    titleFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    QColor gradStart = m_themeColors.size() > 8 ? m_themeColors[8] : QColor(0,0,128);
    QColor gradEnd = m_themeColors.size() > 9 ? m_themeColors[9] : QColor(100,149,237);
    QString gradStyle = QString("QFrame { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2); }")
                            .arg(gradStart.name(), gradEnd.name());
    titleFrame->setStyleSheet(gradStyle);
    QHBoxLayout *titleLayout = new QHBoxLayout(titleFrame);
    titleLayout->setContentsMargins(8,2,2,2);
    titleLayout->setSpacing(0);

    QLabel *titleLabel = new QLabel("Параметры", titleFrame);
    titleLabel->setStyleSheet("color: white; font-weight: bold; background: transparent;");
    titleLayout->addWidget(titleLabel);

    titleLayout->addStretch();

    // Кнопка закрытия со спрайтами
    QPushButton *closeBtn = new QPushButton(titleFrame);
    closeBtn->setFixedSize(16, 14);
    // загружаем спрайты из той же темы
    QString theme = Settings::instance().theme;
    QString path = QString(":/themes/%1.bmp").arg(theme);
    QPixmap sheet(path);
    if (sheet.isNull()) sheet.load("sprites/" + theme + ".bmp");
    if (!sheet.isNull()) {
        QPixmap normal = sheet.copy(120, 39, 16, 14);
        QPixmap pressed = sheet.copy(136, 39, 16, 14);
        QIcon icon;
        icon.addPixmap(normal, QIcon::Normal);
        icon.addPixmap(pressed, QIcon::Active);
        icon.addPixmap(pressed, QIcon::Selected);
        closeBtn->setIcon(icon);
        closeBtn->setIconSize(QSize(16,14));
    }
    closeBtn->setStyleSheet("border: none; background: transparent;");
    connect(closeBtn, &QPushButton::clicked, this, &MainWindow::onOverlayClose);
    titleLayout->addWidget(closeBtn);

    mainLayout->addWidget(titleFrame);

    // Содержимое – без отступов
    QFrame *contentFrame = new QFrame(mainFrame);
    contentFrame->setFrameStyle(QFrame::NoFrame);
    QHBoxLayout *contentLayout = new QHBoxLayout(contentFrame);
    contentLayout->setContentsMargins(0,0,0,0);
    contentLayout->setSpacing(0);   // плотно

    // Левая колонка (настройки)
    QFrame *leftCol = new QFrame(contentFrame);
    leftCol->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftCol);
    leftLayout->setContentsMargins(10,10,10,10);
    leftLayout->setSpacing(8);

    // Сложность
    QHBoxLayout *diffLayout = new QHBoxLayout();
    QLabel *diffLabel = new QLabel("Сложность", leftCol);
    diffLabel->setStyleSheet("background: transparent; border: none;");
    diffLabel->setFixedWidth(80);
    m_diffEdit = new QLineEdit(leftCol);
    m_diffEdit->setText(QString::number(Settings::instance().difficulty));
    m_diffEdit->setStyleSheet("border: 1px inset gray; background: white;");
    connect(m_diffEdit, &QLineEdit::textChanged, this, &MainWindow::onDifficultyTextChanged);
    diffLayout->addWidget(diffLabel);
    diffLayout->addWidget(m_diffEdit);
    leftLayout->addLayout(diffLayout);

    m_warnDiff = new QLabel("⚠ Изменения вступят в силу только для следующего мира", leftCol);
    m_warnDiff->setStyleSheet("color: red; font-size: 8pt; background: transparent; border: none;");
    m_warnDiff->setVisible(false);
    leftLayout->addWidget(m_warnDiff);

    // Сид
    QHBoxLayout *seedLayout = new QHBoxLayout();
    QLabel *seedLabel = new QLabel("Сид мира", leftCol);
    seedLabel->setStyleSheet("background: transparent; border: none;");
    seedLabel->setFixedWidth(80);
    m_seedEdit = new QLineEdit(leftCol);
    m_seedEdit->setText(m_currentSeed);
    m_seedEdit->setStyleSheet("border: 1px inset gray; background: white;");
    connect(m_seedEdit, &QLineEdit::textChanged, this, &MainWindow::onSeedTextChanged);
    seedLayout->addWidget(seedLabel);
    seedLayout->addWidget(m_seedEdit);
    leftLayout->addLayout(seedLayout);

    m_warnSeed = new QLabel("⚠ Изменения вступят в силу только для следующего мира", leftCol);
    m_warnSeed->setStyleSheet("color: red; font-size: 8pt; background: transparent; border: none;");
    m_warnSeed->setVisible(false);
    leftLayout->addWidget(m_warnSeed);

    // Тема
    QHBoxLayout *themeLayout = new QHBoxLayout();
    QLabel *themeLabel = new QLabel("Тема", leftCol);
    themeLabel->setStyleSheet("background: transparent; border: none;");
    themeLabel->setFixedWidth(80);
    m_themeCombo = new QComboBox(leftCol);
    m_themeCombo->addItem("light");
    m_themeCombo->addItem("dark");
    m_themeCombo->setCurrentText(Settings::instance().theme);
    m_themeCombo->setStyleSheet("border: 1px inset gray; background: white;");
    connect(m_themeCombo, &QComboBox::currentTextChanged, this, &MainWindow::onThemeChanged);
    themeLayout->addWidget(themeLabel);
    themeLayout->addWidget(m_themeCombo);
    leftLayout->addLayout(themeLayout);

    leftLayout->addStretch();

    m_regenerateBtn = new QPushButton("Пересоздать мир", leftCol);
    m_regenerateBtn->setStyleSheet("border: 2px outset gray; background: lightgray;");
    connect(m_regenerateBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateWorld);
    leftLayout->addWidget(m_regenerateBtn);

    contentLayout->addWidget(leftCol, 2);

    // Правая колонка (миры)
    QFrame *rightCol = new QFrame(contentFrame);
    rightCol->setFrameStyle(QFrame::NoFrame);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(10,10,10,10);
    rightLayout->setSpacing(8);

    QLabel *worldsLabel = new QLabel("Миры", rightCol);
    worldsLabel->setStyleSheet("background: transparent; border: none;");
    rightLayout->addWidget(worldsLabel);

    m_worldListView = new QListView(rightCol);
    m_worldListModel = new QStringListModel(this);
    m_worldListView->setModel(m_worldListModel);
    m_worldListView->setStyleSheet("border: 1px inset gray; background: white;");
    rightLayout->addWidget(m_worldListView);

    m_loadWorldBtn = new QPushButton("Загрузить", rightCol);
    m_loadWorldBtn->setStyleSheet("border: 2px outset gray; background: lightgray;");
    connect(m_loadWorldBtn, &QPushButton::clicked, this, &MainWindow::onLoadWorld);
    rightLayout->addWidget(m_loadWorldBtn);

    contentLayout->addWidget(rightCol, 1);

    mainLayout->addWidget(contentFrame);

    // ... остальное (список миров, скрытие предупреждений)
    updateWorldList();
    hideWarningLabels();
    m_diffChanged = false;
    m_seedChanged = false;
}

void MainWindow::onOverlayClose() {
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
}

void MainWindow::onDifficultyTextChanged(const QString &text) {
    bool ok;
    int val = text.toInt(&ok);
    if (ok && val > 0) {
        m_diffChanged = true;
        m_warnDiff->setVisible(true);
    } else {
        m_warnDiff->setVisible(false);
        m_diffChanged = false;
    }
}

void MainWindow::onSeedTextChanged(const QString &text) {
    if (text != m_currentSeed) {
        m_seedChanged = true;
        m_warnSeed->setVisible(true);
    } else {
        m_seedChanged = false;
        m_warnSeed->setVisible(false);
    }
}

void MainWindow::onThemeChanged(const QString &theme) {
    Settings::instance().theme = theme;
    saveSettings();
    loadThemeColors();
    applyTheme();
    // тема применяется сразу
}

void MainWindow::onRegenerateWorld() {
    // Сохраняем текущий мир
    saveCurrentWorld();

    // Читаем сложность и сид из полей
    bool ok;
    int diff = m_diffEdit->text().toInt(&ok);
    if (!ok || diff <= 0) diff = Settings::instance().difficulty;
    QString seed = m_seedEdit->text().trimmed();
    if (seed.isEmpty()) seed = World::generateSeed();

    // Применяем сложность в настройки
    Settings::instance().difficulty = diff;
    saveSettings();

    // Пересоздаём мир
    stopEngine();
    if (m_wm) {
        m_wm->deleteLater();
        m_wm = nullptr;
    }
    initNewWorld(seed, true);
    startEngine();

    // Обновляем список миров (может появиться новая папка)
    updateWorldList();

    // Скрываем предупреждения
    hideWarningLabels();
    m_diffChanged = false;
    m_seedChanged = false;
}

void MainWindow::onLoadWorld() {
    QModelIndex idx = m_worldListView->currentIndex();
    if (!idx.isValid()) return;
    QString folder = m_worldListModel->data(idx, Qt::DisplayRole).toString();
    if (folder.isEmpty()) return;

    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QString worldPath = worldsDir + "/" + folder;
    if (!QDir(worldPath).exists()) return;

    // Сохраняем текущий мир
    saveCurrentWorld();

    // Загружаем выбранный
    loadWorld(worldPath);
    startEngine();

    // Обновляем настройки
    Settings::instance().lastWorld = folder;
    saveSettings();

    // Обновляем поле сида в overlay
    if (m_seedEdit) {
        m_seedEdit->setText(m_currentSeed);
        m_seedChanged = false;
    }
    hideWarningLabels();

    // Обновляем список и выделение
    updateWorldList();
}

void MainWindow::updateWorldList() {
    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QDir dir(worldsDir);
    QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    m_worldListModel->setStringList(dirs);

    // Выделяем текущий мир
    QString current = Settings::instance().lastWorld;
    if (!current.isEmpty()) {
        int row = dirs.indexOf(current);
        if (row >= 0) {
            m_worldListView->setCurrentIndex(m_worldListModel->index(row));
        }
    }
}

void MainWindow::hideWarningLabels() {
    if (m_warnDiff) m_warnDiff->setVisible(false);
    if (m_warnSeed) m_warnSeed->setVisible(false);
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