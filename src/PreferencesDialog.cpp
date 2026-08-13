#include "PreferencesDialog.h"
#include "Theme.h"

#include <QCheckBox>
#include <functional>
#include <QCoreApplication>
#include <QComboBox>
#include <QStandardItemModel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QWidget *withBrowseButton(QLineEdit *edit, QWidget *parent, const char *label,
                          const std::function<void()> &onClick)
{
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit);
    auto *button = new QPushButton(QCoreApplication::translate("PreferencesDialog", label),
                                   container);
    QObject::connect(button, &QPushButton::clicked, container, onClick);
    layout->addWidget(button);
    return container;
}

} // namespace

PreferencesDialog::PreferencesDialog(const Settings &current, QWidget *parent)
    : QDialog(parent)
    , m_original(current)
{
    setWindowTitle(tr("Preferences"));
    setMinimumWidth(620);

    auto *tabs = new QTabWidget(this);

    // ---- Locations --------------------------------------------------------
    auto *locations = new QWidget(this);
    auto *locationForm = new QFormLayout(locations);

    m_archiveRoot = new QLineEdit(current.archiveRoot, this);
    locationForm->addRow(tr("Archive folder"),
                         withBrowseButton(m_archiveRoot, this, "Browse…",
                                          [this] { browseForFolder(m_archiveRoot); }));

    auto *note = new QLabel(tr("Each channel gets its own subfolder. The catalog database "
                               "(catalog.db) lives here too, alongside the media."), this);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("hint"));
    locationForm->addRow(QString(), note);

    m_ytDlpPath = new QLineEdit(current.ytDlpPath, this);
    locationForm->addRow(tr("yt-dlp"),
                         withBrowseButton(m_ytDlpPath, this, "Browse…",
                                          [this] { browseForFile(m_ytDlpPath, tr("Locate yt-dlp")); }));

    m_ffmpegPath = new QLineEdit(current.ffmpegPath, this);
    m_ffmpegPath->setPlaceholderText(tr("Leave empty to use the system ffmpeg"));
    locationForm->addRow(tr("ffmpeg"),
                         withBrowseButton(m_ffmpegPath, this, "Browse…",
                                          [this] { browseForFile(m_ffmpegPath, tr("Locate ffmpeg")); }));

    m_jsRuntimes = new QLineEdit(current.jsRuntimes, this);
    m_jsRuntimes->setPlaceholderText(tr("Leave empty for deno, or e.g. node, bun, node:/usr/bin/node"));
    locationForm->addRow(tr("JavaScript runtime"), m_jsRuntimes);

    auto *runtimeNote = new QLabel(
        tr("yt-dlp needs a JavaScript runtime to sign media URLs. Without one, "
           "listings work but downloads fail with HTTP 403. Only <b>deno</b> is used "
           "automatically; name any other runtime here."), this);
    runtimeNote->setWordWrap(true);
    runtimeNote->setObjectName(QStringLiteral("hint"));
    locationForm->addRow(QString(), runtimeNote);

    m_theme = new QComboBox(this);
    m_theme->addItem(tr("Dark"),  QStringLiteral("dark"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->addItem(Theme::systemDetectionAvailable()
                         ? tr("Match the system")
                         : tr("Match the system (needs Qt 6.5 or newer)"),
                     QStringLiteral("system"));
    if (!Theme::systemDetectionAvailable()) {
        // Present but inert on older Qt, rather than silently doing nothing.
        auto *model = qobject_cast<QStandardItemModel *>(m_theme->model());
        if (model && model->item(2))
            model->item(2)->setEnabled(false);
    }
    const int themeIndex = m_theme->findData(current.theme);
    m_theme->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    locationForm->addRow(tr("Theme"), m_theme);

    m_checkUpdates = new QCheckBox(tr("Check for new versions on startup"), this);
    m_checkUpdates->setChecked(current.checkUpdatesOnStartup);
    locationForm->addRow(tr("Updates"), m_checkUpdates);

    auto *updateNote = new QLabel(tr("Checks the project's GitHub releases at most once a day. "
                                     "Nothing is downloaded or installed automatically."), this);
    updateNote->setWordWrap(true);
    updateNote->setObjectName(QStringLiteral("hint"));
    locationForm->addRow(QString(), updateNote);

    tabs->addTab(locations, tr("Locations"));

    // ---- Downloading ------------------------------------------------------
    auto *downloads = new QWidget(this);
    auto *downloadForm = new QFormLayout(downloads);

    m_format = new QComboBox(this);
    m_format->setEditable(true);
    m_format->addItems({
        QStringLiteral("bv*+ba/b"),
        QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]"),
        QStringLiteral("bv*[height<=720]+ba/b[height<=720]"),
        QStringLiteral("bestvideo[vcodec^=avc1]+bestaudio[ext=m4a]/b"),
        QStringLiteral("b")
    });
    m_format->setToolTip(
        tr("Selectors that combine separate video and audio streams give the best "
           "quality but are more prone to HTTP 403. Plain \"b\" takes a single "
           "pre-combined stream: lower quality, noticeably more reliable."));
    m_format->setCurrentText(current.formatSelector);
    downloadForm->addRow(tr("Quality (yt-dlp format selector)"), m_format);

    m_container = new QComboBox(this);
    m_container->addItems({ QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("webm") });
    m_container->setCurrentText(current.mergeContainer);
    downloadForm->addRow(tr("Container"), m_container);

    m_concurrency = new QSpinBox(this);
    m_concurrency->setRange(1, 8);
    m_concurrency->setValue(current.maxConcurrent);
    downloadForm->addRow(tr("Simultaneous downloads"), m_concurrency);

    m_rateLimit = new QSpinBox(this);
    m_rateLimit->setRange(0, 200000);
    m_rateLimit->setSuffix(tr(" KiB/s"));
    m_rateLimit->setSpecialValueText(tr("Unlimited"));
    m_rateLimit->setValue(current.rateLimitKiB);
    downloadForm->addRow(tr("Speed limit"), m_rateLimit);

    m_retries = new QSpinBox(this);
    m_retries->setRange(0, 100);
    m_retries->setValue(current.retries);
    downloadForm->addRow(tr("Retries per video"), m_retries);

    m_extractorRetries = new QSpinBox(this);
    m_extractorRetries->setRange(0, 50);
    m_extractorRetries->setValue(current.extractorRetries);
    m_extractorRetries->setToolTip(
        tr("Retries of the metadata step, separate from retries of the transfer."));
    downloadForm->addRow(tr("Extraction retries"), m_extractorRetries);

    m_autoRetryAttempts = new QSpinBox(this);
    m_autoRetryAttempts->setRange(0, 10);
    m_autoRetryAttempts->setValue(current.autoRetryAttempts);
    m_autoRetryAttempts->setSpecialValueText(tr("Do not retry"));
    downloadForm->addRow(tr("Automatic retries"), m_autoRetryAttempts);

    m_autoRetryDelay = new QSpinBox(this);
    m_autoRetryDelay->setRange(1, 600);
    m_autoRetryDelay->setSuffix(tr(" s"));
    m_autoRetryDelay->setValue(current.autoRetryDelay);
    downloadForm->addRow(tr("First retry after"), m_autoRetryDelay);

    auto *retryNote = new QLabel(
        tr("Each retry re-runs yt-dlp from scratch, which re-extracts the media URLs - "
           "usually what a 403 actually needs. The wait grows with each attempt. "
           "Videos the service reports as private, removed or members-only are never "
           "retried."), this);
    retryNote->setWordWrap(true);
    retryNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(QString(), retryNote);

    m_sleepRequests = new QSpinBox(this);
    m_sleepRequests->setRange(0, 60);
    m_sleepRequests->setSuffix(tr(" s"));
    m_sleepRequests->setSpecialValueText(tr("None"));
    m_sleepRequests->setValue(current.sleepRequests);
    downloadForm->addRow(tr("Pause between requests"), m_sleepRequests);

    m_sleepInterval = new QSpinBox(this);
    m_sleepInterval->setRange(0, 600);
    m_sleepInterval->setSuffix(tr(" s"));
    m_sleepInterval->setSpecialValueText(tr("None"));
    m_sleepInterval->setValue(current.sleepInterval);
    downloadForm->addRow(tr("Pause between videos, minimum"), m_sleepInterval);

    m_maxSleepInterval = new QSpinBox(this);
    m_maxSleepInterval->setRange(0, 600);
    m_maxSleepInterval->setSuffix(tr(" s"));
    m_maxSleepInterval->setSpecialValueText(tr("Same as the minimum"));
    m_maxSleepInterval->setValue(current.maxSleepInterval);
    downloadForm->addRow(tr("Pause between videos, maximum"), m_maxSleepInterval);

    auto *pacingNote = new QLabel(
        tr("Pauses are the only defence against being rate limited by IP. They cost "
           "time on every download, so leave them off until 403 errors appear."), this);
    pacingNote->setWordWrap(true);
    pacingNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(QString(), pacingNote);

    m_extractorArgs = new QLineEdit(current.extractorArgs, this);
    m_extractorArgs->setPlaceholderText(
        QStringLiteral("youtube:player_client=default,mweb"));
    downloadForm->addRow(tr("Extractor arguments"), m_extractorArgs);

    auto *extractorNote = new QLabel(
        tr("Passed straight through as <tt>--extractor-args</tt>. Use it to supply a "
           "PO token or select a different client when the service demands one; what "
           "it wants changes faster than this program is rebuilt."), this);
    extractorNote->setWordWrap(true);
    extractorNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(QString(), extractorNote);

    m_verboseLogging = new QCheckBox(tr("Verbose yt-dlp output"), this);
    m_verboseLogging->setChecked(current.verboseLogging);
    downloadForm->addRow(tr("Diagnostics"), m_verboseLogging);

    auto *verboseNote = new QLabel(
        tr("Adds <tt>-v</tt>, keeps warnings, and writes the full command line to the "
           "Output tab. This is how to confirm which yt-dlp is being run and whether "
           "a PO token provider is loaded. Noisy; leave it off unless diagnosing."), this);
    verboseNote->setWordWrap(true);
    verboseNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(QString(), verboseNote);

    m_filenameTemplate = new QLineEdit(current.filenameTemplate, this);
    downloadForm->addRow(tr("Filename template"), m_filenameTemplate);
    auto *templateNote = new QLabel(
        tr("yt-dlp output template. The default puts the upload date first so files sort "
           "chronologically even without reading timestamps."), this);
    templateNote->setWordWrap(true);
    templateNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(QString(), templateNote);

    tabs->addTab(downloads, tr("Downloading"));

    // ---- What to keep -----------------------------------------------------
    auto *artefacts = new QWidget(this);
    auto *artefactLayout = new QVBoxLayout(artefacts);

    auto *intro = new QLabel(tr("Extra files saved next to each video. These are what turn a "
                                "folder of media into an archive with provenance."), this);
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("hint"));
    artefactLayout->addWidget(intro);

    auto addCheck = [&](QCheckBox *&target, const QString &text, bool value) {
        target = new QCheckBox(text, this);
        target->setChecked(value);
        artefactLayout->addWidget(target);
    };
    addCheck(m_infoJson,        tr("Full metadata (.info.json)"),        current.writeInfoJson);
    addCheck(m_thumbnail,       tr("Thumbnail image"),                   current.writeThumbnail);
    addCheck(m_descriptionFile, tr("Description (.description)"),        current.writeDescription);
    addCheck(m_subtitles,       tr("Uploaded subtitles"),                current.writeSubtitles);
    addCheck(m_autoSubs,        tr("Auto-generated subtitles"),          current.writeAutoSubs);
    addCheck(m_comments,        tr("Comments (slow; adds minutes per video)"), current.writeComments);
    addCheck(m_embedMetadata,   tr("Embed title and date in the media file"), current.embedMetadata);
    addCheck(m_embedChapters,   tr("Embed chapter markers"),             current.embedChapters);
    artefactLayout->addStretch();

    tabs->addTab(artefacts, tr("What to keep"));

    // ---- Access -----------------------------------------------------------
    auto *access = new QWidget(this);
    auto *accessForm = new QFormLayout(access);

    // A list rather than a text field: the error that sends people here names
    // this setting, so it should be selectable without knowing yt-dlp's syntax.
    m_cookiesBrowser = new QComboBox(this);
    m_cookiesBrowser->setEditable(true);
    m_cookiesBrowser->addItem(tr("None"), QString());
    for (const QString &browser : { QStringLiteral("firefox"), QStringLiteral("chrome"),
                                    QStringLiteral("chromium"), QStringLiteral("brave"),
                                    QStringLiteral("edge"), QStringLiteral("opera"),
                                    QStringLiteral("vivaldi"), QStringLiteral("safari"),
                                    QStringLiteral("whale") }) {
        m_cookiesBrowser->addItem(browser, browser);
    }
    if (current.cookiesFromBrowser.isEmpty()) {
        m_cookiesBrowser->setCurrentIndex(0);
    } else {
        const int index = m_cookiesBrowser->findData(current.cookiesFromBrowser);
        if (index >= 0)
            m_cookiesBrowser->setCurrentIndex(index);
        else
            m_cookiesBrowser->setCurrentText(current.cookiesFromBrowser);
    }
    accessForm->addRow(tr("Use cookies from browser"), m_cookiesBrowser);

    auto *cookiesNote = new QLabel(
        tr("Pick the browser you are signed in to. Cookies are read from its profile "
           "each time, so nothing is stored here and nothing needs exporting. For a "
           "named profile, type it as <tt>firefox:profilename</tt>.<br><br>"
           "Close the browser first: some keep their cookie database locked while "
           "running. Set this only when you need it - passing cookies can make some "
           "formats unavailable, and ties your downloads to your account."), this);
    cookiesNote->setWordWrap(true);
    cookiesNote->setObjectName(QStringLiteral("hint"));
    accessForm->addRow(QString(), cookiesNote);

    m_cookiesFile = new QLineEdit(current.cookiesFile, this);
    accessForm->addRow(tr("Cookies file"),
                       withBrowseButton(m_cookiesFile, this, "Browse…",
                                        [this] { browseForFile(m_cookiesFile, tr("Select cookies file")); }));

    auto *accessNote = new QLabel(
        tr("Needed only for videos your account can see but an anonymous visitor cannot, "
           "such as age-restricted or unlisted uploads you have access to."), this);
    accessNote->setWordWrap(true);
    accessNote->setObjectName(QStringLiteral("hint"));
    accessForm->addRow(QString(), accessNote);

    tabs->addTab(access, tr("Access"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

void PreferencesDialog::browseForFolder(QLineEdit *target)
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder"),
                                                          target->text());
    if (!dir.isEmpty())
        target->setText(dir);
}

