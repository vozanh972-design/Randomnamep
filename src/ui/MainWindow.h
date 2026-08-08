#pragma once

#include <QWidget>
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
class TitleBar;
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

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *buildSidebar();
    QWidget *buildContent();
    QWidget *buildRightPane();
    QPushButton *makeNavButton(const QString &iconPath, const QString &text, const QString &badge = QString());
    QWidget *makePlatformChip(const QString &iconPath, const QString &label);

    void startDownload();
    void analyzeLink();
    void showVideoInfo(const VideoInfo &info);
    void showAnalyzeError(const QString &message);

    LicenseService *m_licenseService;
    DownloadService *m_downloadService;

    QFrame *m_root = nullptr;
    TitleBar *m_titleBar = nullptr;
    QLineEdit *m_urlInput = nullptr;
    QComboBox *m_resolutionCombo = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QLineEdit *m_folderField = nullptr;
    QVBoxLayout *m_activeListLayout = nullptr;
    QVBoxLayout *m_completedListLayout = nullptr;

    // "Phân tích" (analyze) preview: shows title/channel/upload
    // date/duration/thumbnail for the pasted link before downloading.
    // Works for any yt-dlp-supported site, not just YouTube.
    QPushButton *m_analyzeButton = nullptr;
    QFrame *m_previewCard = nullptr;
    QLabel *m_previewThumb = nullptr;
    QLabel *m_previewTitle = nullptr;
    QLabel *m_previewMeta = nullptr;
    QLabel *m_previewError = nullptr;
    QNetworkAccessManager *m_thumbNetwork = nullptr;
};
