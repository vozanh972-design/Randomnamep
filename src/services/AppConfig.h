#pragma once

#include <QString>

// Centralised, single source of truth for every external URL / constant
// the app needs. Nothing outside this file should hard-code a URL.
class AppConfig
{
public:
    static QString licenseApiUrl();     // Endpoint that validates/activates a key
    static QString purchaseUrl();       // Where "Mua key bản quyền" opens
    static QString supportUrl();        // "Hỗ trợ"
    static QString websiteUrl();        // "Website"
    static QString appVersion();        // Shown in the footer

    // Organisation / app identity, used for QSettings + credential storage.
    static QString orgName();
    static QString appName();
};
