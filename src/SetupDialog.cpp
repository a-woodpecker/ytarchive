#include "SetupDialog.h"

#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// yt-dlp's own long-standing test video, used only to make the extractor run
// far enough to report which providers are loaded.
const char *kProbeUrl = "https://www.youtube.com/watch?v=BaW_jenozKc";

QString statusText(int status)
{
    switch (status) {
    case 0: return QCoreApplication::translate("SetupDialog", "checking...");
    case 1: return QCoreApplication::translate("SetupDialog", "OK");
    case 2: return QCoreApplication::translate("SetupDialog", "WARNING");
    default: return QCoreApplication::translate("SetupDialog", "MISSING");
    }
}

} // namespace

SetupDialog::SetupDialog(const Settings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Download support"));
    resize(760, 620);

    m_checks = {
        { tr("yt-dlp"),
          tr("Performs every listing and download."),
          QStringLiteral("pipx upgrade yt-dlp"),
          tr("Distribution packages lag badly; pipx keeps it current.") },

        { tr("JavaScript runtime"),
          tr("Required to sign media URLs. Without it, listings work and every "
             "download fails with HTTP 403."),
          QStringLiteral("curl -fsSL https://deno.land/install.sh | sh"),
          tr("Only deno is used automatically. To use node instead, install it and "
             "put \"node\" in Preferences > Locations > JavaScript runtime.") },

        { tr("ffmpeg"),
          tr("Merges the separate video and audio streams. A download runs to "
             "completion and then fails at the merge without it."),
          QStringLiteral("sudo apt install ffmpeg"),
          tr("ffprobe must sit beside ffmpeg; every build ships both.") },

        { tr("JavaScript challenge solver"),
          tr("Computes the signatures that turn the service's format entries into "
             "usable URLs. Without it, downloads that need it report \"Only images "
             "are available\" and then fail as though no format matched."),
          QStringLiteral("pipx inject yt-dlp yt-dlp-ejs"),
          tr("Installs the solver alongside yt-dlp, so nothing is fetched at run "
             "time. Alternatively, enable \"Allow yt-dlp to download its challenge "
             "solver\" under Preferences > Downloading.") },

        { tr("PO token provider"),
          tr("Supplies the proof-of-origin tokens the service now demands for "
             "media URLs. Without one, downloads can fail with HTTP 403 part way "
             "through, at full speed, on videos that play fine in a browser."),
          QStringLiteral("pipx inject yt-dlp bgutil-ytdlp-pot-provider"),
          tr("Then set up the generator it talks to. The script option needs only "
             "the JavaScript runtime above - no Docker and no background service. "
             "See the README section on HTTP 403.") },
    };

    buildUi();
    runChecks();
}

void SetupDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("This program drives external tools rather than reimplementing them. "
           "Everything below lives outside the application and has to be installed "
           "separately; when one is missing, downloads fail in ways that look like "
           "faults here."), this);
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("hint"));
    layout->addWidget(intro);

    m_rows = new QVBoxLayout;
    m_rows->setSpacing(14);
    layout->addLayout(m_rows);

    for (int i = 0; i < m_checks.size(); ++i)
        addCheckRow(i);

    layout->addSpacing(6);
    auto *outputLabel = new QLabel(tr("Details"), this);
    outputLabel->setObjectName(QStringLiteral("hint"));
    layout->addWidget(outputLabel);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(500);
    m_output->setMinimumHeight(110);
    layout->addWidget(m_output, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_recheck = buttons->addButton(tr("Check again"), QDialogButtonBox::ActionRole);
    connect(m_recheck, &QPushButton::clicked, this, &SetupDialog::runChecks);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}

