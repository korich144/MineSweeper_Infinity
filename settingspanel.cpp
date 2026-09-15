#include "settingspanel.h"
#include "ui_settingspanel.h"

#include <QLineEdit>
#include <QComboBox>
#include <QListView>
#include <QPushButton>
#include <QLabel>
#include <QStringListModel>
#include <QResizeEvent>
#include <QGridLayout>

SettingsPanel::SettingsPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPanel)
{
    ui->setupUi(this);

    // Без этого атрибута обычный QWidget не рисует фон из таблицы стилей
    // (в отличие от QFrame), и панель осталась бы прозрачной.
    setAttribute(Qt::WA_StyledBackground, true);

    m_worldListModel = new QStringListModel(this);
    ui->worldListView->setModel(m_worldListModel);

    connect(ui->closeButton, &QPushButton::clicked, this, &SettingsPanel::closeRequested);

    // Порог переключения в "стопку" считаем по естественным размерам колонок,
    // пока они ещё стоят рядом (как задано в .ui). Некоторое сжатие колонок
    // допускаем, иначе режим рядом выключался бы от малейшего изменения размера.
    constexpr double SIDE_BY_SIDE_TOLERANCE = 0.85;
    constexpr int    LAYOUT_MARGIN = 24;
    const int natural = ui->leftColumn->sizeHint().width() + ui->rightColumn->sizeHint().width();
    m_stackThreshold = qBound(440, qRound(natural * SIDE_BY_SIDE_TOLERANCE) + LAYOUT_MARGIN, 900);
}

SettingsPanel::~SettingsPanel() {
    delete ui;
}

QLineEdit *SettingsPanel::difficultyEdit() const { return ui->difficultyEdit; }
QLineEdit *SettingsPanel::seedEdit() const { return ui->seedEdit; }
QComboBox *SettingsPanel::themeCombo() const { return ui->themeCombo; }
QListView *SettingsPanel::worldListView() const { return ui->worldListView; }
QPushButton *SettingsPanel::loadWorldButton() const { return ui->loadWorldButton; }
QPushButton *SettingsPanel::regenerateButton() const { return ui->regenerateButton; }

void SettingsPanel::setDifficulty(int difficulty) {
    ui->difficultyEdit->setText(QString::number(difficulty));
}

void SettingsPanel::setSeed(const QString &seed) {
    ui->seedEdit->setText(seed);
}

void SettingsPanel::setDifficultyInfo(const QString &text) {
    ui->difficultyInfo->setText(text);
}

void SettingsPanel::setDifficultyWarning(const QString &text) {
    ui->difficultyWarning->setText(text);
    ui->difficultyWarning->setVisible(!text.isEmpty());
}

void SettingsPanel::setSeedWarning(const QString &text) {
    ui->seedWarning->setText(text);
    ui->seedWarning->setVisible(!text.isEmpty());
}

void SettingsPanel::clearWarnings() {
    setDifficultyWarning(QString());
    setSeedWarning(QString());
}

void SettingsPanel::setWorldList(const QStringList &names, const QString &current) {
    m_worldListModel->setStringList(names);
    const int row = names.indexOf(current);
    if (row >= 0)
        ui->worldListView->setCurrentIndex(m_worldListModel->index(row));
}

QString SettingsPanel::selectedWorld() const {
    const QModelIndex idx = ui->worldListView->currentIndex();
    if (!idx.isValid()) return QString();
    return m_worldListModel->data(idx, Qt::DisplayRole).toString();
}

void SettingsPanel::setCloseIcon(const QIcon &icon) {
    ui->closeButton->setIcon(icon);
    ui->closeButton->setIconSize(QSize(16, 14));
}

void SettingsPanel::applyTheme(const ThemePalette &palette) {
    // Все цвета берутся из палитры темы: собственных стилей у элементов нет,
    // поэтому таблица стилей панели полностью определяет внешний вид.
    setStyleSheet(buildStyleSheet(palette));
}

