#include "TitleBar.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QWindow>
#include <QStyle>

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(46 * 3, 44); // exactly wide enough for the 3 buttons -> makes top-right pinning exact
    setObjectName(QStringLiteral("TitleBar"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);

    m_minBtn = new QPushButton(this);
    m_minBtn->setObjectName(QStringLiteral("TitleBarMinBtn"));
    m_minBtn->setCursor(Qt::PointingHandCursor);
    m_minBtn->setFixedSize(46, 44);
    m_minBtn->setText(QStringLiteral("\u2212"));

    m_maxBtn = new QPushButton(this);
    m_maxBtn->setObjectName(QStringLiteral("TitleBarMaxBtn"));
    m_maxBtn->setCursor(Qt::PointingHandCursor);
    m_maxBtn->setFixedSize(46, 44);
    m_maxBtn->setText(QStringLiteral("\u25A1"));

    m_closeBtn = new QPushButton(this);
    m_closeBtn->setObjectName(QStringLiteral("TitleBarCloseBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFixedSize(46, 44);
    m_closeBtn->setText(QStringLiteral("\u2715"));

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    connect(m_minBtn, &QPushButton::clicked, this, &TitleBar::minimizeClicked);
    connect(m_maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &TitleBar::closeClicked);
}

void TitleBar::setDark(bool dark)
{
    setProperty("dark", dark);
    style()->unpolish(this);
    style()->polish(this);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        window()->move(event->globalPosition().toPoint() - m_dragStartPos);
    }
    QWidget::mouseMoveEvent(event);
}
