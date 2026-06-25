#include "counterwidget.h"
#include <QPainter>

CounterWidget::CounterWidget(int numDigits, const QVector<QPixmap> &digitSprites, QWidget *parent)
    : QWidget(parent), m_numDigits(numDigits), m_digits(digitSprites)
{
    setFixedSize(numDigits * 26, 46);
    m_value = QString(numDigits, ' ');
}

void CounterWidget::setValue(const QString &text) {
    if (text.size() == m_numDigits) {
        m_value = text;
        update();
    }
}

void CounterWidget::setSprite(const QVector<QPixmap> &sprites) {
    m_digits = sprites;
    update();
}

void CounterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (m_digits.size() < 14) return;

    for (int i = 0; i < m_numDigits; ++i) {
        QChar ch = m_value.at(i);
        int idx;
        if (ch == ':') idx = 0;
        else if (ch == ';') idx = 1;
        else if (ch == '-') idx = 2;
        else if (ch == ' ') idx = 3;
        else if (ch >= '0' && ch <= '9') idx = 13 - (ch.unicode() - '0');
        else idx = 3;

        QRectF target(i * 26, 0, 26, 46);
        p.drawPixmap(target, m_digits.at(idx), QRectF(0,0,13,23));
    }
}