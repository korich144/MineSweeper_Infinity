#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include <QWidget>
#include <QStringList>
#include "theme.h"

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsPanel; }
QT_END_NAMESPACE

class QLineEdit;
class QComboBox;
class QListView;
class QPushButton;
class QLabel;
class QStringListModel;

// Панель настроек из settingspanel.ui.
// При нехватке ширины колонки автоматически становятся друг над другом,
// а содержимое прокручивается (QScrollArea внутри .ui).
// Внешний вид целиком задаётся палитрой темы (см. theme.h).
class SettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget *parent = nullptr);
    ~SettingsPanel();

    QLineEdit *difficultyEdit() const;
    QLineEdit *seedEdit() const;
    QComboBox *themeCombo() const;
    QListView *worldListView() const;
    QPushButton *loadWorldButton() const;
    QPushButton *regenerateButton() const;

    void setDifficulty(int difficulty);
    void setSeed(const QString &seed);
    void setDifficultyInfo(const QString &text);
    void setDifficultyWarning(const QString &text);   // пустая строка — скрыть
    void setSeedWarning(const QString &text);         // пустая строка — скрыть
    void clearWarnings();

    void setWorldList(const QStringList &names, const QString &current);
    QString selectedWorld() const;

    void setCloseIcon(const QIcon &icon);
    // Применяет палитру темы ко всем элементам панели.
    void applyTheme(const ThemePalette &palette);

    bool isStacked() const { return m_stacked; }

signals:
    void closeRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateColumnsLayout();
    QString buildStyleSheet(const ThemePalette &palette) const;

    Ui::SettingsPanel *ui;
    QStringListModel *m_worldListModel = nullptr;
    int m_stackThreshold = 600;
    bool m_stacked = false;
};

#endif // SETTINGSPANEL_H
