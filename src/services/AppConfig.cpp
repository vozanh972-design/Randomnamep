#include "AppConfig.h"

// NOTE: Replace these with your real production values.
// Keeping them in one file means a deployment change is a one-line edit,
// never a project-wide find & replace.

QString AppConfig::licenseApiUrl()
{
    return QStringLiteral("https://your-domain.example.com/api/verify_key.php");
}

QString AppConfig::purchaseUrl()
{
    return QStringLiteral("https://your-domain.example.com/mua-key");
}

QString AppConfig::supportUrl()
{
    return QStringLiteral("https://your-domain.example.com/ho-tro");
}

QString AppConfig::websiteUrl()
{
    return QStringLiteral("https://your-domain.example.com");
}

QString AppConfig::appVersion()
{
    return QStringLiteral("1.0.0");
}

QString AppConfig::orgName()
{
    return QStringLiteral("VideoX");
}

QString AppConfig::appName()
{
    return QStringLiteral("VideoX");
}
