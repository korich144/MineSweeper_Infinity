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
    QString theme = Settings::instance().theme;
    QString themePath = QString(":/themes/%1.bmp").arg(theme);
    QPixmap sheet(themePath);
    if (sheet.isNull()) sheet.load("sprites/" + theme + ".bmp");
    if (!sheet.isNull()) {
        m_digitSprites.clear();
        for (int i = 0; i < 14; ++i)
            m_digitSprites.append(sheet.copy(i * 13, 16, 13, 23));

        m_smileSprites.clear();
        for (int i = 0; i < 5; ++i) {
            QPixmap sp = sheet.copy(i * 24, 39, 24, 24);
            QPixmap scaled = sp.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation);
            m_smileSprites.append(scaled);
        }
        ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
        ui->smileButton->setIconSize(QSize(48, 48));
    }
    m_field->setTheme(theme);

    m_scoreWidget = new CounterWidget(6, m_digitSprites, ui->frameHeader);
    m_timeWidget = new CounterWidget(8, m_digitSprites, ui->frameHeader);
    ui->frameHeader->layout()->replaceWidget(ui->scoreLabel, m_scoreWidget);
    ui->frameHeader->layout()->replaceWidget(ui->timeLabel, m_timeWidget);
    delete ui->scoreLabel;
    delete ui->timeLabel;
    m_scoreWidget->setValue("      ");
    m_timeWidget->setValue("00:00:00");

    QTimer *uiTimer = new QTimer(this);
    connect(uiTimer, &QTimer::timeout, this, &MainWindow::updateCounters);
    uiTimer->start(33);
}

MainWindow::~MainWindow() {
    stopEngine();
    delete ui;
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

    m_field->setOffset(0, 0);
    m_field->centerOn(info.lastViewX, info.lastViewY);
    m_field->setScale(info.lastScale);

    m_gameStarted = info.gameActive;
    m_elapsedOffset = info.elapsed;
    m_score = info.score;
    if (m_gameStarted)
        m_gameTimer.start();
    else
        m_gameTimer.invalidate();
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

    // время
    QString timeStr;
    if (m_gameStarted && m_wm->isGameActive()) {
        qint64 ms = m_elapsedOffset + m_gameTimer.elapsed();
        int h = ms / 3600000;
        int m = (ms % 3600000) / 60000;
        int s = (ms % 60000) / 1000;
        timeStr = QString("%1:%2:%3").arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    } else {
        qint64 ms = m_elapsedOffset + (m_gameTimer.isValid() ? m_gameTimer.elapsed() : 0);
        int h = ms / 3600000;
        int m = (ms % 3600000) / 60000;
        int s = (ms % 60000) / 1000;
        timeStr = QString("%1:%2:%3").arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    }
    m_timeWidget->setValue(timeStr);

    // очки
    int opened, flagged;
    m_wm->countStats(opened, flagged);
    static int lastOpened = -1;
    if (lastOpened != -1 && opened > lastOpened) {
        int diff = m_wm->difficulty();
        int delta = (diff > 0) ? ((opened - lastOpened) * diff / 10) : 0;
        m_score += delta;
        if (m_score > 999999) m_score = 999999;
    }
    lastOpened = opened;
    QString scoreStr = QString::number(m_score).rightJustified(6, ' ');
    m_scoreWidget->setValue(scoreStr);

    QString status = QString("Сложность: %1 | Флажков: %2 | Открыто: %3")
                         .arg(m_wm->difficulty()).arg(flagged).arg(opened);
    statusBar()->showMessage(status);

    if (m_smileSprites.size() >= 5) {
        if (!m_wm->isGameActive())
            ui->smileButton->setIcon(QIcon(m_smileSprites[2]));
        else
            ui->smileButton->setIcon(QIcon(m_smileSprites[4]));
    }

    m_field->update();   // принудительная перерисовка поля
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
    m_restartAfterSave = true;
    emit requestSave();
}

