#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <QThread>
#include <QLabel>
#include <QColor>
#include <QListView>
#include <QStringListModel>
#include <qpushbutton.h>

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

    // Настройки
    void onDifficultyTextChanged(const QString &text);
    void onSeedTextChanged(const QString &text);
    void onThemeChanged(const QString &theme);
    void onRegenerateWorld();
    void onLoadWorld();
    void onOverlayClose();

signals:
    void requestSave();

private:
    void initNewWorld(const QString &seed, bool fromOverlay = false);
    void loadWorld(const QString &worldPath);
    void startEngine();
    void stopEngine();
    void saveSettings();
    void setupOverlay();
    void applyTheme();                     // применяет QSS и рамки
    void loadThemeColors();                // из последней строки спрайтлиста
    void updateWorldList();                // заполняет список миров
    void saveCurrentWorld();               // сохраняет текущий мир в файл
    void hideWarningLabels();              // скрывает все предупреждения

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
    QVector<QColor> m_themeColors;        // цвета из спрайтлиста
    CounterWidget *m_scoreWidget = nullptr;
    CounterWidget *m_timeWidget = nullptr;
    QFrame *m_scoreFrame = nullptr;
    QFrame *m_timeFrame = nullptr;

    QLabel *m_coordLabel = nullptr;

    // Overlay elements
    QFrame *m_overlay = nullptr;
    QLineEdit *m_diffEdit = nullptr;
    QLineEdit *m_seedEdit = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QLabel *m_warnDiff = nullptr;
    QLabel *m_warnSeed = nullptr;
    QListView *m_worldListView = nullptr;
    QStringListModel *m_worldListModel = nullptr;
    QPushButton *m_loadWorldBtn = nullptr;
    QPushButton *m_regenerateBtn = nullptr;

    bool m_diffChanged = false;
    bool m_seedChanged = false;
};

#endif // MAINWINDOW_H