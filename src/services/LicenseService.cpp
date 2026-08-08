#include "LicenseService.h"
#include "AppConfig.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSysInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QByteArray>

namespace
{
constexpr auto kSettingsGroup   = "license";
constexpr auto kKeyKey          = "key";
constexpr auto kPackageKey      = "package";
constexpr auto kMaxDaysKey      = "maxDays";
constexpr auto kDaysLeftKey     = "daysLeft";
constexpr auto kExpiresAtKey    = "expiresAt";
constexpr auto kActivatedKey    = "activated";

// Mirrors the server pattern: LUNEX-XXXXXX-XXXXXX-XXXXXX-P30D / B30D
const QRegularExpression kKeyPattern(
    QStringLiteral(R"(^LUNEX-[a-zA-Z0-9]{6}-[a-zA-Z0-9]{6}-[a-zA-Z0-9]{6}-(P|B)(\d+)D$)"),
    QRegularExpression::CaseInsensitiveOption);
}

LicenseService::LicenseService(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    connect(m_network, &QNetworkAccessManager::finished, this, &LicenseService::onNetworkReply);
}

bool LicenseService::validateKey(const QString &key)
{
    return kKeyPattern.match(key.trimmed()).hasMatch();
}

QString LicenseService::deviceId() const
{
    // Stable per-machine identifier, hashed so we never transmit raw hardware info.
    const QByteArray raw = QSysInfo::machineUniqueId();
    const QByteArray source = raw.isEmpty()
        ? QSysInfo::machineHostName().toUtf8()
        : raw;
    return QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
}

void LicenseService::activateKey(const QString &key)
{
    const QString trimmed = key.trimmed();

    if (!validateKey(trimmed)) {
        emit activationFailed(QStringLiteral("Key không hợp lệ hoặc đã hết hạn."));
        return;
    }

    emit activationStarted();

    QUrl url(AppConfig::licenseApiUrl());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), trimmed);
    query.addQueryItem(QStringLiteral("device_id"), deviceId());
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_network->get(request);
    reply->setProperty("videox_request_key", trimmed);
}

void LicenseService::onNetworkReply(QNetworkReply *reply)
{
    reply->deleteLater();

    const QString requestedKey = reply->property("videox_request_key").toString();

    if (reply->error() != QNetworkReply::NoError) {
        emit activationFailed(QStringLiteral("Không thể kết nối máy chủ. Vui lòng thử lại."));
        return;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        emit activationFailed(QStringLiteral("Phản hồi từ máy chủ không hợp lệ."));
        return;
    }

    const QJsonObject obj = doc.object();
    const QString status = obj.value(QStringLiteral("status")).toString();

    if (status != QStringLiteral("success")) {
        const QString message = obj.value(QStringLiteral("message")).toString();
        emit activationFailed(message.isEmpty()
            ? QStringLiteral("Key không hợp lệ hoặc đã hết hạn.")
            : message);
        return;
    }

    LicenseInfo info;
    info.key       = requestedKey;
    info.package   = obj.value(QStringLiteral("package")).toString();
    info.maxDays   = obj.value(QStringLiteral("max_days")).toInt();
    info.daysLeft  = obj.value(QStringLiteral("days_left")).toInt();

    const qint64 expireTs = obj.value(QStringLiteral("expire_ts")).toVariant().toLongLong();
    if (expireTs > 0) {
        info.expiresAt = QDateTime::fromSecsSinceEpoch(expireTs, Qt::UTC);
    }
    info.activated = true;

    saveLicense(info);
    emit activationSucceeded(info);
}

bool LicenseService::isActivated() const
{
    return loadLicense().isValid();
}

QString LicenseService::obfuscate(const QString &plain) const
{
    // Lightweight at-rest obfuscation so the key isn't sitting around in
    // plaintext inside the settings file. For production-grade secrecy,
    // swap this for Windows Credential Manager (CredWriteW/CredReadW) --
    // this method keeps the same call sites so that swap is a one-file change.
    QByteArray data = plain.toUtf8();
    const QByteArray salt = QSysInfo::machineUniqueId();
    for (int i = 0; i < data.size(); ++i) {
        data[i] = data[i] ^ (salt.isEmpty() ? 0x5A : salt.at(i % salt.size()));
    }
    return QString::fromLatin1(data.toBase64());
}

QString LicenseService::deobfuscate(const QString &stored) const
{
    QByteArray data = QByteArray::fromBase64(stored.toLatin1());
    const QByteArray salt = QSysInfo::machineUniqueId();
    for (int i = 0; i < data.size(); ++i) {
        data[i] = data[i] ^ (salt.isEmpty() ? 0x5A : salt.at(i % salt.size()));
    }
    return QString::fromUtf8(data);
}

LicenseInfo LicenseService::loadLicense() const
{
    QSettings settings(AppConfig::orgName(), AppConfig::appName());
    settings.beginGroup(kSettingsGroup);

    LicenseInfo info;
    const QString storedKey = settings.value(kKeyKey).toString();
    if (storedKey.isEmpty()) {
        settings.endGroup();
        return info; // default-constructed, activated == false
    }

    info.key       = deobfuscate(storedKey);
    info.package   = settings.value(kPackageKey).toString();
    info.maxDays   = settings.value(kMaxDaysKey).toInt();
    info.daysLeft  = settings.value(kDaysLeftKey).toInt();
    info.expiresAt = settings.value(kExpiresAtKey).toDateTime();
    info.activated = settings.value(kActivatedKey, false).toBool();

    settings.endGroup();
    return info;
}

void LicenseService::saveLicense(const LicenseInfo &info)
{
    QSettings settings(AppConfig::orgName(), AppConfig::appName());
    settings.beginGroup(kSettingsGroup);

    settings.setValue(kKeyKey, obfuscate(info.key));
    settings.setValue(kPackageKey, info.package);
    settings.setValue(kMaxDaysKey, info.maxDays);
    settings.setValue(kDaysLeftKey, info.daysLeft);
    settings.setValue(kExpiresAtKey, info.expiresAt);
    settings.setValue(kActivatedKey, info.activated);

    settings.endGroup();
    settings.sync();
}

void LicenseService::clearLicense()
{
    QSettings settings(AppConfig::orgName(), AppConfig::appName());
    settings.beginGroup(kSettingsGroup);
    settings.remove(QString());
    settings.endGroup();
    settings.sync();
}
