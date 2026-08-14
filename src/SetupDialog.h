#pragma once

#include "Settings.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

// Reports whether the external pieces yt-dlp needs are actually present, and
// gives the exact command to fix each one that is not.
//
// This exists because the failure mode is otherwise indistinguishable from a
// bug in this program: listings work, downloads return 403, and nothing on
// screen explains why. Everything checked here lives outside the application,
// so it cannot be fixed by updating it.
class SetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SetupDialog(const Settings &settings, QWidget *parent = nullptr);

private:
    enum class Status { Checking, Good, Warning, Missing };

    struct Check {
        QString  title;
        QString  explanation;
        QString  fixCommand;      // shown with a Copy button when not Good
        QString  fixNote;
        Status   status = Status::Checking;
        QLabel  *statusLabel = nullptr;
        QLabel  *detailLabel = nullptr;
        QWidget *fixWidget = nullptr;
    };

    void buildUi();
    void addCheckRow(int index);
    void runChecks();
    void setResult(int index, Status status, const QString &detail);

    void checkYtDlp(int index);
    void checkRuntime(int index);
    void checkFfmpeg(int index);
    void checkChallengeSolver(int index);
    void checkTokenProvider(int index);

    Settings        m_settings;
    QVector<Check>  m_checks;
    QVBoxLayout    *m_rows = nullptr;
    QPushButton    *m_recheck = nullptr;
    QPlainTextEdit *m_output = nullptr;
    int             m_pending = 0;
};