void SetupDialog::addCheckRow(int index)
{
    Check &check = m_checks[index];
    const ThemePalette &t = Theme::palette();

    auto *row = new QWidget(this);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(3);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(check.title), row);
    header->addWidget(title);
    header->addStretch();

    check.statusLabel = new QLabel(statusText(0), row);
    check.statusLabel->setStyleSheet(QStringLiteral("color:%1").arg(t.meta.name()));
    header->addWidget(check.statusLabel);
    rowLayout->addLayout(header);

    auto *explanation = new QLabel(check.explanation, row);
    explanation->setWordWrap(true);
    explanation->setObjectName(QStringLiteral("hint"));
    rowLayout->addWidget(explanation);

    check.detailLabel = new QLabel(QString(), row);
    check.detailLabel->setWordWrap(true);
    check.detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rowLayout->addWidget(check.detailLabel);

    // The fix is a selectable command with a Copy button rather than something
    // this program runs itself: installing software on someone's machine
    // without them seeing the command first is not a decision to take quietly.
    check.fixWidget = new QWidget(row);
    auto *fixLayout = new QVBoxLayout(check.fixWidget);
    fixLayout->setContentsMargins(0, 4, 0, 0);
    fixLayout->setSpacing(3);

    auto *commandRow = new QHBoxLayout;
    auto *command = new QLineEdit(check.fixCommand, check.fixWidget);
    command->setReadOnly(true);
    command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    commandRow->addWidget(command, 1);

    auto *copy = new QPushButton(tr("Copy"), check.fixWidget);
    const QString commandText = check.fixCommand;
    connect(copy, &QPushButton::clicked, this, [this, commandText] {
        QApplication::clipboard()->setText(commandText);
        m_output->appendPlainText(tr("Copied: %1").arg(commandText));
    });
    commandRow->addWidget(copy);
    fixLayout->addLayout(commandRow);

    if (!check.fixNote.isEmpty()) {
        auto *note = new QLabel(check.fixNote, check.fixWidget);
        note->setWordWrap(true);
        note->setObjectName(QStringLiteral("hint"));
        fixLayout->addWidget(note);
    }

    check.fixWidget->hide();
    rowLayout->addWidget(check.fixWidget);

    auto *line = new QFrame(row);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("color:%1").arg(t.cardHover.name()));
    rowLayout->addWidget(line);

    m_rows->addWidget(row);
}

void SetupDialog::setResult(int index, Status status, const QString &detail)
{
    Check &check = m_checks[index];
    check.status = status;

    const ThemePalette &t = Theme::palette();
    QColor colour = t.meta;
    switch (status) {
    case Status::Good:    colour = t.archived; break;
    case Status::Warning: colour = QColor(0xC8, 0x8A, 0x00); break;
    case Status::Missing: colour = t.failed; break;
    case Status::Checking: break;
    }

    check.statusLabel->setText(statusText(static_cast<int>(status)));
    check.statusLabel->setStyleSheet(
        QStringLiteral("color:%1; font-weight:bold").arg(colour.name()));
    check.detailLabel->setText(detail);
    check.fixWidget->setVisible(status != Status::Good);

    if (--m_pending <= 0)
        m_recheck->setEnabled(true);
}

void SetupDialog::runChecks()
{
    m_recheck->setEnabled(false);
    m_output->clear();
    m_pending = m_checks.size();

    for (int i = 0; i < m_checks.size(); ++i)
        setResult(i, Status::Checking, QString());
    m_pending = m_checks.size();

    checkYtDlp(0);
    checkRuntime(1);
    checkFfmpeg(2);
    checkChallengeSolver(3);
    checkTokenProvider(4);
}

void SetupDialog::checkYtDlp(int index)
{
    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(toolProcessEnvironment());
    proc->setProgram(m_settings.ytDlpPath);
    proc->setArguments({ QStringLiteral("--version") });

    connect(proc, &QProcess::errorOccurred, this, [this, index, proc] {
        setResult(index, Status::Missing,
                  tr("Not found at \"%1\".").arg(m_settings.ytDlpPath));
        proc->deleteLater();
    });
    connect(proc, &QProcess::finished, this, [this, index, proc] {
        const QString version = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        proc->deleteLater();
        if (version.isEmpty()) {
            setResult(index, Status::Missing, tr("No version reported."));
            return;
        }
        const QString path = findToolExecutable(QStringLiteral("yt-dlp"));
        m_output->appendPlainText(tr("yt-dlp %1 at %2").arg(version, path));

        const QDate released = QDate::fromString(version.left(10),
                                                 QStringLiteral("yyyy.MM.dd"));
        const qint64 age = released.isValid() ? released.daysTo(QDate::currentDate()) : -1;
        if (age > 60) {
            setResult(index, Status::Warning,
                      tr("Version %1, released %n day(s) ago.", "", static_cast<int>(age))
                          .arg(version));
        } else {
            setResult(index, Status::Good, tr("Version %1").arg(version));
        }
    });
    proc->start();
}

