#ifndef COUNTERWIDGET_H
#define COUNTERWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QVector>

class CounterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CounterWidget(const QVector<QPixmap> &digitSprites, QWidget *parent = nullptr);

    void setValue(const QString &text);
    void setSprite(const QVector<QPixmap> &sprites);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateSize();

    QVector<QPixmap> m_digits;   // 14 items
    QString m_value;
    int m_numDigits = 0;
};

#endif // COUNTERWIDGET_H