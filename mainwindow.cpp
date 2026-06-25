#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "minefieldwidget.h"
#include "worldmanager.h"
#include "solver.h"
#include "loader.h"
#include "saver.h"
#include "settings.h"
#include "world.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QKeyEvent>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // field widget
    m_field = new MinefieldWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(ui->frameField);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(m_field);

    QString settingsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    Settings::instance().load(settingsPath);

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
            // search by originalName
            QStringList dirs = QDir(worldsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &d : dirs) {
                WorldInfo info = World::load(worldsDir + "/" + d);
                if (info.originalName == lastWorld) {
                    Settings::instance().lastWorld = d;
                    info.originalName = d;   // update both to current name
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

    // threads
    m_solverThread = new QThread(this);
    m_solver = new Solver(m_wm);
    m_solver->moveToThread(m_solverThread);
    m_solverThread->start();

    m_loaderThread = new QThread(this);
    m_loader = new Loader(m_wm, m_solver, m_currentWorldDir);
    m_loader->moveToThread(m_loaderThread);
    m_loaderThread->start();

    m_saverThread = new QThread(this);
    m_saver = new Saver(m_wm, m_currentWorldDir);
    m_saver->moveToThread(m_saverThread);
    m_saverThread->start();

    // connections
    connect(m_field, &MinefieldWidget::startReveal, m_solver, &Solver::startReveal, Qt::QueuedConnection);
    connect(m_field, &MinefieldWidget::firstClick, this, &MainWindow::onFirstClick);
    connect(m_saver, &Saver::showSaveIndicator, this, &MainWindow::showSaveIndicator, Qt::QueuedConnection);
    connect(ui->smileButton, &QPushButton::clicked, this, &MainWindow::onSmileClicked);

    // UI timer 30 fps
    QTimer *uiTimer = new QTimer(this);
    connect(uiTimer, &QTimer::timeout, this, &MainWindow::updateCounters);
    uiTimer->start(33);

    // theme sprites
    QString theme = Settings::instance().theme;
    QString themePath = QString(":/themes/%1.bmp").arg(theme);
    QPixmap sheet(themePath);
    if (!sheet.isNull()) {
        m_digitSprites.clear();
        for (int i = 0; i < 14; ++i)
            m_digitSprites.append(sheet.copy(i * 13, 16, 13, 23));
        m_smileSprites.clear();
        for (int i = 0; i < 5; ++i)
            m_smileSprites.append(sheet.copy(i * 24, 39, 24, 24));
        ui->smileButton->setIcon(QIcon(m_smileSprites[4])); // normal
        ui->smileButton->setIconSize(QSize(24,24));
    }
    m_field->setTheme(theme);

    // overlay setup will be done on first Esc
    resize(Settings::instance().windowSize);
}

MainWindow::~MainWindow() {
    m_solverThread->quit();
    m_loaderThread->quit();
    m_saverThread->quit();
    m_solverThread->wait();
    m_loaderThread->wait();
    m_saverThread->wait();
    delete ui;
}

void MainWindow::initNewWorld(const QString &seed) {
    QString worldsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worlds";
    QString folderName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    QString worldPath = worldsDir + "/" + folderName;

    WorldInfo info;
    info.seed = seed;
    info.originalName = folderName;
    info.lastViewX = 0;
    info.lastViewY = 0;
    info.folderName = folderName;
    World::save(worldPath, info);

    Settings::instance().lastWorld = folderName;
    saveSettings();

    if (m_wm) {
        delete m_wm; // old worldmanager will be replaced
    }
    m_wm = new WorldManager(seed, Settings::instance().difficulty, this);
    m_field->setWorldManager(m_wm);
    m_currentWorldDir = worldPath;
    m_currentSeed = seed;
    m_gameStarted = false;
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
    if (m_wm) delete m_wm;
    m_wm = new WorldManager(info.seed, Settings::instance().difficulty, this);
    m_field->setWorldManager(m_wm);
    m_gameStarted = false;
    m_gameTimer.invalidate();
    // restore camera
    m_field->setOffset(0, 0);
    m_field->centerOn(info.lastViewX, info.lastViewY);
}

void MainWindow::saveSettings() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    Settings::instance().save(path);
}

