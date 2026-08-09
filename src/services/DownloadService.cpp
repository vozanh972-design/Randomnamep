#include "DownloadService.h"

#include <QProcess>
#include <QUuid>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDate>
#include <QSharedPointer>
#include <QTimer>

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
    // 1) Right next to the exe: <appdir>/yt-dlp.exe
    //    (most common place people actually drop it -- checked first).
    const QString sameDir = QCoreApplication::applicationDirPath() + "/yt-dlp.exe";
    if (QFileInfo::exists(sameDir)) {
        return sameDir;
    }

    // 2) In a "tools" subfolder next to the exe: <appdir>/tools/yt-dlp.exe
    const QString bundled = QCoreApplication::applicationDirPath() + "/tools/yt-dlp.exe";
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    // 3) Available on PATH
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
        "Vui lòng đặt yt-dlp.exe cùng thư mục với LunexReDown.exe "
        "(hoặc trong thư mục con \"tools\"), hoặc thêm vào biến môi trường PATH.");
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

namespace
{
QString formatDuration(double seconds)
{
    if (seconds <= 0) {
        return QString();
    }
    const int total = static_cast<int>(seconds);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

QString formatUploadDate(const QString &yyyymmdd)
{
    // yt-dlp reports upload_date as "YYYYMMDD".
    const QDate date = QDate::fromString(yyyymmdd, QStringLiteral("yyyyMMdd"));
    return date.isValid() ? date.toString(QStringLiteral("dd/MM/yyyy")) : QString();
}
}

void DownloadService::fetchVideoInfo(const QString &url)
{
    const QString enginePath = resolveEnginePath();
    if (enginePath.isEmpty()) {
        emit videoInfoFailed(engineNotFoundMessage());
        return;
    }
    if (url.trimmed().isEmpty()) {
        emit videoInfoFailed(QStringLiteral("Vui lòng dán một liên kết video trước."));
        return;
    }

    // --skip-download: only fetch metadata, never touch the media itself.
    // --dump-single-json: one JSON object on stdout, works for single videos.
    // --no-playlist only short-circuits a *video* URL that happens to carry
    // a playlist id (e.g. a YouTube watch link opened from inside a
    // playlist) -- it does nothing for a URL that is itself a playlist,
    // channel, or profile page. Without a limit, yt-dlp would walk the
    // entire channel gathering full metadata for every video before
    // "--dump-single-json" could return anything, which is what made
    // pasting a profile link sit there and never finish. --playlist-items 1
    // caps that walk to just the first entry so a profile link resolves
    // (as a preview of its most recent video) instead of hanging.
    QStringList args;
    args << QStringLiteral("--skip-download")
         << QStringLiteral("--no-playlist")
         << QStringLiteral("--playlist-items") << QStringLiteral("1")
         << QStringLiteral("--dump-single-json")
         << url.trimmed();

    auto *process = new QProcess(this);
    process->setProgram(enginePath);
    process->setArguments(args);

    auto *stdoutBuf = new QByteArray();
    auto handled = QSharedPointer<bool>::create(false);

    // Safety net: even capped to one item, a slow/unreachable site
    // shouldn't be able to leave the "Đang phân tích..." button stuck
    // forever. Kill the process and report a clear reason instead.
    auto *timeoutTimer = new QTimer(process);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(25000);

    connect(process, &QProcess::readyReadStandardOutput, this, [process, stdoutBuf]() {
        stdoutBuf->append(process->readAllStandardOutput());
    });

    auto finishOnce = [this, process, stdoutBuf, handled, timeoutTimer](
                           bool timedOut, int exitCode, QByteArray output) {
        // output is taken by value: it must be a copy of *stdoutBuf, not a
        // reference to it, since stdoutBuf is freed a few lines down and
        // output is still read after that (exitCode/isEmpty checks, JSON
        // parsing) -- a reference here would dangle.
        if (*handled) {
            return;
        }
        *handled = true;
        timeoutTimer->stop();
        delete stdoutBuf;
        process->deleteLater();

        if (timedOut) {
            emit videoInfoFailed(QStringLiteral(
                "Liên kết này mất quá nhiều thời gian để phân tích (có thể đây là link "
                "kênh/trang cá nhân có rất nhiều video). Vui lòng dán link của một video cụ thể."));
            return;
        }

        if (exitCode != 0 || output.isEmpty()) {
            emit videoInfoFailed(QStringLiteral(
                "Không phân tích được liên kết này. Kiểm tra lại URL hoặc thử liên kết khác."));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(output);
        if (!doc.isObject()) {
            emit videoInfoFailed(QStringLiteral("Phản hồi phân tích không hợp lệ."));
            return;
        }

        QJsonObject obj = doc.object();

        // A bare channel/profile URL with --playlist-items 1 can still come
        // back as a playlist wrapper (_type: "playlist") holding a single
        // entry, rather than the video object itself -- unwrap it so the
        // preview shows that first video instead of failing to find a title.
        if (obj.value(QStringLiteral("_type")).toString() == QStringLiteral("playlist")) {
            const QJsonArray entries = obj.value(QStringLiteral("entries")).toArray();
            if (entries.isEmpty()) {
                emit videoInfoFailed(QStringLiteral(
                    "Đây có vẻ là link kênh/trang cá nhân không có video nào để phân tích."));
                return;
            }
            obj = entries.first().toObject();
        }

        VideoInfo info;
        info.title       = obj.value(QStringLiteral("title")).toString();
        info.uploader     = obj.value(QStringLiteral("uploader")).toString();
        if (info.uploader.isEmpty()) {
            info.uploader = obj.value(QStringLiteral("channel")).toString();
        }
        info.uploadDate   = formatUploadDate(obj.value(QStringLiteral("upload_date")).toString());
        info.durationText = formatDuration(obj.value(QStringLiteral("duration")).toDouble());
        info.thumbnailUrl = obj.value(QStringLiteral("thumbnail")).toString();
        info.platform     = obj.value(QStringLiteral("extractor_key")).toString();

        if (info.title.isEmpty()) {
            emit videoInfoFailed(QStringLiteral("Không đọc được thông tin video từ liên kết này."));
            return;
        }

        emit videoInfoReady(info);
    };

    connect(process, &QProcess::finished, this,
            [stdoutBuf, finishOnce](int exitCode, QProcess::ExitStatus) {
        finishOnce(false, exitCode, *stdoutBuf);
    });

    connect(process, &QProcess::errorOccurred, this, [finishOnce](QProcess::ProcessError) {
        finishOnce(false, -1, QByteArray());
    });

    connect(timeoutTimer, &QTimer::timeout, this, [process, finishOnce]() {
        process->kill();
        finishOnce(true, -1, QByteArray());
    });

    timeoutTimer->start();
    process->start();
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
