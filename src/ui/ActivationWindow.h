#pragma once

#include <QWidget>
#include "../services/LicenseService.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QVBoxLayout;
class TitleBar;

// The very first screen the user sees: license key entry.
// Purely presentational + input handling - all license logic is delegated
// to LicenseService.
class ActivationWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ActivationWindow(LicenseService *licenseService, QWidget *parent = nullptr);

signals:
    void activationCompleted(const LicenseInfo &info);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onActivateClicked();
    void onKeyTextChanged(const QString &text);

private:
    QWidget *buildLeftPanel();
    QWidget *buildRightPanel();
    QWidget *buildFeatureRow(const QString &iconPath, const QString &title, const QString &desc);

    void setLoadingState(bool loading);
    void showInlineError(const QString &message);
    void showInlineSuccess(const QString &message);
    void clearInlineMessage();

    LicenseService *m_licenseService;

    TitleBar *m_titleBar = nullptr;
    QLineEdit *m_keyInput = nullptr;
    QPushButton *m_activateButton = nullptr;
    QPushButton *m_purchaseButton = nullptr;
    QLabel *m_inlineMessageLabel = nullptr;
    QLabel *m_spinnerLabel = nullptr;
};
