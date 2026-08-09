#include "MainWindow.h"
#include "TitleBar.h"
#include "../services/AppConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QFrame>
#include <QScrollArea>
#include <QSvgWidget>
#include <QGraphicsDropShadowEffect>
#include <QFileDialog>
#include <QStandardPaths>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QColor>
#include <QResizeEvent>
#include <QGraphicsEffect>
#include <QIcon>
#include <QButtonGroup>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>

namespace
{
QLabel *makeIcon(const QString &path, int size)
{
    auto *label = new QLabel;
    label->setPixmap(QIcon(path).pixmap(size, size));
    label->setFixedSize(size, size);
    return label;
}
}

MainWindow::MainWindow(LicenseService *licenseService, QWidget *parent)
    : QWidget(parent)
    , m_licenseService(licenseService)
    , m_downloadService(new DownloadService(this))
    , m_thumbNetwork(new QNetworkAccessManager(this))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1600, 980);
    setMinimumSize(1200, 760);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);

    m_root = new QFrame(this);
    QFrame *root = m_root;
    root->setObjectName(QStringLiteral("MainRoot"));
    auto *shadow = new QGraphicsDropShadowEffect(root);
    shadow->setBlurRadius(48);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 90));
    root->setGraphicsEffect(shadow);

    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildSidebar());
    rootLayout->addWidget(buildContent(), 1);
    rootLayout->addWidget(buildRightPane());

    m_titleBar = new TitleBar(root);
    m_titleBar->setParent(root);
    m_titleBar->raise();
    m_titleBar->move(root->width() - m_titleBar->width(), 0);
    connect(m_titleBar, &TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::closeClicked, this, &QWidget::close);
    connect(m_titleBar, &TitleBar::maximizeClicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });

    outer->addWidget(root);

    connect(m_downloadService, &DownloadService::jobFailed, this, [](const QString &, const QString &msg) {
        // Kept intentionally simple: a production build would surface this
        // as an inline toast on the relevant job card rather than a dialog.
        Q_UNUSED(msg);
    });

    connect(m_downloadService, &DownloadService::videoInfoReady, this, &MainWindow::showVideoInfo);
    connect(m_downloadService, &DownloadService::videoInfoFailed, this, &MainWindow::showAnalyzeError);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // Keep the min/max/close cluster pinned to the true top-right corner on
    // every resize (drag-resize, maximize, restore, DPI change) instead of
    // the one-shot position computed at construction time.
    if (m_titleBar && m_root) {
        m_titleBar->move(m_root->width() - m_titleBar->width(), 0);
    }

    // Maximized -> fill the whole screen edge-to-edge (drop the floating
    // card's outer margin + shadow); windowed -> restore the card look.
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

