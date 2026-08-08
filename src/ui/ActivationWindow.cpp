#include "ActivationWindow.h"
#include "TitleBar.h"
#include "../services/AppConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QSvgWidget>
#include <QDesktopServices>
#include <QUrl>
#include <QColor>
#include <QResizeEvent>
#include <QGraphicsEffect>
#include <QIcon>
#include <QStyle>

namespace
{
QLabel *makeIcon(const QString &path, int size)
{
    auto *label = new QLabel;
    label->setPixmap(QIcon(path).pixmap(size, size));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    return label;
}
}

ActivationWindow::ActivationWindow(LicenseService *licenseService, QWidget *parent)
    : QWidget(parent)
    , m_licenseService(licenseService)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1440, 900);
    setMinimumSize(1100, 720);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(0);

    m_root = new QFrame(this);
    QFrame *root = m_root;
    root->setObjectName(QStringLiteral("ActivationRoot"));
    auto *shadow = new QGraphicsDropShadowEffect(root);
    shadow->setBlurRadius(48);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(17, 24, 39, 60));
    root->setGraphicsEffect(shadow);

    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_titleBar = new TitleBar(root);
    connect(m_titleBar, &TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::closeClicked, this, &QWidget::close);
    connect(m_titleBar, &TitleBar::maximizeClicked, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            // Frameless windows need the "card" margins/shadow dropped so
            // maximizing actually fills the whole screen instead of
            // leaving the 24px card gap around the edges.
            showMaximized();
        }
    });

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(buildLeftPanel(), 30);
    body->addWidget(buildRightPanel(), 70);

    // Title bar overlays top-right of the right panel (matches reference: it
    // sits at the very top, above the right panel's white background).
    rootLayout->addLayout(body);
    m_titleBar->setParent(root);
    m_titleBar->raise();
    m_titleBar->move(root->width() - m_titleBar->width(), 0);

    outer->addWidget(root);

    connect(m_licenseService, &LicenseService::activationStarted, this, [this]() {
        setLoadingState(true);
    });
    connect(m_licenseService, &LicenseService::activationSucceeded, this, [this](const LicenseInfo &info) {
        setLoadingState(false);
        showInlineSuccess(QStringLiteral("Kích hoạt thành công"));
        emit activationCompleted(info);
    });
    connect(m_licenseService, &LicenseService::activationFailed, this, [this](const QString &msg) {
        setLoadingState(false);
        showInlineError(msg);
    });
}

void ActivationWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // Root frame carries its own rounded background via QSS; nothing to
    // paint at this level beyond the default (keeps translucent margins).
}

void ActivationWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // Keep the min/max/close cluster pinned to the true top-right corner on
    // every resize (window drag-resize, maximize, restore, DPI changes) --
    // a one-shot move() at construction time goes stale the moment the
    // window size changes.
    if (m_titleBar && m_root) {
        m_titleBar->move(m_root->width() - m_titleBar->width(), 0);
    }

    // When maximized, drop the floating-card look (outer margin + shadow)
    // so the window actually fills the entire screen edge-to-edge instead
    // of leaving a visible gap around a "card" that never grew past its
    // windowed size.
    if (auto *outer = qobject_cast<QVBoxLayout *>(layout())) {
        const bool maximized = isMaximized();
        outer->setContentsMargins(maximized ? 0 : 24, maximized ? 0 : 24,
                                   maximized ? 0 : 24, maximized ? 0 : 24);
    }
    if (m_root) {
        if (auto *shadow = m_root->graphicsEffect()) {
            shadow->setEnabled(!isMaximized());
        }
    }
}

QWidget *ActivationWindow::buildFeatureRow(const QString &iconPath, const QString &title, const QString &desc)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *iconWrap = new QFrame;
    iconWrap->setObjectName(QStringLiteral("FeatureIconWrap"));
    iconWrap->setFixedSize(40, 40);
    auto *iconLayout = new QVBoxLayout(iconWrap);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    iconLayout->setAlignment(Qt::AlignCenter);
    iconLayout->addWidget(makeIcon(iconPath, 20));

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);

    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("FeatureTitle"));

    auto *descLabel = new QLabel(desc);
    descLabel->setObjectName(QStringLiteral("FeatureDesc"));
    descLabel->setWordWrap(true);

    textCol->addWidget(titleLabel);
    textCol->addWidget(descLabel);

    layout->addWidget(iconWrap, 0, Qt::AlignTop);
    layout->addLayout(textCol, 1);

    return row;
}

