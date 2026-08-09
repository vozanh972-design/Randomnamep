#pragma once

#include <QWidget>
#include <QHash>
#include "../services/LicenseService.h"
#include "../services/DownloadService.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QVBoxLayout;
class QComboBox;
class QFrame;
class QResizeEvent;
class QNetworkAccessManager;
class QProgressBar;
struct VideoInfo;

// Main application shell shown once a valid license is active.
// This implements the "Tải Video" (download) view faithfully; the other
// nav destinations (Trình chỉnh sửa, Lịch sử, Công cụ, Cài đặt) are wired
// as placeholders ready to be filled in with their own views.
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(LicenseService *licenseService, QWidget *parent = nullptr);

private:
    // One live row in the "Hàng đợi tải xuống" queue table below the
    // options card (mirrors the reference's Download Queue list) --
    // pointers into the row's own widgets so progress/status updates can
    // touch just that row instead of rebuilding the whole list.
    struct QueueRowWidgets
    {
        QFrame *row = nullptr;
        QLabel *thumb = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *metaLabel = nullptr;
        QProgressBar *progressBar = nullptr;
        QLabel *speedLabel = nullptr;
        QLabel *statusLabel = nullptr;
        QPushButton *cancelBtn = nullptr;
    };

    QWidget *buildSidebar();
    QWidget *buildContent();
    QWidget *buildQueueSection();
    QPushButton *makeNavButton(const QString &iconPath, const QString &text, const QString &badge = QString());
    QWidget *makePlatformIcon(const QString &iconPath, const QString &label, const QString &bgColor);

    void startDownload();
    void analyzeLink();
    void showVideoInfo(const VideoInfo &info);
    void showAnalyzeError(const QString &message);

    void addQueueRow(const QString &id, const QString &title, const QString &meta, const QPixmap &thumb);
    void updateQueueProgress(const QString &id, int percent, const QString &speed, const QString &eta);
    void markQueueCompleted(const QString &id);
    void markQueueFailed(const QString &id, const QString &message);
    void removeQueueRow(const QString &id);
    void updateQueueEmptyState();

    LicenseService *m_licenseService;
    DownloadService *m_downloadService;

    QLineEdit *m_urlInput = nullptr;
    QComboBox *m_resolutionCombo = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QLineEdit *m_folderField = nullptr;

    // "Phân tích" (analyze) preview: shows title/channel/upload
    // date/duration/thumbnail for the pasted link before downloading.
    // Works for any yt-dlp-supported site, not just YouTube. The actual
    // "Tải xuống" action now lives on this card (see m_previewDownloadButton)
    // instead of as a separate always-visible button on the page -- there's
    // nothing to download until a link has been analyzed, so the button
    // only appears once there is.
    QPushButton *m_analyzeButton = nullptr;
    QFrame *m_previewCard = nullptr;
    QLabel *m_previewThumb = nullptr;
    QLabel *m_previewTitle = nullptr;
    QLabel *m_previewMeta = nullptr;
    QPushButton *m_previewDownloadButton = nullptr;
    QLabel *m_previewError = nullptr;
    QNetworkAccessManager *m_thumbNetwork = nullptr;

    // Download queue (below the options card): one row per active/finished
    // job, live-updated from DownloadService's progress/completion signals.
    QFrame *m_queueCard = nullptr;
    QVBoxLayout *m_queueListLayout = nullptr;
    QLabel *m_queueEmptyLabel = nullptr;
    QHash<QString, QueueRowWidgets> m_queueRows;
};