QString SettingsPanel::buildStyleSheet(const ThemePalette &p) const {
    auto hex = [](const QColor &c) { return c.name(QColor::HexRgb); };

    // Объёмные рамки: выпуклая у кнопок, утопленная у полей ввода.
    const QString bevelUp = QString("border-top: 2px solid %1; border-left: 2px solid %1; "
                                    "border-bottom: 2px solid %2; border-right: 2px solid %2;")
                                .arg(hex(p.highlight), hex(p.shadow));
    const QString bevelDown = QString("border-top: 2px solid %1; border-left: 2px solid %1; "
                                      "border-bottom: 2px solid %2; border-right: 2px solid %2;")
                                  .arg(hex(p.shadow), hex(p.highlight));

    QString css;
    css += QString("QWidget#SettingsPanel { background-color: %1; }").arg(hex(p.panelBg));
    css += "QScrollArea, QWidget#scrollAreaWidgetContents, QFrame#leftColumn, QFrame#rightColumn "
           "{ background: transparent; border: none; }";

    css += QString("QLabel { color: %1; background: transparent; border: none; }").arg(hex(p.text));
    css += QString("QLabel#titleLabel { color: %1; font-weight: bold; }").arg(hex(p.titleText()));
    css += QString("QLabel#difficultyInfo { color: %1; font-size: 8pt; }").arg(hex(p.mutedText));
    css += QString("QLabel#difficultyWarning, QLabel#seedWarning { color: %1; font-size: 8pt; }")
               .arg(hex(p.warningText));

    css += QString("QLineEdit { background-color: %1; color: %2; %3 padding: 1px 3px; "
                   "selection-background-color: %4; selection-color: %5; }")
               .arg(hex(p.fieldBg), hex(p.text), bevelDown, hex(p.selectionBg), hex(p.selectionText));

    css += QString("QComboBox { background-color: %1; color: %2; %3 padding: 1px 3px; }")
               .arg(hex(p.fieldBg), hex(p.text), bevelDown);
    css += QString("QComboBox::drop-down { width: 16px; background-color: %1; border: none; "
                   "border-left: 1px solid %2; }").arg(hex(p.controlFace), hex(p.shadow));
    css += QString("QComboBox::down-arrow { width: 0; height: 0; border-left: 4px solid transparent; "
                   "border-right: 4px solid transparent; border-top: 5px solid %1; }").arg(hex(p.text));
    css += QString("QComboBox QAbstractItemView { background-color: %1; color: %2; "
                   "selection-background-color: %3; selection-color: %4; }")
               .arg(hex(p.fieldBg), hex(p.text), hex(p.selectionBg), hex(p.selectionText));

    css += QString("QListView { background-color: %1; color: %2; %3 }")
               .arg(hex(p.fieldBg), hex(p.text), bevelDown);
    css += QString("QListView::item { padding: 1px 2px; } "
                   "QListView::item:selected { background-color: %1; color: %2; }")
               .arg(hex(p.selectionBg), hex(p.selectionText));

    css += QString("QPushButton { background-color: %1; color: %2; %3 padding: 3px 8px; }")
               .arg(hex(p.controlFace), hex(p.text), bevelUp);
    css += QString("QPushButton:pressed { %1 padding: 4px 7px 2px 9px; }").arg(bevelDown);
    css += QString("QPushButton:disabled { color: %1; }").arg(hex(p.mutedText));
    // Кнопка закрытия рисуется спрайтами поверх градиента заголовка.
    css += "QPushButton#closeButton { background: transparent; border: none; padding: 0; }";

    css += QString("QScrollBar:vertical { background: %1; width: 14px; margin: 0; }").arg(hex(p.fieldBg));
    css += QString("QScrollBar:horizontal { background: %1; height: 14px; margin: 0; }").arg(hex(p.fieldBg));
    css += QString("QScrollBar::handle { background: %1; border-top: 1px solid %2; border-left: 1px solid %2; "
                   "border-bottom: 1px solid %3; border-right: 1px solid %3; }")
               .arg(hex(p.controlFace), hex(p.highlight), hex(p.shadow));
    css += "QScrollBar::handle:vertical { min-height: 24px; } "
           "QScrollBar::handle:horizontal { min-width: 24px; } "
           "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }";
    css += QString("QScrollBar::add-page, QScrollBar::sub-page { background: %1; }").arg(hex(p.fieldBg));

    css += QString("QFrame#titleFrame { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
                   "stop:0 %1, stop:1 %2); }").arg(hex(p.titleGradStart), hex(p.titleGradEnd));
    return css;
}

void SettingsPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateColumnsLayout();
}

void SettingsPanel::updateColumnsLayout() {
    const bool stacked = width() < m_stackThreshold;
    if (stacked == m_stacked) return;
    m_stacked = stacked;

    QGridLayout *grid = ui->columnsLayout;
    // Убираем элементы раскладки, сами виджеты остаются живы.
    while (QLayoutItem *item = grid->takeAt(0))
        delete item;

    if (stacked) {
        // В стопке лишнюю высоту забирает список миров, а настройки
        // прижимаются к верху без пустого зазора.
        grid->addWidget(ui->leftColumn, 0, 0);
        grid->addWidget(ui->rightColumn, 1, 0);
        grid->setRowStretch(0, 0);
        grid->setRowStretch(1, 1);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 0);
    } else {
        grid->addWidget(ui->leftColumn, 0, 0);
        grid->addWidget(ui->rightColumn, 0, 1);
        grid->setRowStretch(0, 1);
        grid->setRowStretch(1, 0);
        grid->setColumnStretch(0, 2);
        grid->setColumnStretch(1, 1);
    }
}
