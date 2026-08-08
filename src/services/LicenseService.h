#pragma once

#include <QObject>
#include <QString>
#include "../models/LicenseInfo.h"

class QNetworkAccessManager;
class QNetworkReply;

// Handles everything related to license keys:
//  - client-side format validation
//  - talking to the remote activation API
//  - persisting / loading / clearing the local license state
//
// This is the ONLY place that knows about the license API shape.
// UI code (ActivationWindow / MainWindow) only ever talks to this class.
class LicenseService : public QObject
{
    Q_OBJECT

public:
    explicit LicenseService(QObject *parent = nullptr);

    // Cheap, synchronous, client-side sanity check on the key format.
    // Mirrors the server-side pattern so we can give instant feedback
    // before ever hitting the network. Does NOT mean the key is valid.
    static bool validateKey(const QString &key);

    // Fires off the network activation request. Result arrives via
    // activationSucceeded()/activationFailed() signals (async by design,
    // so the UI can show a non-blocking loading state).
    void activateKey(const QString &key);

    // True if a previously-saved license is present and not expired.
    bool isActivated() const;

    // Reads whatever license state is currently stored on disk.
    LicenseInfo loadLicense() const;

    // Persists the given license state (encrypted-at-rest, not plaintext).
    void saveLicense(const LicenseInfo &info);

    // Wipes any stored license (used on hard errors / logout / device mismatch).
    void clearLicense();

signals:
    void activationStarted();
    void activationSucceeded(const LicenseInfo &info);
    void activationFailed(const QString &message);

private slots:
    void onNetworkReply(QNetworkReply *reply);

private:
    QString deviceId() const;
    QString obfuscate(const QString &plain) const;
    QString deobfuscate(const QString &stored) const;

    QNetworkAccessManager *m_network;
};
