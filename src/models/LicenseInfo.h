#pragma once

#include <QString>
#include <QDateTime>

// Plain-data model describing the current license state.
// Kept free of any networking / persistence logic on purpose.
struct LicenseInfo
{
    QString key;                 // Full license key, e.g. LUNEX-XXXXXX-XXXXXX-XXXXXX-P30D
    QString package;             // "PRO" or "BASIC"
    int     maxDays = 0;         // Total validity length in days
    int     daysLeft = 0;        // Days remaining as of last check
    QDateTime expiresAt;         // Absolute expiry timestamp (server time)
    bool    activated = false;   // Whether this license is currently considered valid/active

    bool isValid() const
    {
        return activated && !key.isEmpty() && (expiresAt.isNull() || QDateTime::currentDateTimeUtc() < expiresAt);
    }
};
