#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <QThread>
#include <QLabel>

class WorldManager;
class Solver;
class Loader;
class Saver;
class MinefieldWidget;
class CounterWidget;
class QFrame;
class QLineEdit;
class QComboBox;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void updateCounters();
    void onSmilePressed();
    void onSmileReleased();
    void onSmileClicked();
    void onFirstClick();
    void showSaveIndicator();
    void onSaveFinished();
    void onCellsOpened(int count);
    void onFlagToggled(int delta);
    void onMouseCellChanged(int64_t x, int64_t y);
    void restoreCamera();

signals:
    void requestSave();

private:
    void initNewWorld(const QString &seed, bool fromOverlay = false);
    void loadWorld(const QString &worldPath);
    void startEngine();
    void stopEngine();
    void saveSettings();
    void setupOverlay();

    Ui::MainWindow *ui;
    MinefieldWidget *m_field;

    WorldManager *m_wm = nullptr;
    Solver *m_solver = nullptr;
    Loader *m_loader = nullptr;
    Saver *m_saver = nullptr;

    QThread *m_solverThread = nullptr;
    QThread *m_loaderThread = nullptr;
    QThread *m_saverThread = nullptr;

    QString m_currentWorldDir;
    QString m_currentSeed;
    QElapsedTimer m_gameTimer;
    qint64 m_elapsedOffset = 0;
    qint64 m_score = 0;
    int m_flagsPlaced = 0;
    bool m_gameStarted = false;
    bool m_gamePaused = false;
    bool m_restartAfterSave = false;

    bool m_cameraRestored = false;
    double m_restoreScale = 1.0;
    int64_t m_restoreCellX = 0;
    int64_t m_restoreCellY = 0;

    QVector<QPixmap> m_digitSprites;
    QVector<QPixmap> m_smileSprites;
    CounterWidget *m_scoreWidget = nullptr;
    CounterWidget *m_timeWidget = nullptr;

    QLabel *m_coordLabel = nullptr;

    QFrame *m_overlay = nullptr;
};

#endif // MAINWINDOW_H