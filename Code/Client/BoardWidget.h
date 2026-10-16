#pragma once
#include <QWidget>
#include <QStringList>

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget *parent = nullptr);
    void setBoard(const QStringList &board, int lastRow = -1, int lastCol = -1);
    void resetBoard();
    QSize sizeHint() const override { return QSize(535, 535); }

signals:
    void cellClicked(int row, int col);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QStringList board_;
    int lastRow_ = -1, lastCol_ = -1;
    void geometryOfBoard(int &x, int &y, int &cell) const;
};
