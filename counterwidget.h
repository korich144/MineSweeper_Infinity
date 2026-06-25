#ifndef COUNTERWIDGET_H
#define COUNTERWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QVector>

class CounterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CounterWidget(int numDigits, const QVector<QPixmap> &digitSprites,
                           QWidget *parent = nullptr);

    void setValue(const QString &text);
    void setSprite(const QVector<QPixmap> &sprites);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_numDigits;
    QVector<QPixmap> m_digits;   // 14 items
    QString m_value;
};

#endif // COUNTERWIDGET_H