QPushButton *MainWindow::makeNavButton(const QString &iconPath, const QString &text, const QString &badge)
{
    auto *btn = new QPushButton;
    btn->setObjectName(QStringLiteral("NavButton"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(40);
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(QSize(16, 16));
    btn->setText(QStringLiteral("  %1").arg(text));

    if (!badge.isEmpty()) {
        auto *layout = new QHBoxLayout(btn);
        layout->setContentsMargins(0, 0, 12, 0);
        auto *badgeLabel = new QLabel(badge);
        badgeLabel->setObjectName(QStringLiteral("NavBadge"));
        badgeLabel->setAlignment(Qt::AlignCenter);
        layout->addStretch(1);
        layout->addWidget(badgeLabel);
    }
    return btn;
}

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("MainSidebar"));
    sidebar->setFixedWidth(260);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(0);

    auto *logoRow = new QHBoxLayout;
    auto *logo = new QSvgWidget(QStringLiteral(":/images/videox-logo.svg"));
    logo->setFixedSize(36, 36);
    auto *logoTextCol = new QVBoxLayout;
    logoTextCol->setSpacing(0);
    auto *logoTitle = new QLabel(QStringLiteral("Lunex ReDown"));
    logoTitle->setObjectName(QStringLiteral("SidebarLogoText"));
    auto *logoSub = new QLabel(QStringLiteral("Downloader & Editor"));
    logoSub->setObjectName(QStringLiteral("SidebarLogoSub"));
    logoTextCol->addWidget(logoTitle);
    logoTextCol->addWidget(logoSub);
    logoRow->addWidget(logo);
    logoRow->addSpacing(8);
    logoRow->addLayout(logoTextCol);
    logoRow->addStretch(1);
    auto *versionBadge = new QLabel(QStringLiteral("v1.0.0"));
    versionBadge->setObjectName(QStringLiteral("VersionBadge"));
    logoRow->addWidget(versionBadge);

    layout->addLayout(logoRow);
    layout->addSpacing(28);

    // "Đang tải" used to be its own nav destination backed by a fake demo
    // list in the right pane. That list never reflected real download
    // state, so it's gone now -- completed downloads still get a home
    // below ("Đã hoàn thành"), and in-progress state belongs on the job
    // itself once real progress wiring lands, not a separate page.
    auto *navHome = makeNavButton(QStringLiteral(":/icons/download-arrow.svg"), QStringLiteral("Tải Video"));
    navHome->setChecked(true);
    auto *navEditor = makeNavButton(QStringLiteral(":/icons/film-edit.svg"), QStringLiteral("Trình chỉnh sửa"));
    auto *navDone = makeNavButton(QStringLiteral(":/icons/check-circle.svg"), QStringLiteral("Đã hoàn thành"), QStringLiteral("12"));
    auto *navHistory = makeNavButton(QStringLiteral(":/icons/globe.svg"), QStringLiteral("Lịch sử"));
    auto *navTools = makeNavButton(QStringLiteral(":/icons/zap.svg"), QStringLiteral("Công cụ"));
    auto *navSettings = makeNavButton(QStringLiteral(":/icons/lock.svg"), QStringLiteral("Cài đặt"));

    for (auto *btn : { navHome, navEditor, navDone, navHistory, navTools, navSettings }) {
        layout->addWidget(btn);
        layout->addSpacing(4);
    }

    // Exclusive selection: clicking one nav item unchecks every other one.
    // Previously each QPushButton was independently checkable, so several
    // could end up highlighted at once (e.g. "Tải Video" + "Trình chỉnh
    // sửa" both active in the screenshot).
    auto *navGroup = new QButtonGroup(sidebar);
    navGroup->setExclusive(true);
    for (auto *btn : { navHome, navEditor, navDone, navHistory, navTools, navSettings }) {
        navGroup->addButton(btn);
    }

    layout->addStretch(1);

    // License status card. MainWindow is only ever shown once
    // LicenseService::isActivated() is true, so there is always a valid
    // license here -- show its expiry instead of an "upgrade" pitch that
    // no longer makes sense once the user already activated a key.
    const LicenseInfo license = m_licenseService ? m_licenseService->loadLicense() : LicenseInfo{};

    auto *upgradeCard = new QFrame;
    upgradeCard->setObjectName(QStringLiteral("UpgradeCard"));
    auto *upgradeLayout = new QVBoxLayout(upgradeCard);
    upgradeLayout->setContentsMargins(16, 16, 16, 16);
    upgradeLayout->setSpacing(6);

    // Plain text, no emoji/colored badges -- matches the understated
    // account block at the bottom of the reference sidebar.
    if (license.isValid()) {
        const QString packageLabel = license.package.isEmpty()
            ? QStringLiteral("PRO")
            : license.package;
        auto *statusTitle = new QLabel(QStringLiteral("Gói %1 · Đang hoạt động").arg(packageLabel));
        statusTitle->setObjectName(QStringLiteral("UpgradeTitle"));
        upgradeLayout->addWidget(statusTitle);
        upgradeLayout->addSpacing(4);

        if (license.expiresAt.isValid()) {
            auto *expiryLabel = new QLabel(
                QStringLiteral("Hết hạn: %1").arg(license.expiresAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy"))));
            expiryLabel->setObjectName(QStringLiteral("UpgradeItem"));
            upgradeLayout->addWidget(expiryLabel);
        }
        auto *daysLeftLabel = new QLabel(
            license.daysLeft > 0
                ? QStringLiteral("Còn lại %1 ngày sử dụng").arg(license.daysLeft)
                : QStringLiteral("Sắp hết hạn"));
        daysLeftLabel->setObjectName(QStringLiteral("UpgradeItem"));
        upgradeLayout->addWidget(daysLeftLabel);
        upgradeLayout->addSpacing(10);

        auto *renewBtn = new QPushButton(QStringLiteral("Gia hạn / Mua thêm key"));
        renewBtn->setObjectName(QStringLiteral("UpgradeButton"));
        renewBtn->setCursor(Qt::PointingHandCursor);
        upgradeLayout->addWidget(renewBtn);
        connect(renewBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(AppConfig::purchaseUrl()));
        });
    } else {
        // Fallback (shouldn't normally be reachable from MainWindow, kept
        // defensive in case license state is ever cleared while this
        // window is still open).
        auto *upgradeTitle = new QLabel(QStringLiteral("Nâng cấp Pro"));
        upgradeTitle->setObjectName(QStringLiteral("UpgradeTitle"));
        upgradeLayout->addWidget(upgradeTitle);
        upgradeLayout->addSpacing(4);

        for (const QString &item : { QStringLiteral("Tốc độ tải nhanh hơn"),
                                      QStringLiteral("Không giới hạn lượt tải"),
                                      QStringLiteral("Hỗ trợ 1000+ website"),
                                      QStringLiteral("Chuyển đổi không giới hạn") }) {
            auto *l = new QLabel(item);
            l->setObjectName(QStringLiteral("UpgradeItem"));
            upgradeLayout->addWidget(l);
        }
        upgradeLayout->addSpacing(10);

        auto *upgradeBtn = new QPushButton(QStringLiteral("Nâng cấp ngay"));
        upgradeBtn->setObjectName(QStringLiteral("UpgradeButton"));
        upgradeBtn->setCursor(Qt::PointingHandCursor);
        upgradeLayout->addWidget(upgradeBtn);
        connect(upgradeBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(AppConfig::purchaseUrl()));
        });
    }

    layout->addWidget(upgradeCard);

    return sidebar;
}

