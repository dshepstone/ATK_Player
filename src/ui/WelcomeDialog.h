#pragma once

#include <QDialog>

namespace atk::ui {

/// Thank-you screen shown on the first launch after ATK Player is installed or
/// upgraded, and on demand from Help > Welcome to ATK Player.
///
/// It carries no state of its own; MainWindow decides when it appears and
/// records that it has been seen.
class WelcomeDialog final : public QDialog {
    Q_OBJECT

public:
    explicit WelcomeDialog(QWidget* parent = nullptr);

    static QString websiteUrl();
    static QString feedbackUrl();
};

} // namespace atk::ui