void PreferencesDialog::browseForFile(QLineEdit *target, const QString &caption)
{
    const QString file = QFileDialog::getOpenFileName(this, caption, target->text());
    if (!file.isEmpty())
        target->setText(file);
}

Settings PreferencesDialog::result() const
{
    Settings s = m_original;
    s.archiveRoot       = m_archiveRoot->text().trimmed();
    s.ytDlpPath         = m_ytDlpPath->text().trimmed();
    s.ffmpegPath        = m_ffmpegPath->text().trimmed();
    s.jsRuntimes        = m_jsRuntimes->text().trimmed();
    s.formatSelector    = m_format->currentText().trimmed();
    s.mergeContainer    = m_container->currentText().trimmed();
    s.maxConcurrent     = m_concurrency->value();
    s.rateLimitKiB      = m_rateLimit->value();
    s.retries           = m_retries->value();
    s.extractorRetries  = m_extractorRetries->value();
    s.sleepRequests     = m_sleepRequests->value();
    s.sleepInterval     = m_sleepInterval->value();
    s.maxSleepInterval  = m_maxSleepInterval->value();
    s.extractorArgs     = m_extractorArgs->text().trimmed();
    s.verboseLogging    = m_verboseLogging->isChecked();
    s.autoRetryAttempts = m_autoRetryAttempts->value();
    s.autoRetryDelay    = m_autoRetryDelay->value();
    s.filenameTemplate  = m_filenameTemplate->text().trimmed();
    s.writeInfoJson     = m_infoJson->isChecked();
    s.writeThumbnail    = m_thumbnail->isChecked();
    s.writeDescription  = m_descriptionFile->isChecked();
    s.writeSubtitles    = m_subtitles->isChecked();
    s.writeAutoSubs     = m_autoSubs->isChecked();
    s.writeComments     = m_comments->isChecked();
    s.embedMetadata     = m_embedMetadata->isChecked();
    s.embedChapters     = m_embedChapters->isChecked();
    s.theme             = m_theme->currentData().toString();
    s.checkUpdatesOnStartup = m_checkUpdates->isChecked();
    // Index 0 is "None"; anything else is either a listed browser or typed.
    s.cookiesFromBrowser = m_cookiesBrowser->currentIndex() == 0
                               ? QString()
                               : m_cookiesBrowser->currentText().trimmed();
    if (s.cookiesFromBrowser.compare(tr("None"), Qt::CaseInsensitive) == 0)
        s.cookiesFromBrowser.clear();
    s.cookiesFile       = m_cookiesFile->text().trimmed();

    if (s.ytDlpPath.isEmpty())
        s.ytDlpPath = QStringLiteral("yt-dlp");
    if (s.archiveRoot.isEmpty())
        s.archiveRoot = m_original.archiveRoot;
    return s;
}