QWidget *MainWindow::makePlatformChip(const QString &iconPath, const QString &label)
{
    auto *chip = new QFrame;
    chip->setObjectName(QStringLiteral("PlatformChip"));
    chip->setFixedSize(96, 74);
    auto *layout = new QVBoxLayout(chip);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(6);
    if (!iconPath.isEmpty()) {
        auto *icon = makeIcon(iconPath, 22);
        icon->setAlignment(Qt::AlignCenter);
        layout->addWidget(icon, 0, Qt::AlignHCenter);
    }
    auto *text = new QLabel(label);
    text->setAlignment(Qt::AlignCenter);
    text->setStyleSheet(QStringLiteral("color:#D1D5DB; font-size:11.5px;"));
    layout->addWidget(text);
    return chip;
}

QWidget *MainWindow::buildContent()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *content = new QWidget;
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(36, 60, 36, 36);
    layout->setSpacing(0);

    auto *title = new QLabel(QStringLiteral("Tải video từ mọi nền tảng"));
    title->setObjectName(QStringLiteral("MainContentTitle"));
    auto *subtitle = new QLabel(QStringLiteral("Hỗ trợ YouTube, Facebook, TikTok, Instagram, Twitter và hơn 1000+ website khác"));
    subtitle->setObjectName(QStringLiteral("MainContentSubtitle"));

    layout->addWidget(title);
    layout->addSpacing(4);
    layout->addWidget(subtitle);
    layout->addSpacing(24);

    // URL input row
    auto *urlRow = new QHBoxLayout;
    m_urlInput = new QLineEdit;
    m_urlInput->setObjectName(QStringLiteral("UrlInputBar"));
    m_urlInput->setPlaceholderText(QStringLiteral("Dán liên kết video vào đây..."));

    auto *pasteBtn = new QPushButton(QStringLiteral("  Dán từ clipboard"));
    pasteBtn->setObjectName(QStringLiteral("PasteButton"));
    pasteBtn->setCursor(Qt::PointingHandCursor);
    pasteBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));

    m_analyzeButton = new QPushButton(QStringLiteral("  Phân tích"));
    m_analyzeButton->setObjectName(QStringLiteral("PasteButton"));
    m_analyzeButton->setCursor(Qt::PointingHandCursor);
    m_analyzeButton->setIcon(QIcon(QStringLiteral(":/icons/globe.svg")));

    urlRow->addWidget(m_urlInput, 1);
    urlRow->addSpacing(10);
    urlRow->addWidget(pasteBtn);
    urlRow->addSpacing(10);
    urlRow->addWidget(m_analyzeButton);
    layout->addLayout(urlRow);
    layout->addSpacing(28);

    auto *platformsLabel = new QLabel(QStringLiteral("Hỗ trợ các nền tảng phổ biến"));
    platformsLabel->setObjectName(QStringLiteral("SectionLabel"));
    layout->addWidget(platformsLabel);
    layout->addSpacing(12);

    auto *chipsRow = new QHBoxLayout;
    chipsRow->setSpacing(12);
    chipsRow->addWidget(makePlatformChip(QStringLiteral(":/images/youtube-logo.png"), QStringLiteral("YouTube")));
    chipsRow->addWidget(makePlatformChip(QStringLiteral(":/images/facebook-logo.png"), QStringLiteral("Facebook")));
    chipsRow->addWidget(makePlatformChip(QString(), QStringLiteral("TikTok")));
    chipsRow->addWidget(makePlatformChip(QString(), QStringLiteral("Instagram")));
    chipsRow->addWidget(makePlatformChip(QString(), QStringLiteral("Twitter")));
    chipsRow->addWidget(makePlatformChip(QString(), QStringLiteral("Khác")));
    chipsRow->addStretch(1);
    layout->addLayout(chipsRow);
    layout->addSpacing(28);

    auto *optionsLabel = new QLabel(QStringLiteral("Tùy chọn tải xuống"));
    optionsLabel->setObjectName(QStringLiteral("SectionLabel"));
    layout->addWidget(optionsLabel);
    layout->addSpacing(12);

    auto *optionsCard = new QFrame;
    optionsCard->setObjectName(QStringLiteral("OptionsCard"));
    auto *optionsLayout = new QGridLayout(optionsCard);
    optionsLayout->setContentsMargins(20, 20, 20, 20);
    optionsLayout->setHorizontalSpacing(20);
    optionsLayout->setVerticalSpacing(14);

    auto addField = [&](int row, int col, const QString &labelText, QWidget *field) {
        auto *col_ = new QVBoxLayout;
        auto *l = new QLabel(labelText);
        l->setObjectName(QStringLiteral("FieldLabel"));
        col_->addWidget(l);
        col_->addWidget(field);
        optionsLayout->addLayout(col_, row, col);
    };

    m_resolutionCombo = new QComboBox;
    m_resolutionCombo->setObjectName(QStringLiteral("OptionCombo"));
    m_resolutionCombo->addItems({QStringLiteral("1080p (Full HD)"), QStringLiteral("720p (HD)"), QStringLiteral("480p"), QStringLiteral("360p")});

    m_formatCombo = new QComboBox;
    m_formatCombo->setObjectName(QStringLiteral("OptionCombo"));
    m_formatCombo->addItems({QStringLiteral("MP4"), QStringLiteral("MKV"), QStringLiteral("MP3 (chỉ âm thanh)")});

    m_qualityCombo = new QComboBox;
    m_qualityCombo->setObjectName(QStringLiteral("OptionCombo"));
    m_qualityCombo->addItems({QStringLiteral("Chất lượng cao"), QStringLiteral("Chất lượng trung bình"), QStringLiteral("Chất lượng thấp")});

    m_folderField = new QLineEdit(QStringLiteral("D:\\LunexReDown\\Downloads"));
    m_folderField->setObjectName(QStringLiteral("FolderField"));

    addField(0, 0, QStringLiteral("Độ phân giải"), m_resolutionCombo);
    addField(0, 1, QStringLiteral("Định dạng"), m_formatCombo);
    addField(1, 0, QStringLiteral("Chất lượng"), m_qualityCombo);
    addField(1, 1, QStringLiteral("Thư mục lưu"), m_folderField);

    auto *checksRow = new QHBoxLayout;
    auto *thumbCheck = new QCheckBox(QStringLiteral("Tải thumbnail"));
    thumbCheck->setChecked(true);
    thumbCheck->setStyleSheet(QStringLiteral("color:#D1D5DB; font-size:12.5px;"));
    auto *subCheck = new QCheckBox(QStringLiteral("Tải phụ đề (nếu có)"));
    subCheck->setChecked(true);
    subCheck->setStyleSheet(QStringLiteral("color:#D1D5DB; font-size:12.5px;"));
    auto *convertCheck = new QCheckBox(QStringLiteral("Chuyển đổi sau khi tải"));
    convertCheck->setStyleSheet(QStringLiteral("color:#D1D5DB; font-size:12.5px;"));
    checksRow->addWidget(thumbCheck);
    checksRow->addSpacing(24);
    checksRow->addWidget(convertCheck);
    checksRow->addSpacing(24);
    checksRow->addWidget(subCheck);
    checksRow->addStretch(1);
    optionsLayout->addLayout(checksRow, 2, 0, 1, 2);

    layout->addWidget(optionsCard);
    layout->addSpacing(20);

    auto *downloadBtn = new QPushButton(QStringLiteral("  Tải xuống"));
    downloadBtn->setObjectName(QStringLiteral("DownloadButton"));
    downloadBtn->setCursor(Qt::PointingHandCursor);
    downloadBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    layout->addWidget(downloadBtn);
    layout->addSpacing(20);

    // Preview card: filled in by analyzeLink() once yt-dlp reports the
    // video's metadata. Hidden until there is something to show. Lives
    // below the download button rather than above the URL bar, so the
    // page reads top-to-bottom: paste link -> set options -> download ->
    // see what you just analyzed/downloaded.
    m_previewCard = new QFrame;
    m_previewCard->setObjectName(QStringLiteral("OptionsCard"));
    m_previewCard->setVisible(false);
    auto *previewLayout = new QHBoxLayout(m_previewCard);
    previewLayout->setContentsMargins(16, 16, 16, 16);
    previewLayout->setSpacing(16);

    m_previewThumb = new QLabel;
    m_previewThumb->setFixedSize(160, 90);
    m_previewThumb->setScaledContents(true);
    m_previewThumb->setStyleSheet(QStringLiteral("background:#0B111F; border-radius:8px;"));
    previewLayout->addWidget(m_previewThumb);

    auto *previewTextCol = new QVBoxLayout;
    previewTextCol->setSpacing(6);
    m_previewTitle = new QLabel;
    m_previewTitle->setObjectName(QStringLiteral("JobTitle"));
    m_previewTitle->setWordWrap(true);
    m_previewMeta = new QLabel;
    m_previewMeta->setObjectName(QStringLiteral("FieldLabel"));
    m_previewMeta->setWordWrap(true);
    previewTextCol->addWidget(m_previewTitle);
    previewTextCol->addWidget(m_previewMeta);
    previewTextCol->addStretch(1);
    previewLayout->addLayout(previewTextCol, 1);

    layout->addWidget(m_previewCard);

    m_previewError = new QLabel;
    m_previewError->setObjectName(QStringLiteral("FieldLabel"));
    m_previewError->setStyleSheet(QStringLiteral("color:#F87171; font-size:12.5px;"));
    m_previewError->setVisible(false);
    m_previewError->setWordWrap(true);
    layout->addWidget(m_previewError);

    layout->addStretch(1);

    connect(pasteBtn, &QPushButton::clicked, this, [this]() {
        m_urlInput->setText(QApplication::clipboard()->text());
    });
    connect(m_analyzeButton, &QPushButton::clicked, this, &MainWindow::analyzeLink);
    connect(downloadBtn, &QPushButton::clicked, this, &MainWindow::startDownload);
    connect(m_folderField, &QLineEdit::returnPressed, this, [this]() {
        // no-op placeholder; a "..." browse button can call QFileDialog::getExistingDirectory
    });

    scroll->setWidget(content);
    return scroll;
}

