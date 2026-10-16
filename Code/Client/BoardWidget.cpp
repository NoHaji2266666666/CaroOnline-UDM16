#include "BoardWidget.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

BoardWidget::BoardWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(380, 380);
    resetBoard();
    setCursor(Qt::PointingHandCursor);
}
void BoardWidget::resetBoard() {
    board_ = QStringList();
    for (int i = 0; i < 15; ++i) board_ << QString(15, QLatin1Char('.'));
    lastRow_ = lastCol_ = -1;
    update();
}
void BoardWidget::setBoard(const QStringList &board, int lastRow, int lastCol) {
    if (board.size() != 15) return;
    for (const QString &line : board) if (line.size() != 15) return;
    board_ = board; lastRow_ = lastRow; lastCol_ = lastCol;
    update();
}
void BoardWidget::geometryOfBoard(int &x, int &y, int &cell) const {
    cell = std::max(12, std::min((width()-24)/15, (height()-24)/15));
    x = (width() - cell*15)/2;
    y = (height() - cell*15)/2;
}
void BoardWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(239, 244, 249));
    int bx, by, tile; geometryOfBoard(bx, by, tile);
    const QRect boardRect(bx, by, tile*15, tile*15);
    painter.setBrush(QColor(247, 222, 173));
    painter.setPen(QPen(QColor(180, 142, 87), 2));
    painter.drawRoundedRect(boardRect, 6, 6);
    painter.setPen(QPen(QColor(149, 115, 73), 1));
    for (int i = 1; i < 15; ++i) {
        painter.drawLine(bx+i*tile, by, bx+i*tile, by+15*tile);
        painter.drawLine(bx, by+i*tile, bx+15*tile, by+i*tile);
    }
    if (lastRow_ >= 0 && lastRow_ < 15 && lastCol_ >= 0 && lastCol_ < 15)
        painter.fillRect(bx + lastCol_*tile+2, by + lastRow_*tile+2,
                         tile-4, tile-4, QColor(250, 207, 88, 135));
    for (int r = 0; r < 15; ++r) for (int c = 0; c < 15; ++c) {
        const QChar piece = board_.at(r).at(c);
        if (piece != QLatin1Char('X') && piece != QLatin1Char('O')) continue;
        const QRectF cellRect(bx+c*tile+tile*0.15, by+r*tile+tile*0.15,
                              tile*0.7, tile*0.7);
        if (piece == QLatin1Char('X')) {
            painter.setPen(QPen(QColor(31, 72, 139), std::max(3, tile/10), Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(cellRect.topLeft(), cellRect.bottomRight());
            painter.drawLine(cellRect.topRight(), cellRect.bottomLeft());
        } else {
            painter.setPen(QPen(QColor(178, 51, 67), std::max(3, tile/10)));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(cellRect);
        }
    }
}
void BoardWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    int x, y, cell; geometryOfBoard(x, y, cell);
    const QPointF point = event->position();
    const int col = int((point.x()-x)/cell), row = int((point.y()-y)/cell);
    if (point.x() < x || point.y() < y || col < 0 || col >= 15 || row < 0 || row >= 15) return;
    emit cellClicked(row, col);
}
