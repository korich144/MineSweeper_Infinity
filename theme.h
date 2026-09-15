#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QVector>

class QPixmap;

// Палитра интерфейса, читается из последней строки спрайтлиста темы.
// Слоты строки палитры:
//
//    0 — фон окна и панелей              8 — начало градиента заголовка
//    1 — фон кнопок                      9 — конец градиента заголовка
//    2 — основной текст                 10 — фон выделения (списки, поля)
//    3 — (свободно)                     11 — текст выделения
//    4 — светлая грань (бевел)          12 — текст предупреждений
//    5 — тёмная грань (бевел)           13 — приглушённый текст
//    6 — фон полей ввода и списков      14, 15 — (свободно)
//    7 — (свободно)
//
// Незаполненный слот помечен в спрайтлисте ярко-зелёным маркером (#10FF00
// в светлой теме, #0DCE00 в тёмной) — тогда цвет подбирается автоматически
// из остальных цветов палитры.
struct ThemePalette {
    QColor panelBg;          // 0
    QColor controlFace;      // 1
    QColor text;             // 2
    QColor highlight;        // 4
    QColor shadow;           // 5
    QColor fieldBg;          // 6
    QColor titleGradStart;   // 8
    QColor titleGradEnd;     // 9
    QColor selectionBg;      // 10
    QColor selectionText;    // 11
    QColor warningText;      // 12
    QColor mutedText;        // 13

    bool valid = false;      // палитра действительно прочитана из спрайтлиста

    // Читает палитру из последней строки спрайтлиста темы.
    static ThemePalette fromSheet(const QPixmap &sheet);
    static ThemePalette fromColors(const QVector<QColor> &colors);
    // Сколько цветов читается из строки палитры.
    static QVector<QColor> readPaletteRow(const QPixmap &sheet);
    static constexpr int SLOT_COUNT = 16;

    // Цвет текста заголовка поверх градиента.
    QColor titleText() const;
};

#endif // THEME_H