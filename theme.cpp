#include "theme.h"

#include <QPixmap>
#include <QImage>
#include <optional>

namespace {

// Маркер «слот не заполнен» — насыщенный зелёный, других таких цветов
// в строке палитры не бывает.
bool isPlaceholder(const QColor &c) {
    return c.green() > 190 && c.red() < 32 && c.blue() < 32;
}

} // namespace

QVector<QColor> ThemePalette::readPaletteRow(const QPixmap &sheet) {
    QVector<QColor> colors;
    if (sheet.isNull()) return colors;

    const QImage img = sheet.toImage();
    const int y = img.height() - 1;
    if (y < 0) return colors;

    const int count = qMin(SLOT_COUNT, img.width());
    colors.reserve(count);
    for (int x = 0; x < count; ++x)
        colors.append(img.pixelColor(x, y));
    return colors;
}

ThemePalette ThemePalette::fromSheet(const QPixmap &sheet) {
    return fromColors(readPaletteRow(sheet));
}

ThemePalette ThemePalette::fromColors(const QVector<QColor> &colors) {
    ThemePalette p;

    // Возвращает цвет слота, если он задан в спрайтлисте.
    auto slot = [&colors](int index) -> std::optional<QColor> {
        if (index < 0 || index >= colors.size()) return std::nullopt;
        const QColor c = colors.at(index);
        if (!c.isValid() || isPlaceholder(c)) return std::nullopt;
        return c;
    };

    p.panelBg = slot(0).value_or(QColor(0xC0, 0xC0, 0xC0));
    // От светлоты фона зависит, в какую сторону подбирать остальные цвета.
    const bool lightPanel = p.panelBg.lightness() > 140;

    p.controlFace    = slot(1).value_or(p.panelBg);
    p.text           = slot(2).value_or(lightPanel ? QColor(Qt::black) : QColor(Qt::white));
    p.highlight      = slot(4).value_or(lightPanel ? QColor(Qt::white) : p.panelBg.lighter(170));
    p.shadow         = slot(5).value_or(lightPanel ? QColor(0x80, 0x80, 0x80) : p.panelBg.darker(120));
    p.fieldBg        = slot(6).value_or(p.panelBg);
    p.titleGradStart = slot(8).value_or(lightPanel ? QColor(0x08, 0x1E, 0x71) : p.panelBg.darker(150));
    p.titleGradEnd   = slot(9).value_or(p.titleGradStart.lighter(170));
    p.selectionBg    = slot(10).value_or(lightPanel ? QColor(0x00, 0x00, 0x80) : QColor(0x56, 0x69, 0xAB));
    p.selectionText  = slot(11).value_or(p.selectionBg.lightness() > 140 ? QColor(Qt::black) : QColor(Qt::white));
    p.warningText    = slot(12).value_or(lightPanel ? QColor(0xC0, 0x00, 0x00) : QColor(0xFF, 0x80, 0x80));
    p.mutedText      = slot(13).value_or(lightPanel ? p.panelBg.darker(150) : p.panelBg.lighter(150));
    // По умолчанию туман — нарочно заметный зелёный: пока слот не заполнен
    // в спрайтлисте, его так видно при отладке.
    p.fog            = slot(14).value_or(QColor(0x00, 0xA0, 0x00));

    p.valid = !colors.isEmpty();
    return p;
}

QColor ThemePalette::titleText() const {
    return titleGradStart.lightness() > 140 ? QColor(Qt::black) : QColor(Qt::white);
}