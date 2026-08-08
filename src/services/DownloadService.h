#pragma once

#include <QObject>
#include <QString>
#include <QHash>

class QProcess;

// A single download job as tracked by the UI.
struct DownloadJob
{
    QString id;            // internal unique id
    QString url;
    QString title;
    QString resolution;    // e.g. "1080p (Full HD)"
    QString format;        // e.g. "MP4"
    QString outputDir;
    int     progress = 0;  // 0-100
    QString status;        // "downloading" | "completed" | "error" | "paused"
    QString filePath;      // populated once completed
};

// Thin wrapper around an external command-line downloader (yt-dlp.exe is the
// de-facto standard and is what this class shells out to). Keeping this
// behind an interface-like service means swapping the backend later
// (native engine, different CLI, a real download API) touches one file.
class DownloadService : public QObject
{
    Q_OBJECT

public:
    explicit DownloadService(QObject *parent = nullptr);
    ~DownloadService() override;

    // Starts a new download job and returns its generated id.
    // Requires yt-dlp(.exe) to be reachable (either on PATH or shipped
    // alongside VideoX.exe in a "tools/" folder next to the executable).
    QString startDownload(const QString &url,
                           const QString &resolution,
                           const QString &format,
                           const QString &outputDir);

    void pauseDownload(const QString &id);
    void cancelDownload(const QString &id);

    bool isEngineAvailable() const;
    QString engineNotFoundMessage() const;

signals:
    void progressChanged(const QString &id, int percent, const QString &speed, const QString &eta);
    void jobCompleted(const QString &id, const QString &filePath);
    void jobFailed(const QString &id, const QString &message);

private:
    QString resolveEnginePath() const;
    QStringList buildArguments(const DownloadJob &job) const;

    QHash<QString, QProcess *> m_processes;
    QHash<QString, DownloadJob> m_jobs;
};
