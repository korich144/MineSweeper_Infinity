#include "counterwidget.h"
#include <QPainter>

CounterWidget::CounterWidget(const QVector<QPixmap> &digitSprites, QWidget *parent)
    : QWidget(parent), m_digits(digitSprites)
{
    setValue(" ");
}

void CounterWidget::setValue(const QString &text) {
    if (m_value != text) {
        m_value = text;
        updateSize();
        update();
    }
}

void CounterWidget::setSprite(const QVector<QPixmap> &sprites) {
    m_digits = sprites;
    update();
}

void CounterWidget::updateSize() {
    int len = m_value.length();
    if (len < 1) len = 1;
    setFixedSize(len * 13, 23);
    m_numDigits = len;
}

void CounterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (m_digits.size() < 14) return;

    for (int i = 0; i < m_value.length(); ++i) {
        QChar ch = m_value.at(i);
        int idx;
        if (ch == ':') idx = 0;
        else if (ch == ';') idx = 1;
        else if (ch == '-') idx = 2;
        else if (ch == ' ') idx = 3;
        else if (ch >= '0' && ch <= '9') idx = 13 - (ch.unicode() - '0');
        else idx = 3;

        QRectF target(i * 13, 0, 13, 23);
        p.drawPixmap(target, m_digits.at(idx), QRectF(0,0,13,23));
    }
}