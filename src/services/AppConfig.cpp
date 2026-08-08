#include "AppConfig.h"

// NOTE: Replace these with your real production values.
// Keeping them in one file means a deployment change is a one-line edit,
// never a project-wide find & replace.

QString AppConfig::licenseApiUrl()
{
    return QStringLiteral("https://lunex.io.vn/api/verify_key.php");
}

QString AppConfig::purchaseUrl()
{
    return QStringLiteral("https://lunex.io.vn");
}

QString AppConfig::supportUrl()
{
    return QStringLiteral("https://zalo.me/0931006827");
}

QString AppConfig::websiteUrl()
{
    return QStringLiteral("https://lunex.io.vn");
}

QString AppConfig::appVersion()
{
    return QStringLiteral("1.0.0");
}

QString AppConfig::orgName()
{
    return QStringLiteral("Lunex ReDown");
}

QString AppConfig::appName()
{
    return QStringLiteral("Lunex ReDown");
}
