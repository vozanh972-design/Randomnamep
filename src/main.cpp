#include <QApplication>
#include <QFile>
#include <QFontDatabase>

#include "ui/ActivationWindow.h"
#include "ui/MainWindow.h"
#include "services/LicenseService.h"
#include "services/AppConfig.h"

static void loadStylesheet(QApplication &app)
{
    QFile file(QStringLiteral(":/styles/styles.qss"));
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
    }
}

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(AppConfig::orgName());
    QApplication::setApplicationName(AppConfig::appName());
    QApplication::setApplicationVersion(AppConfig::appVersion());

    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Segoe UI"), 10));

    loadStylesheet(app);

    auto *licenseService = new LicenseService(&app);

    // Persistent activation: if we already hold a valid license, skip
    // straight to MainWindow. Otherwise show the activation screen.
    if (licenseService->isActivated()) {
        auto *mainWindow = new MainWindow(licenseService);
        mainWindow->show();
    } else {
        auto *activationWindow = new ActivationWindow(licenseService);

        QObject::connect(activationWindow, &ActivationWindow::activationCompleted,
                          &app, [licenseService, activationWindow](const LicenseInfo &) {
            auto *mainWindow = new MainWindow(licenseService);
            mainWindow->show();
            activationWindow->close();
            activationWindow->deleteLater();
        });

        activationWindow->show();
    }

    return app.exec();
}