QWidget *MainWindow::buildRightPane()
{
    auto *pane = new QFrame;
    pane->setObjectName(QStringLiteral("RightPane"));
    pane->setFixedWidth(360);

    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(20, 60, 20, 20);
    layout->setSpacing(0);

    // "Đang tải" (in-progress) used to live here as a hardcoded demo list
    // with no connection to DownloadService's real progress signals. It's
    // gone until real progress wiring lands -- showing fake percentages
    // next to a real download queue was actively misleading.
    auto *doneHeader = new QHBoxLayout;
    auto *doneTitle = new QLabel(QStringLiteral("Đã hoàn thành (12)"));
    doneTitle->setObjectName(QStringLiteral("PaneTitle"));
    auto *clearAllBtn = new QPushButton(QStringLiteral("Xóa tất cả"));
    clearAllBtn->setObjectName(QStringLiteral("PaneLinkButton"));
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    doneHeader->addWidget(doneTitle);
    doneHeader->addStretch(1);
    doneHeader->addWidget(clearAllBtn);
    layout->addLayout(doneHeader);
    layout->addSpacing(12);

    m_completedListLayout = new QVBoxLayout;
    m_completedListLayout->setSpacing(8);
    layout->addLayout(m_completedListLayout);

    struct DoneJob { QString title; QString meta; };
    const QList<DoneJob> demoDone = {
        {QStringLiteral("Amazing Waterfalls 4K"), QStringLiteral("1080p • MP4 • 98.7 MB")},
        {QStringLiteral("Best Music Mix 2024"), QStringLiteral("1080p • MP4 • 152.4 MB")},
        {QStringLiteral("Cooking ASMR"), QStringLiteral("720p • MP4 • 45.2 MB")},
        {QStringLiteral("Gaming Highlights"), QStringLiteral("1080p • MP4 • 78.9 MB")},
    };

    for (const auto &job : demoDone) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("JobCard"));
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);

        auto *textCol = new QVBoxLayout;
        auto *titleLabel = new QLabel(job.title);
        titleLabel->setObjectName(QStringLiteral("JobTitle"));
        auto *metaLabel = new QLabel(job.meta);
        metaLabel->setObjectName(QStringLiteral("JobMeta"));
        textCol->addWidget(titleLabel);
        textCol->addWidget(metaLabel);

        cardLayout->addLayout(textCol, 1);
        m_completedListLayout->addWidget(card);
    }

    auto *viewAllBtn = new QPushButton(QStringLiteral("Xem tất cả"));
    viewAllBtn->setObjectName(QStringLiteral("PaneLinkButton"));
    viewAllBtn->setCursor(Qt::PointingHandCursor);
    layout->addSpacing(8);
    layout->addWidget(viewAllBtn, 0, Qt::AlignHCenter);

    layout->addStretch(1);

    return pane;
}