void SetupDialog::checkRuntime(int index)
{
    const QStringList names{ QStringLiteral("deno"), QStringLiteral("node"),
                             QStringLiteral("bun"), QStringLiteral("qjs") };
    QStringList found;
    QString denoPath;
    for (const QString &name : names) {
        const QString path = findToolExecutable(name);
        if (path.isEmpty())
            continue;
        found << name;
        if (name == QLatin1String("deno"))
            denoPath = path;
    }

    if (found.isEmpty()) {
        setResult(index, Status::Missing, tr("None of deno, node, bun or qjs was found."));
        return;
    }
    if (!denoPath.isEmpty()) {
        m_output->appendPlainText(tr("JavaScript runtime: %1").arg(denoPath));
        setResult(index, Status::Good, denoPath);
        return;
    }
    if (!m_settings.jsRuntimes.trimmed().isEmpty()) {
        setResult(index, Status::Good,
                  tr("Found %1, configured as \"%2\".")
                      .arg(found.join(QStringLiteral(", ")), m_settings.jsRuntimes));
        return;
    }
    // Present but not the default, so yt-dlp will not use it on its own.
    setResult(index, Status::Warning,
              tr("Found %1, but only deno is used automatically. Set Preferences > "
                 "Locations > JavaScript runtime to \"%2\".")
                  .arg(found.join(QStringLiteral(", ")), found.first()));
}

void SetupDialog::checkFfmpeg(int index)
{
    const QString configured = m_settings.ffmpegPath.trimmed();
    const QString ffmpeg = configured.isEmpty()
                               ? findToolExecutable(QStringLiteral("ffmpeg"))
                               : configured;
    if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg)) {
        setResult(index, Status::Missing, tr("Not found."));
        return;
    }

    const QString probe = findToolExecutable(QStringLiteral("ffprobe"));
    if (probe.isEmpty()) {
        setResult(index, Status::Warning,
                  tr("%1 found, but ffprobe was not. yt-dlp needs both.").arg(ffmpeg));
        return;
    }
    setResult(index, Status::Good, ffmpeg);
}

void SetupDialog::checkChallengeSolver(int index)
{
    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(toolProcessEnvironment());
    proc->setProgram(m_settings.ytDlpPath);
    proc->setArguments({ QStringLiteral("-v"), QStringLiteral("--simulate"),
                         QStringLiteral("--no-warnings"),
                         QString::fromLatin1(kProbeUrl) });

    connect(proc, &QProcess::errorOccurred, this, [this, index, proc] {
        setResult(index, Status::Missing, tr("Could not run yt-dlp to check."));
        proc->deleteLater();
    });
    connect(proc, &QProcess::finished, this, [this, index, proc] {
        const QString out = QString::fromUtf8(proc->readAllStandardError())
                            + QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();

        // yt-dlp lists what it loaded; the solver appears as yt_dlp_ejs.
        static const QRegularExpression ejs(QStringLiteral("yt[_-]dlp[_-]ejs[- ]?([0-9.]*)"));
        const QRegularExpressionMatch match = ejs.match(out);
        if (match.hasMatch()) {
            const QString version = match.captured(1);
            setResult(index, Status::Good,
                      version.isEmpty() ? tr("Installed") : tr("Version %1").arg(version));
            m_output->appendPlainText(tr("Challenge solver: yt-dlp-ejs %1").arg(version));
            return;
        }

        if (m_settings.allowRemoteComponents) {
            setResult(index, Status::Warning,
                      tr("Not installed, but yt-dlp is allowed to download it when "
                         "needed. Installing it is faster and works offline."));
            return;
        }
        setResult(index, Status::Missing,
                  tr("Not installed. Signature solving will fail on the videos that "
                     "need it."));
    });
    proc->start();
}

void SetupDialog::checkTokenProvider(int index)
{
    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(toolProcessEnvironment());
    proc->setProgram(m_settings.ytDlpPath);
    proc->setArguments({ QStringLiteral("-v"), QStringLiteral("--simulate"),
                         QStringLiteral("--no-warnings"),
                         QString::fromLatin1(kProbeUrl) });

    connect(proc, &QProcess::errorOccurred, this, [this, index, proc] {
        setResult(index, Status::Missing, tr("Could not run yt-dlp to check."));
        proc->deleteLater();
    });
    connect(proc, &QProcess::finished, this, [this, index, proc] {
        const QString out = QString::fromUtf8(proc->readAllStandardError())
                            + QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();

        // The extractor prints the providers it loaded, whatever they are.
        static const QRegularExpression re(
            QStringLiteral("PO Token Providers:\\s*(.+)"));
        const QRegularExpressionMatch match = re.match(out);
        if (!match.hasMatch()) {
            setResult(index, Status::Warning,
                      tr("Could not determine this. It needs a working network "
                         "connection to check."));
            return;
        }

        const QString providers = match.captured(1).trimmed();
        m_output->appendPlainText(tr("PO Token Providers: %1").arg(providers));
        if (providers.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
            setResult(index, Status::Missing,
                      tr("No provider is installed. This is the usual cause of a "
                         "download failing part way through at full speed."));
        } else {
            setResult(index, Status::Good, providers);
        }
    });
    proc->start();
}
