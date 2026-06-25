#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <QThread>

class WorldManager;
class Solver;
class Loader;
class Saver;
class MinefieldWidget;
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

signals:
    void requestSave();

private:
    void initNewWorld(const QString &seed);
    void loadWorld(const QString &worldPath);
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
    bool m_gameStarted = false;
    bool m_restartAfterSave = false;

    // sprites
    QVector<QPixmap> m_digitSprites;   // 14 items
    QVector<QPixmap> m_smileSprites;   // 5 items

    // overlay
    QFrame *m_overlay = nullptr;
};

#endif // MAINWINDOW_H