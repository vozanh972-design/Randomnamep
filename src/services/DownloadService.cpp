#include "DownloadService.h"

#include <QProcess>
#include <QUuid>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

DownloadService::DownloadService(QObject *parent)
    : QObject(parent)
{
}

DownloadService::~DownloadService()
{
    qDeleteAll(m_processes);
}

QString DownloadService::resolveEnginePath() const
{
    // 1) Shipped next to the exe: <appdir>/tools/yt-dlp.exe
    const QString bundled = QCoreApplication::applicationDirPath() + "/tools/yt-dlp.exe";
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    // 2) Available on PATH
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (!onPath.isEmpty()) {
        return onPath;
    }

    return QString();
}

bool DownloadService::isEngineAvailable() const
{
    return !resolveEnginePath().isEmpty();
}

QString DownloadService::engineNotFoundMessage() const
{
    return QStringLiteral(
        "Không tìm thấy công cụ tải video (yt-dlp.exe). "
        "Vui lòng đặt yt-dlp.exe vào thư mục \"tools\" cạnh VideoX.exe, "
        "hoặc thêm vào biến môi trường PATH.");
}

QStringList DownloadService::buildArguments(const DownloadJob &job) const
{
    QStringList args;

    // Resolution -> yt-dlp format selector
    QString heightLimit;
    const QRegularExpression heightRe(QStringLiteral("(\\d+)p"));
    const auto match = heightRe.match(job.resolution);
    if (match.hasMatch()) {
        heightLimit = match.captured(1);
    }

    QString formatSelector = heightLimit.isEmpty()
        ? QStringLiteral("bestvideo+bestaudio/best")
        : QStringLiteral("bestvideo[height<=%1]+bestaudio/best[height<=%1]").arg(heightLimit);

    args << QStringLiteral("-f") << formatSelector;

    if (job.format.compare(QStringLiteral("MP4"), Qt::CaseInsensitive) == 0) {
        args << QStringLiteral("--merge-output-format") << QStringLiteral("mp4");
    }

    args << QStringLiteral("-o")
         << (QDir(job.outputDir).filePath(QStringLiteral("%(title)s.%(ext)s")));

    args << QStringLiteral("--newline"); // makes progress parsing line-based
    args << job.url;

    return args;
}

QString DownloadService::startDownload(const QString &url,
                                        const QString &resolution,
                                        const QString &format,
                                        const QString &outputDir)
{
    const QString enginePath = resolveEnginePath();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    DownloadJob job;
    job.id = id;
    job.url = url;
    job.resolution = resolution;
    job.format = format;
    job.outputDir = outputDir;
    job.status = QStringLiteral("downloading");
    m_jobs.insert(id, job);

    if (enginePath.isEmpty()) {
        emit jobFailed(id, engineNotFoundMessage());
        return id;
    }

    QDir().mkpath(outputDir);

    auto *process = new QProcess(this);
    process->setProgram(enginePath);
    process->setArguments(buildArguments(job));

    connect(process, &QProcess::readyReadStandardOutput, this, [this, id, process]() {
        const QString chunk = QString::fromUtf8(process->readAllStandardOutput());
        static const QRegularExpression progressRe(
            QStringLiteral(R"(\[download\]\s+(\d{1,3}\.\d)%.*?at\s+([^\s]+).*?ETA\s+([^\s]+))"));
        const auto m = progressRe.match(chunk);
        if (m.hasMatch()) {
            const int percent = static_cast<int>(m.captured(1).toDouble());
            emit progressChanged(id, percent, m.captured(2), m.captured(3));
        }
    });

    connect(process, &QProcess::finished, this, [this, id, process](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            emit jobCompleted(id, m_jobs.value(id).outputDir);
        } else {
            emit jobFailed(id, QStringLiteral("Tải xuống thất bại (mã lỗi %1).").arg(exitCode));
        }
        process->deleteLater();
        m_processes.remove(id);
    });

    m_processes.insert(id, process);
    process->start();

    return id;
}

void DownloadService::pauseDownload(const QString &id)
{
    // yt-dlp has no native pause/resume over a running process; the pragmatic
    // approach is to terminate and rely on yt-dlp's own partial-file resume
    // (re-running the same command will continue an interrupted download).
    if (auto *p = m_processes.value(id, nullptr)) {
        p->terminate();
    }
}

void DownloadService::cancelDownload(const QString &id)
{
    if (auto *p = m_processes.value(id, nullptr)) {
        p->kill();
        m_processes.remove(id);
    }
    m_jobs.remove(id);
}