QWidget *ActivationWindow::buildLeftPanel()
{
    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("LeftPanel"));

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(48, 48, 40, 32);
    layout->setSpacing(0);

    // Logo + wordmark
    auto *logo = new QSvgWidget(QStringLiteral(":/images/videox-logo.svg"));
    logo->setFixedSize(72, 72);

    auto *wordmarkRow = new QHBoxLayout;
    wordmarkRow->setContentsMargins(0, 0, 0, 0);
    wordmarkRow->setSpacing(0);
    auto *videoLabel = new QLabel(QStringLiteral("Video"));
    videoLabel->setObjectName(QStringLiteral("AppTitleVideo"));
    auto *xLabel = new QLabel(QStringLiteral("X"));
    xLabel->setObjectName(QStringLiteral("AppTitleX"));
    wordmarkRow->addWidget(videoLabel);
    wordmarkRow->addWidget(xLabel);
    wordmarkRow->addStretch(1);

    auto *subtitle = new QLabel(QStringLiteral("Downloader & Editor"));
    subtitle->setObjectName(QStringLiteral("AppSubtitle"));

    layout->addWidget(logo);
    layout->addSpacing(16);
    layout->addLayout(wordmarkRow);
    layout->addSpacing(2);
    layout->addWidget(subtitle);
    layout->addSpacing(40);

    layout->addWidget(buildFeatureRow(QStringLiteral(":/icons/shield-check.svg"),
        QStringLiteral("Tải video chất lượng cao"),
        QStringLiteral("Hỗ trợ hơn 1000+ website phổ biến như YouTube, Facebook, TikTok...")));
    layout->addSpacing(24);

    layout->addWidget(buildFeatureRow(QStringLiteral(":/icons/zap.svg"),
        QStringLiteral("Tốc độ nhanh vượt trội"),
        QStringLiteral("Công nghệ tăng tốc tải đa luồng, tiết kiệm thời gian")));
    layout->addSpacing(24);

    layout->addWidget(buildFeatureRow(QStringLiteral(":/icons/film-edit.svg"),
        QStringLiteral("Chỉnh sửa chuyên nghiệp"),
        QStringLiteral("Cắt ghép, thêm hiệu ứng, phụ đề và nhiều công cụ mạnh mẽ")));
    layout->addSpacing(24);

    layout->addWidget(buildFeatureRow(QStringLiteral(":/icons/lock.svg"),
        QStringLiteral("An toàn & bảo mật"),
        QStringLiteral("Key bản quyền giúp bảo vệ tài khoản và cập nhật tính năng mới liên tục")));

    layout->addStretch(1);

    auto *illustration = new QSvgWidget(QStringLiteral(":/images/illustration.svg"));
    illustration->setFixedHeight(180);
    layout->addWidget(illustration);

    return panel;
}

