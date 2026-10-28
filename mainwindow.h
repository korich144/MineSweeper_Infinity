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
#include "theme.h"

class WorldManager;
class Solver;
class Loader;
class Saver;
class MinefieldWidget;
class CounterWidget;
class SettingsPanel;
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
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateCounters();
    void onSmilePressed();
    void onSmileReleased();
    void onSmileClicked();
    void onFirstClick();
    void showSaveIndicator();
    void onSaveFinished();
    void onFlagToggled(int delta);
    void onMineExploded();
    void onMouseCellChanged(int64_t x, int64_t y);
    void restoreCamera();

    // Настройки
    void onDifficultyTextChanged(const QString &text);
    void onSeedTextChanged(const QString &text);
    void onThemeChanged(const QString &theme);
    void onRegenerateWorld();
    void onLoadWorld();
    void onOverlayClose();
    void toggleStatusBar();

signals:
    void requestSave();

private:
    void initNewWorld(const QString &seed, int difficulty);
    void loadWorld(const QString &worldPath);
    void startEngine();
    void stopEngine();
    void saveSettings();
    void setupOverlay();
    void applyTheme();                     // применяет палитру, QSS и спрайты
    void applyPanelTheme();                // передаёт палитру панели настроек
    void loadThemeSheet();                 // читает спрайтлист текущей темы
    void updateWorldList();                // заполняет список миров
    void saveCurrentWorld();               // сохраняет текущий мир в файл
    void interruptReveal();                // останавливает открытие клеток
    void applyPendingOpens();              // переносит очки за открытые клетки в счёт
    bool flushRegions();                   // синхронно пишет грязные регионы на диск
    void adjustWindowSizeForStatusBar(bool show);
    void scheduleCameraRestore();          // восстановление камеры после перезагрузки мира
    void applyDefaultWindowSize();         // первый запуск: окно ровно под 9x9 клеток
    void updateStartIllumination();        // подсветка центра, пока не открыта ни одна клетка
    int  panelDifficulty(int fallback) const;  // сложность из панели или запасное значение
    QIcon loadCloseIcon() const;

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
    // Новый мир открывается так, чтобы клетка (0,0) была ровно в углу поля:
    // клетки прилегают к рамке без половинок.
    bool m_restoreAlignsToOrigin = false;
    double m_restoreScale = 1.0;
    double m_restoreCellX = 0.0;
    double m_restoreCellY = 0.0;

    QVector<QPixmap> m_digitSprites;
    QVector<QPixmap> m_smileSprites;
    QPixmap m_themeSheet;                 // спрайтлист текущей темы
    ThemePalette m_palette;               // палитра интерфейса из спрайтлиста
    CounterWidget *m_scoreWidget = nullptr;
    CounterWidget *m_timeWidget = nullptr;
    QFrame *m_scoreFrame = nullptr;
    QFrame *m_timeFrame = nullptr;

    QLabel *m_coordLabel = nullptr;

    // Окно настроек: затемнение + панель из settingspanel.ui
    QFrame *m_overlay = nullptr;
    SettingsPanel *m_settingsPanel = nullptr;

    bool m_diffChanged = false;
    bool m_seedChanged = false;

    bool m_statusBarVisible = false;
};

#endif // MAINWINDOW_H