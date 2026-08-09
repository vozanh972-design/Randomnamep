#include "MainWindow.h"
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
#include <QProgressBar>
#include <QFileDialog>
#include <QStandardPaths>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QButtonGroup>
#include <QFontMetrics>
#include <QStyle>
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
    // Native window chrome (default Windows title bar with its own
    // minimize/maximize/close) instead of a frameless window with a
    // hand-drawn titlebar. The old Qt::FramelessWindowHint +
    // WA_TranslucentBackground combination was also what caused the
    // "two overlapping UIs" glitch: on some Windows/DWM configurations a
    // translucent frameless top-level widget composites its own rounded
    // card on top of a residual native frame instead of replacing it.
    resize(1600, 980);
    setMinimumSize(1200, 760);
    setWindowTitle(QStringLiteral("Lunex ReDown"));
    setObjectName(QStringLiteral("MainRoot"));

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildSidebar());
    rootLayout->addWidget(buildContent(), 1);

    connect(m_downloadService, &DownloadService::jobFailed, this, [this](const QString &id, const QString &msg) {
        markQueueFailed(id, msg);
    });
    connect(m_downloadService, &DownloadService::progressChanged, this,
            [this](const QString &id, int percent, const QString &speed, const QString &eta) {
        updateQueueProgress(id, percent, speed, eta);
    });
    connect(m_downloadService, &DownloadService::jobCompleted, this, [this](const QString &id, const QString &) {
        markQueueCompleted(id);
    });

    connect(m_downloadService, &DownloadService::videoInfoReady, this, &MainWindow::showVideoInfo);
    connect(m_downloadService, &DownloadService::videoInfoFailed, this, &MainWindow::showAnalyzeError);
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

    // "Đã hoàn thành" (completed) used to have its own nav entry with a
    // hardcoded "12" badge that never reflected real state. Removed --
    // finished downloads belong in "Lịch sử" once that view is wired up,
    // not as a separate always-visible counter.
    auto *navHome = makeNavButton(QStringLiteral(":/icons/download-arrow.svg"), QStringLiteral("Tải Video"));
    navHome->setChecked(true);
    auto *navEditor = makeNavButton(QStringLiteral(":/icons/film-edit.svg"), QStringLiteral("Trình chỉnh sửa"));
    auto *navHistory = makeNavButton(QStringLiteral(":/icons/globe.svg"), QStringLiteral("Lịch sử"));
    auto *navTools = makeNavButton(QStringLiteral(":/icons/zap.svg"), QStringLiteral("Công cụ"));
    auto *navSettings = makeNavButton(QStringLiteral(":/icons/lock.svg"), QStringLiteral("Cài đặt"));

    for (auto *btn : { navHome, navEditor, navHistory, navTools, navSettings }) {
        layout->addWidget(btn);
        layout->addSpacing(4);
    }

    // Exclusive selection: clicking one nav item unchecks every other one.
    auto *navGroup = new QButtonGroup(sidebar);
    navGroup->setExclusive(true);
    for (auto *btn : { navHome, navEditor, navHistory, navTools, navSettings }) {
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

// Small round badge used in the compact "Hỗ trợ" strip. Platforms that
// ship a real logo asset (YouTube, Facebook) show it; the rest fall back
// to a two-letter monogram on a brand-ish background so the strip stays a
// single tidy row instead of the old full-size labeled tiles.
QWidget *MainWindow::makePlatformIcon(const QString &iconPath, const QString &label, const QString &bgColor)
{
    auto *badge = new QLabel;
    badge->setObjectName(QStringLiteral("PlatformIcon"));
    badge->setFixedSize(24, 24);
    badge->setAlignment(Qt::AlignCenter);
    badge->setToolTip(label);
    badge->setStyleSheet(QStringLiteral("background:%1; border-radius:12px; color:#FFFFFF; font-size:9.5px; font-weight:700;").arg(bgColor));

    if (!iconPath.isEmpty()) {
        badge->setPixmap(QIcon(iconPath).pixmap(12, 12));
    } else {
        QString monogram = label.left(2).toUpper();
        if (label == QStringLiteral("Twitter")) {
            monogram = QStringLiteral("X");
        }
        badge->setText(monogram);
    }
    return badge;
}

QWidget *MainWindow::buildContent()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *content = new QWidget;
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *outerLayout = new QHBoxLayout(content);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // With the right-hand "Đã hoàn thành" pane gone, the content area is
    // the only thing left of the sidebar and would otherwise stretch edge
    // to edge on a wide window. Cap it to a comfortable reading width and
    // let it hug the left side instead, the way the reference layout does.
    auto *column = new QWidget;
    column->setMaximumWidth(860);
    auto *layout = new QVBoxLayout(column);
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

    // Compact single-line "supported platforms" strip, styled after the
    // reference: a plain row (no boxed card) with a small live-status dot,
    // round platform badges, and a trailing "+1000 websites" note.
    auto *platformBar = new QFrame;
    platformBar->setObjectName(QStringLiteral("PlatformBar"));
    auto *platformLayout = new QHBoxLayout(platformBar);
    platformLayout->setContentsMargins(2, 0, 2, 0);
    platformLayout->setSpacing(10);

    auto *statusDot = new QLabel;
    statusDot->setObjectName(QStringLiteral("PlatformStatusDot"));
    statusDot->setFixedSize(8, 8);
    platformLayout->addWidget(statusDot);

    auto *supportLabel = new QLabel(QStringLiteral("Hỗ trợ:"));
    supportLabel->setObjectName(QStringLiteral("PlatformBarLabel"));
    platformLayout->addWidget(supportLabel);

    platformLayout->addWidget(makePlatformIcon(QStringLiteral(":/images/youtube-logo.png"), QStringLiteral("YouTube"), QStringLiteral("#FF0000")));
    platformLayout->addWidget(makePlatformIcon(QStringLiteral(":/images/facebook-logo.png"), QStringLiteral("Facebook"), QStringLiteral("#1877F2")));
    platformLayout->addWidget(makePlatformIcon(QString(), QStringLiteral("TikTok"), QStringLiteral("#111827")));
    platformLayout->addWidget(makePlatformIcon(QString(), QStringLiteral("Instagram"), QStringLiteral("#C13584")));
    platformLayout->addWidget(makePlatformIcon(QString(), QStringLiteral("Twitter"), QStringLiteral("#000000")));

    auto *moreLabel = new QLabel(QStringLiteral("và hơn 1000+ website khác"));
    moreLabel->setObjectName(QStringLiteral("PlatformBarMore"));
    platformLayout->addWidget(moreLabel);
    platformLayout->addStretch(1);

    layout->addWidget(platformBar);
    layout->addSpacing(24);

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

    // Preview card: filled in by analyzeLink() once yt-dlp reports the
    // video's metadata. Hidden until there is something to show, and
    // carries its own "Tải xuống" button (below) since that's the only
    // point in the page where there's actually something to download.
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

    // Download only becomes an option once there's something analyzed to
    // download -- so the button lives here, on the result, rather than as
    // a big always-on button with nothing to act on yet.
    m_previewDownloadButton = new QPushButton(QStringLiteral("  Tải xuống"));
    m_previewDownloadButton->setObjectName(QStringLiteral("PreviewDownloadButton"));
    m_previewDownloadButton->setCursor(Qt::PointingHandCursor);
    m_previewDownloadButton->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    previewLayout->addWidget(m_previewDownloadButton, 0, Qt::AlignVCenter);

    layout->addWidget(m_previewCard);

    m_previewError = new QLabel;
    m_previewError->setObjectName(QStringLiteral("FieldLabel"));
    m_previewError->setStyleSheet(QStringLiteral("color:#F87171; font-size:12.5px;"));
    m_previewError->setVisible(false);
    m_previewError->setWordWrap(true);
    layout->addWidget(m_previewError);
    layout->addSpacing(20);

    layout->addWidget(buildQueueSection());
    layout->addStretch(1);

    connect(pasteBtn, &QPushButton::clicked, this, [this]() {
        m_urlInput->setText(QApplication::clipboard()->text());
    });
    connect(m_analyzeButton, &QPushButton::clicked, this, &MainWindow::analyzeLink);
    connect(m_previewDownloadButton, &QPushButton::clicked, this, &MainWindow::startDownload);
    connect(m_folderField, &QLineEdit::returnPressed, this, [this]() {
        // no-op placeholder; a "..." browse button can call QFileDialog::getExistingDirectory
    });

    outerLayout->addWidget(column);
    outerLayout->addStretch(1);

    scroll->setWidget(content);
    return scroll;
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

    const QString resolution = m_resolutionCombo->currentText();
    const QString format = m_formatCombo->currentText();

    const QString id = m_downloadService->startDownload(url, resolution, format, outputDir);

    // Reuse whatever the preview card already knows (title + thumbnail from
    // the "Phân tích" step) so the new queue row isn't just a bare URL.
    const QString title = m_previewTitle->text().isEmpty() ? url : m_previewTitle->text();
    const QString meta = QStringLiteral("%1 • %2").arg(resolution, format);
    QPixmap thumb = m_previewThumb->pixmap();
    addQueueRow(id, title, meta, thumb);

    m_urlInput->clear();
    m_previewCard->setVisible(false);
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

// ---------------------------------------------------------------------
// Download queue: a live table of jobs below the options card (mirrors
// the reference layout's "Download Queue" list) instead of leaving that
// space empty. Rows are added when a download starts and updated in
// place from DownloadService's progress/completion/failure signals.
// ---------------------------------------------------------------------

QWidget *MainWindow::buildQueueSection()
{
    m_queueCard = new QFrame;
    m_queueCard->setObjectName(QStringLiteral("OptionsCard"));
    auto *cardLayout = new QVBoxLayout(m_queueCard);
    cardLayout->setContentsMargins(20, 16, 20, 16);
    cardLayout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    auto *headerTitle = new QLabel(QStringLiteral("Hàng đợi tải xuống"));
    headerTitle->setObjectName(QStringLiteral("SectionLabel"));
    headerRow->addWidget(headerTitle);
    headerRow->addStretch(1);
    cardLayout->addLayout(headerRow);

    m_queueEmptyLabel = new QLabel(QStringLiteral("Chưa có video nào đang tải. Dán liên kết và bấm \"Tải xuống\" để bắt đầu."));
    m_queueEmptyLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_queueEmptyLabel->setWordWrap(true);
    cardLayout->addWidget(m_queueEmptyLabel);

    auto *listContainer = new QWidget;
    m_queueListLayout = new QVBoxLayout(listContainer);
    m_queueListLayout->setContentsMargins(0, 0, 0, 0);
    m_queueListLayout->setSpacing(8);
    cardLayout->addWidget(listContainer);

    return m_queueCard;
}

void MainWindow::updateQueueEmptyState()
{
    if (m_queueEmptyLabel) {
        m_queueEmptyLabel->setVisible(m_queueRows.isEmpty());
    }
}

void MainWindow::addQueueRow(const QString &id, const QString &title, const QString &meta, const QPixmap &thumb)
{
    auto *row = new QFrame;
    row->setObjectName(QStringLiteral("QueueRow"));
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(10, 10, 10, 10);
    rowLayout->setSpacing(14);

    auto *thumbLabel = new QLabel;
    thumbLabel->setFixedSize(56, 40);
    thumbLabel->setScaledContents(true);
    thumbLabel->setStyleSheet(QStringLiteral("background:#0B111F; border-radius:6px;"));
    if (!thumb.isNull()) {
        thumbLabel->setPixmap(thumb);
    }
    rowLayout->addWidget(thumbLabel);

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(2);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("JobTitle"));
    titleLabel->setWordWrap(false);
    QFontMetrics fm(titleLabel->font());
    titleLabel->setText(fm.elidedText(title, Qt::ElideRight, 260));
    auto *metaLabel = new QLabel(meta);
    metaLabel->setObjectName(QStringLiteral("FieldLabel"));
    textCol->addWidget(titleLabel);
    textCol->addWidget(metaLabel);
    rowLayout->addLayout(textCol, 2);

    auto *progressBar = new QProgressBar;
    progressBar->setObjectName(QStringLiteral("QueueProgressBar"));
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(6);
    rowLayout->addWidget(progressBar, 3);

    auto *speedLabel = new QLabel(QStringLiteral("--"));
    speedLabel->setObjectName(QStringLiteral("FieldLabel"));
    speedLabel->setFixedWidth(70);
    rowLayout->addWidget(speedLabel);

    auto *statusLabel = new QLabel(QStringLiteral("Đang tải"));
    statusLabel->setObjectName(QStringLiteral("QueueStatusDownloading"));
    statusLabel->setFixedWidth(80);
    rowLayout->addWidget(statusLabel);

    auto *cancelBtn = new QPushButton;
    cancelBtn->setObjectName(QStringLiteral("QueueCancelBtn"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setIcon(QIcon(QStringLiteral(":/icons/alert-circle.svg")));
    cancelBtn->setToolTip(QStringLiteral("Hủy"));
    cancelBtn->setFixedSize(28, 28);
    connect(cancelBtn, &QPushButton::clicked, this, [this, id]() {
        m_downloadService->cancelDownload(id);
        removeQueueRow(id);
    });
    rowLayout->addWidget(cancelBtn);

    m_queueListLayout->addWidget(row);

    QueueRowWidgets widgets;
    widgets.row = row;
    widgets.thumb = thumbLabel;
    widgets.titleLabel = titleLabel;
    widgets.metaLabel = metaLabel;
    widgets.progressBar = progressBar;
    widgets.speedLabel = speedLabel;
    widgets.statusLabel = statusLabel;
    widgets.cancelBtn = cancelBtn;
    m_queueRows.insert(id, widgets);

    updateQueueEmptyState();
}

void MainWindow::updateQueueProgress(const QString &id, int percent, const QString &speed, const QString &eta)
{
    const auto it = m_queueRows.constFind(id);
    if (it == m_queueRows.constEnd()) {
        return;
    }
    const QueueRowWidgets &w = it.value();
    w.progressBar->setValue(percent);
    w.speedLabel->setText(speed.isEmpty() ? QStringLiteral("--") : speed);
    w.statusLabel->setText(eta.isEmpty()
        ? QStringLiteral("Đang tải")
        : QStringLiteral("Còn %1").arg(eta));
}

void MainWindow::markQueueCompleted(const QString &id)
{
    const auto it = m_queueRows.constFind(id);
    if (it == m_queueRows.constEnd()) {
        return;
    }
    const QueueRowWidgets &w = it.value();
    w.progressBar->setValue(100);
    w.speedLabel->setText(QStringLiteral("--"));
    w.statusLabel->setObjectName(QStringLiteral("QueueStatusDone"));
    w.statusLabel->setText(QStringLiteral("Hoàn thành"));
    w.statusLabel->style()->unpolish(w.statusLabel);
    w.statusLabel->style()->polish(w.statusLabel);
    w.cancelBtn->setEnabled(false);
    w.cancelBtn->setVisible(false);
}

void MainWindow::markQueueFailed(const QString &id, const QString &message)
{
    const auto it = m_queueRows.constFind(id);
    if (it == m_queueRows.constEnd()) {
        return;
    }
    const QueueRowWidgets &w = it.value();
    w.statusLabel->setObjectName(QStringLiteral("QueueStatusError"));
    w.statusLabel->setText(QStringLiteral("Lỗi"));
    w.statusLabel->setToolTip(message);
    w.statusLabel->style()->unpolish(w.statusLabel);
    w.statusLabel->style()->polish(w.statusLabel);
    w.speedLabel->setText(QStringLiteral("--"));
    w.cancelBtn->setEnabled(false);
    w.cancelBtn->setVisible(false);
}

void MainWindow::removeQueueRow(const QString &id)
{
    const auto it = m_queueRows.constFind(id);
    if (it == m_queueRows.constEnd()) {
        return;
    }
    it.value().row->deleteLater();
    m_queueRows.remove(id);
    updateQueueEmptyState();
}