QWidget *ActivationWindow::buildRightPanel()
{
    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("RightPanel"));

    auto *outerLayout = new QVBoxLayout(panel);
    outerLayout->setContentsMargins(0, 44, 0, 24); // top margin reserves space for the title bar
    outerLayout->setSpacing(0);

    auto *content = new QVBoxLayout;
    content->setContentsMargins(60, 0, 60, 0);
    content->setSpacing(0);
    content->addStretch(2);

    // Header: key icon + welcome text
    auto *keyCircle = new QFrame;
    keyCircle->setObjectName(QStringLiteral("KeyCircle"));
    keyCircle->setFixedSize(64, 64);
    auto *keyCircleLayout = new QVBoxLayout(keyCircle);
    keyCircleLayout->setContentsMargins(0, 0, 0, 0);
    keyCircleLayout->setAlignment(Qt::AlignCenter);
    keyCircleLayout->addWidget(makeIcon(QStringLiteral(":/icons/key-indigo.svg"), 28));

    auto *keyCircleRow = new QHBoxLayout;
    keyCircleRow->addStretch(1);
    keyCircleRow->addWidget(keyCircle);
    keyCircleRow->addStretch(1);

    auto *welcomeTitle = new QLabel(QStringLiteral("Chào mừng đến với Lunex ReDown"));
    welcomeTitle->setObjectName(QStringLiteral("WelcomeTitle"));
    welcomeTitle->setAlignment(Qt::AlignCenter);

    auto *welcomeSubtitle = new QLabel(QStringLiteral("Vui lòng nhập key bản quyền để tiếp tục sử dụng phần mềm"));
    welcomeSubtitle->setObjectName(QStringLiteral("WelcomeSubtitle"));
    welcomeSubtitle->setAlignment(Qt::AlignCenter);

    content->addLayout(keyCircleRow);
    content->addSpacing(20);
    content->addWidget(welcomeTitle);
    content->addSpacing(8);
    content->addWidget(welcomeSubtitle);
    content->addSpacing(32);

    // License card
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("LicenseCard"));
    auto *cardShadow = new QGraphicsDropShadowEffect(card);
    cardShadow->setBlurRadius(24);
    cardShadow->setOffset(0, 4);
    cardShadow->setColor(QColor(17, 24, 39, 18));
    card->setGraphicsEffect(cardShadow);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(32, 28, 32, 28);
    cardLayout->setSpacing(0);

    auto *cardLabel = new QLabel(QStringLiteral("Nhập key của bạn"));
    cardLabel->setObjectName(QStringLiteral("LicenseCardLabel"));
    cardLayout->addWidget(cardLabel);
    cardLayout->addSpacing(12);

    auto *inputWrap = new QWidget;
    auto *inputWrapLayout = new QGridLayout(inputWrap);
    inputWrapLayout->setContentsMargins(0, 0, 0, 0);

    m_keyInput = new QLineEdit;
    m_keyInput->setObjectName(QStringLiteral("KeyInput"));
    m_keyInput->setPlaceholderText(QStringLiteral("Nhập key tại đây (ví dụ: LUNEX-XXXXXX-XXXXXX-XXXXXX-P30D)"));
    m_keyInput->setClearButtonEnabled(false);

    auto *keyIcon = makeIcon(QStringLiteral(":/icons/key.svg"), 18);
    keyIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

    inputWrapLayout->addWidget(m_keyInput, 0, 0);
    auto *iconOverlay = new QHBoxLayout;
    iconOverlay->setContentsMargins(16, 0, 0, 0);
    iconOverlay->addWidget(keyIcon);
    iconOverlay->addStretch(1);
    inputWrapLayout->addLayout(iconOverlay, 0, 0);

    cardLayout->addWidget(inputWrap);
    cardLayout->addSpacing(10);

    m_inlineMessageLabel = new QLabel;
    m_inlineMessageLabel->setObjectName(QStringLiteral("InlineErrorLabel"));
    m_inlineMessageLabel->setWordWrap(true);
    m_inlineMessageLabel->setVisible(false);
    cardLayout->addWidget(m_inlineMessageLabel);
    cardLayout->addSpacing(14);

    m_activateButton = new QPushButton(QStringLiteral("  Kích hoạt & Sử dụng"));
    m_activateButton->setObjectName(QStringLiteral("ActivateButton"));
    m_activateButton->setCursor(Qt::PointingHandCursor);
    m_activateButton->setIcon(QIcon(QStringLiteral(":/icons/lock-white.svg")));
    m_activateButton->setIconSize(QSize(18, 18));
    cardLayout->addWidget(m_activateButton);

    content->addWidget(card);
    content->addSpacing(20);

    // Separator
    auto *sepRow = new QHBoxLayout;
    auto *lineLeft = new QFrame;
    lineLeft->setObjectName(QStringLiteral("SeparatorLine"));
    lineLeft->setFixedHeight(1);
    auto *lineRight = new QFrame;
    lineRight->setObjectName(QStringLiteral("SeparatorLine"));
    lineRight->setFixedHeight(1);
    auto *sepLabel = new QLabel(QStringLiteral("HOẶC"));
    sepLabel->setObjectName(QStringLiteral("SeparatorLabel"));
    sepRow->addWidget(lineLeft, 1);
    sepRow->addWidget(sepLabel, 0);
    sepRow->addWidget(lineRight, 1);
    sepRow->setSpacing(16);

    content->addLayout(sepRow);
    content->addSpacing(20);

    // Purchase button
    m_purchaseButton = new QPushButton(QStringLiteral("   Mua key bản quyền"));
    m_purchaseButton->setObjectName(QStringLiteral("PurchaseButton"));
    m_purchaseButton->setCursor(Qt::PointingHandCursor);
    m_purchaseButton->setIcon(QIcon(QStringLiteral(":/icons/cart.svg")));
    m_purchaseButton->setIconSize(QSize(18, 18));
    m_purchaseButton->setLayoutDirection(Qt::LeftToRight);

    auto *purchaseWrap = new QHBoxLayout;
    purchaseWrap->addWidget(m_purchaseButton, 1);
    content->addLayout(purchaseWrap);

    content->addSpacing(24);

    auto *hint = new QLabel(QStringLiteral("Bạn chưa có key? Mua ngay để trải nghiệm đầy đủ tính năng của Lunex ReDown."));
    hint->setObjectName(QStringLiteral("LowerHintLabel"));
    hint->setAlignment(Qt::AlignCenter);
    content->addWidget(hint);

    content->addStretch(3);

    outerLayout->addLayout(content);

    // Footer
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(60, 0, 60, 8);

    auto *supportBtn = new QPushButton(QStringLiteral(" Hỗ trợ"));
    supportBtn->setObjectName(QStringLiteral("FooterLink"));
    supportBtn->setCursor(Qt::PointingHandCursor);
    supportBtn->setIcon(QIcon(QStringLiteral(":/icons/headset.svg")));
    supportBtn->setFlat(true);

    auto *websiteBtn = new QPushButton(QStringLiteral(" Website"));
    websiteBtn->setObjectName(QStringLiteral("FooterLink"));
    websiteBtn->setCursor(Qt::PointingHandCursor);
    websiteBtn->setIcon(QIcon(QStringLiteral(":/icons/globe.svg")));
    websiteBtn->setFlat(true);

    auto *versionLabel = new QLabel(QStringLiteral("Phiên bản: 1.0.0"));
    versionLabel->setObjectName(QStringLiteral("FooterVersion"));

    footer->addWidget(supportBtn);
    footer->addSpacing(16);
    footer->addWidget(websiteBtn);
    footer->addStretch(1);
    footer->addWidget(versionLabel);

    outerLayout->addLayout(footer);

    connect(m_activateButton, &QPushButton::clicked, this, &ActivationWindow::onActivateClicked);
    connect(m_keyInput, &QLineEdit::textChanged, this, &ActivationWindow::onKeyTextChanged);
    connect(m_keyInput, &QLineEdit::returnPressed, this, &ActivationWindow::onActivateClicked);
    connect(m_purchaseButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(AppConfig::purchaseUrl()));
    });
    connect(supportBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(AppConfig::supportUrl()));
    });
    connect(websiteBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(AppConfig::websiteUrl()));
    });

    return panel;
}

