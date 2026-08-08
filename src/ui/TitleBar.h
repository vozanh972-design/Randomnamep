#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QPoint;

// Minimal custom title bar: draggable area + minimize/maximize/close.
// Used by both ActivationWindow and MainWindow so the frameless chrome
// stays visually consistent across the app.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setDark(bool dark); // switches icon color for light vs dark hosts

signals:
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QPushButton *m_minBtn;
    QPushButton *m_maxBtn;
    QPushButton *m_closeBtn;
    QPoint m_dragStartPos;
    bool m_dragging = false;
};