void MainWindow::onFirstClick() {
    if (!m_gameStarted && m_wm->isGameActive()) {
        m_gameStarted = true;
        m_gameTimer.start();
    }
}

void MainWindow::updateCounters() {
    if (!m_wm) return;

    // time
    if (m_gameStarted && m_wm->isGameActive()) {
        qint64 ms = m_gameTimer.elapsed();
        int h = ms / 3600000;
        int m = (ms % 3600000) / 60000;
        int s = (ms % 60000) / 1000;
        QString timeStr = QString("%1:%2:%3").arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
        // we should draw with digit sprites; for simplicity just set text (in real code you'd use a custom widget)
        ui->timeLabel->setText(timeStr);
    } else {
        ui->timeLabel->setText("00:00:00");
    }

    // stats
    int opened = 0, flagged = 0;
    m_wm->countStats(opened, flagged);
    int diff = m_wm->difficulty();
    int score = (diff > 0) ? (opened * diff / 10) : 0;
    QString scoreStr = QString::number(score).rightJustified(8, ' ');
    ui->scoreLabel->setText(scoreStr);

    QString status = QString("Сложность: %1 | Флажков: %2 | Открыто: %3")
                         .arg(diff).arg(flagged).arg(opened);
    statusBar()->showMessage(status);

    // smile icon
    if (!m_wm->isGameActive()) {
        ui->smileButton->setIcon(QIcon(m_smileSprites[2])); // defeat
    } else {
        ui->smileButton->setIcon(QIcon(m_smileSprites[4])); // normal
    }
}

void MainWindow::onSmileClicked() {
    // save current world, start new with random seed
    if (m_saver) m_saver->saveNow();
    initNewWorld(World::generateSeed());
}

void MainWindow::showSaveIndicator() {
    statusBar()->showMessage("Сохранение...", 1000);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // save camera position
    if (m_wm && m_currentWorldDir.length() > 0) {
        WorldInfo info = World::load(m_currentWorldDir);
        // compute center cell as camera offset
        QPointF topLeft = m_field->screenToCell(QPointF(0,0));
        info.lastViewX = static_cast<qint64>(qFloor(topLeft.x() + width() / (2 * 16 * m_field->scale())));
        info.lastViewY = static_cast<qint64>(qFloor(topLeft.y() + height() / (2 * 16 * m_field->scale())));
        World::save(m_currentWorldDir, info);
    }
    Settings::instance().windowSize = size();
    saveSettings();
    if (m_saver) m_saver->saveNow();
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
    m_overlay = new QFrame(this);
    m_overlay->setStyleSheet("background-color: rgba(0,0,0,150);");
    m_overlay->setGeometry(rect());
    m_overlay->show();

    QFrame *frame = new QFrame(m_overlay);
    frame->setStyleSheet("background: white; border: 2px solid gray;");
    frame->setFixedSize(400, 320);
    frame->move((width() - 400) / 2, (height() - 320) / 2);

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
        // apply settings (only theme and seed for next world, difficulty saved but not applied now)
        Settings::instance().difficulty = diffEdit->text().toInt();
        Settings::instance().theme = themeCombo->currentText();
        // store seed in memory for next world creation
        m_currentSeed = seedEdit->text();
        // change lastWorld if selected
        QString newWorld = worldEdit->text().trimmed();
        if (!newWorld.isEmpty() && newWorld != Settings::instance().lastWorld) {
            Settings::instance().lastWorld = newWorld;
            saveSettings();
        }
        // close overlay
        m_overlay->deleteLater();
        m_overlay = nullptr;
    });
}