void ActivationWindow::onKeyTextChanged(const QString &)
{
    clearInlineMessage();
}

void ActivationWindow::onActivateClicked()
{
    clearInlineMessage();
    m_licenseService->activateKey(m_keyInput->text());
}

void ActivationWindow::setLoadingState(bool loading)
{
    m_activateButton->setEnabled(!loading);
    m_activateButton->setText(loading
        ? QStringLiteral("  Đang kích hoạt...")
        : QStringLiteral("  Kích hoạt & Sử dụng"));
    m_keyInput->setEnabled(!loading);
}

void ActivationWindow::showInlineError(const QString &message)
{
    m_inlineMessageLabel->setObjectName(QStringLiteral("InlineErrorLabel"));
    m_inlineMessageLabel->setStyleSheet(QString()); // force QSS re-eval via objectName
    m_inlineMessageLabel->setText(message.isEmpty()
        ? QStringLiteral("Key không hợp lệ hoặc đã hết hạn.")
        : message);
    m_inlineMessageLabel->setVisible(true);
    style()->unpolish(m_inlineMessageLabel);
    style()->polish(m_inlineMessageLabel);
}

void ActivationWindow::showInlineSuccess(const QString &message)
{
    m_inlineMessageLabel->setObjectName(QStringLiteral("InlineSuccessLabel"));
    m_inlineMessageLabel->setText(message);
    m_inlineMessageLabel->setVisible(true);
    style()->unpolish(m_inlineMessageLabel);
    style()->polish(m_inlineMessageLabel);
}

void ActivationWindow::clearInlineMessage()
{
    m_inlineMessageLabel->setVisible(false);
    m_inlineMessageLabel->clear();
}