void MainWindow::startDownload()
{
    const QString url = m_urlInput->text().trimmed();
    if (url.isEmpty()) {
        return;
    }

    const QString outputDir = m_folderField->text().trimmed().isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        : m_folderField->text().trimmed();

    m_downloadService->startDownload(url,
                                      m_resolutionCombo->currentText(),
                                      m_formatCombo->currentText(),
                                      outputDir);
    m_urlInput->clear();
}

void MainWindow::analyzeLink()
{
    const QString url = m_urlInput->text().trimmed();

    m_previewError->setVisible(false);
    m_previewCard->setVisible(false);

    if (url.isEmpty()) {
        showAnalyzeError(QStringLiteral("Vui lòng dán một liên kết video trước."));
        return;
    }

    m_analyzeButton->setEnabled(false);
    m_analyzeButton->setText(QStringLiteral("  Đang phân tích..."));

    // DownloadService::fetchVideoInfo shells out to the same yt-dlp engine
    // as the actual download, so it works for every site yt-dlp supports
    // (YouTube, Facebook, TikTok, Instagram, Twitter, and 1000+ others) --
    // not a YouTube-only lookup.
    m_downloadService->fetchVideoInfo(url);
}

void MainWindow::showVideoInfo(const VideoInfo &info)
{
    m_analyzeButton->setEnabled(true);
    m_analyzeButton->setText(QStringLiteral("  Phân tích"));

    m_previewError->setVisible(false);

    m_previewTitle->setText(info.title);

    QStringList metaParts;
    if (!info.uploader.isEmpty()) {
        metaParts << QStringLiteral("Kênh: %1").arg(info.uploader);
    }
    if (!info.uploadDate.isEmpty()) {
        metaParts << QStringLiteral("Ngày đăng: %1").arg(info.uploadDate);
    }
    if (!info.durationText.isEmpty()) {
        metaParts << QStringLiteral("Thời lượng: %1").arg(info.durationText);
    }
    if (!info.platform.isEmpty()) {
        metaParts << info.platform;
    }
    m_previewMeta->setText(metaParts.join(QStringLiteral("   •   ")));

    m_previewThumb->setPixmap(QPixmap());
    m_previewCard->setVisible(true);

    if (!info.thumbnailUrl.isEmpty()) {
        QNetworkRequest request{QUrl(info.thumbnailUrl)};
        QNetworkReply *reply = m_thumbNetwork->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                return;
            }
            QPixmap pixmap;
            if (pixmap.loadFromData(reply->readAll())) {
                m_previewThumb->setPixmap(pixmap);
            }
        });
    }
}

void MainWindow::showAnalyzeError(const QString &message)
{
    m_analyzeButton->setEnabled(true);
    m_analyzeButton->setText(QStringLiteral("  Phân tích"));

    m_previewCard->setVisible(false);
    m_previewError->setText(message);
    m_previewError->setVisible(true);
}