void MainWindow::onSaveFinished() {
    if (m_restartAfterSave) {
        m_restartAfterSave = false;
        stopEngine();
        if (m_wm) {
            m_wm->deleteLater();
            m_wm = nullptr;
        }
        initNewWorld(World::generateSeed(), true);
        startEngine();
    }
}

void MainWindow::showSaveIndicator() {
    statusBar()->showMessage("Сохранение...", 1000);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_wm && !m_currentWorldDir.isEmpty()) {
        WorldInfo info = World::load(m_currentWorldDir);
        QPointF center = m_field->screenToCell(rect().center());
        info.lastViewX = static_cast<qint64>(qFloor(center.x()));
        info.lastViewY = static_cast<qint64>(qFloor(center.y()));
        info.lastScale = m_field->scale();
        info.score = m_score;
        info.elapsed = m_elapsedOffset + (m_gameTimer.isValid() ? m_gameTimer.elapsed() : 0);
        info.gameActive = m_wm->isGameActive() && m_gameStarted;
        World::save(m_currentWorldDir, info);
    }

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
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::setupOverlay() {
    m_overlay = new QFrame(this);   // родитель – главное окно
    m_overlay->setStyleSheet("background-color: rgba(0,0,0,150);");
    m_overlay->setGeometry(rect());
    m_overlay->show();
    m_overlay->raise();

    QFrame *frame = new QFrame(m_overlay);
    frame->setStyleSheet("background: white; border: 2px solid gray;");
    frame->setFixedSize(400, 320);
    frame->move((width() - 400) / 2, (height() - 320) / 2);
    frame->show();
    frame->raise();

    QVBoxLayout *vlay = new QVBoxLayout(frame);

    vlay->addWidget(new QLabel("Сложность (частота):", frame));
    QLineEdit *diffEdit = new QLineEdit(QString::number(Settings::instance().difficulty), frame);
    vlay->addWidget(diffEdit);
    QLabel *diffHint = new QLabel("Изменения вступят в силу только после перезапуска.", frame);
    diffHint->setStyleSheet("color: red;");
    vlay->addWidget(diffHint);

    vlay->addWidget(new QLabel("Сид текущего мира:", frame));
    QLineEdit *seedEdit = new QLineEdit(m_currentSeed, frame);
    vlay->addWidget(seedEdit);
    QLabel *seedHint = new QLabel("Сид будет использован при следующем создании мира.", frame);
    vlay->addWidget(seedHint);

    vlay->addWidget(new QLabel("Тема:", frame));
    QComboBox *themeCombo = new QComboBox(frame);
    themeCombo->addItem("light");
    themeCombo->addItem("dark");
    themeCombo->setCurrentText(Settings::instance().theme);
    vlay->addWidget(themeCombo);

    QHBoxLayout *worldLay = new QHBoxLayout();
    QLineEdit *worldEdit = new QLineEdit(Settings::instance().lastWorld, frame);
    worldEdit->setReadOnly(true);
    QPushButton *browseBtn = new QPushButton("Обзор...", frame);
    worldLay->addWidget(worldEdit);
    worldLay->addWidget(browseBtn);
    vlay->addLayout(worldLay);

    QPushButton *closeBtn = new QPushButton("Закрыть", frame);
    vlay->addWidget(closeBtn);

    connect(browseBtn, &QPushButton::clicked, [this, worldEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Выберите мир",
                                                        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds");
        if (!dir.isEmpty()) {
            worldEdit->setText(QDir(dir).dirName());
        }
    });

    connect(closeBtn, &QPushButton::clicked, [this, diffEdit, seedEdit, themeCombo, worldEdit]() {
        Settings::instance().difficulty = diffEdit->text().toInt();
        Settings::instance().theme = themeCombo->currentText();
        m_currentSeed = seedEdit->text();
        QString newWorld = worldEdit->text().trimmed();
        if (!newWorld.isEmpty() && newWorld != Settings::instance().lastWorld) {
            Settings::instance().lastWorld = newWorld;
            saveSettings();
        }
        m_overlay->deleteLater();
        m_overlay = nullptr;
    });
}