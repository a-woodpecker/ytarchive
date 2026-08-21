#pragma once

#include "Settings.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
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
        QLineEdit *fixEdit = nullptr;    // so the command can be corrected later
        QLabel  *fixNoteLabel = nullptr;
        QLabel  *detailLabel = nullptr;
        QWidget *fixWidget = nullptr;
    };

    void buildUi();
    void addCheckRow(int index);
    void runChecks();
    void setResult(int index, Status status, const QString &detail);

    // Replaces a row's suggested command once we know how yt-dlp was installed.
    // A plugin has to land in the Python that yt-dlp actually runs, and for a
    // standalone build that is not any Python the user can pip into.
    void setFix(int index, const QString &command, const QString &note);

    // Parsed from yt-dlp's own -v output.
    QString m_ytDlpVariant;             // "pip", "win_exe", "zip", ...
    QStringList m_pluginDirectories;

    void checkYtDlp(int index);
    void checkRuntime(int index);
    void checkFfmpeg(int index);
    void readInstallDetails(const QString &verboseOutput);
    bool pluginsInstallViaPip() const;
    QString pluginDirectoryHint() const;
    void applyPluginFixes();

    static constexpr int kSolverIndex = 3;
    static constexpr int kTokenIndex = 4;

    void checkChallengeSolver(int index);
    void checkTokenProvider(int index);

    Settings        m_settings;
    QVector<Check>  m_checks;
    QVBoxLayout    *m_rows = nullptr;
    QPushButton    *m_recheck = nullptr;
    QPlainTextEdit *m_output = nullptr;
    int             m_pending = 0;
};
