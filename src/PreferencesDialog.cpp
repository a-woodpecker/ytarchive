#include "PreferencesDialog.h"
#include "Theme.h"

#include <QCheckBox>
#include <functional>
#include <QCoreApplication>
#include <QComboBox>
#include <QStandardItemModel>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
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
    setMinimumSize(620, 560);
    resize(700, 680);

    auto *tabs = new QTabWidget(this);

    // Wraps a tab page so it scrolls rather than clipping. The Downloading tab
    // in particular is now taller than a small screen.
    auto scrollable = [](QWidget *page) -> QWidget * {
        auto *area = new QScrollArea;
        area->setWidget(page);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return area;
    };

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
    locationForm->addRow(note);

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
    locationForm->addRow(runtimeNote);

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
    locationForm->addRow(updateNote);

    tabs->addTab(scrollable(locations), tr("Locations"));

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
    downloadForm->addRow(retryNote);

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
    downloadForm->addRow(pacingNote);

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
    downloadForm->addRow(extractorNote);

    m_verboseLogging = new QCheckBox(tr("Verbose yt-dlp output"), this);
    m_verboseLogging->setChecked(current.verboseLogging);
    downloadForm->addRow(tr("Diagnostics"), m_verboseLogging);

    auto *verboseNote = new QLabel(
        tr("Adds <tt>-v</tt>, keeps warnings, and writes the full command line to the "
           "Output tab. This is how to confirm which yt-dlp is being run and whether "
           "a PO token provider is loaded. Noisy; leave it off unless diagnosing."), this);
    verboseNote->setWordWrap(true);
    verboseNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(verboseNote);

    m_filenameTemplate = new QLineEdit(current.filenameTemplate, this);
    downloadForm->addRow(tr("Filename template"), m_filenameTemplate);
    auto *templateNote = new QLabel(
        tr("yt-dlp output template. The default puts the upload date first so files sort "
           "chronologically even without reading timestamps."), this);
    templateNote->setWordWrap(true);
    templateNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(templateNote);

    tabs->addTab(scrollable(downloads), tr("Downloading"));

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

    tabs->addTab(scrollable(artefacts), tr("What to keep"));

    // ---- Access -----------------------------------------------------------
    auto *access = new QWidget(this);
    auto *accessForm = new QFormLayout(access);

    // A list rather than a text field: the error that sends people here names
    // this setting, so it should be selectable without knowing yt-dlp's syntax.
    m_cookiesBrowser = new QComboBox(this);
    m_cookiesBrowser->setEditable(true);
    m_cookiesBrowser->addItem(tr("None"), QString());
    // Every browser yt-dlp accepts, in the order it lists them itself.
    for (const QString &browser : { QStringLiteral("brave"),    QStringLiteral("chrome"),
                                    QStringLiteral("chromium"), QStringLiteral("edge"),
                                    QStringLiteral("firefox"),  QStringLiteral("opera"),
                                    QStringLiteral("safari"),   QStringLiteral("vivaldi"),
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
    // Firefox forks - LibreWolf, Floorp, Zen - are not names yt-dlp accepts, but
    // it will read any Firefox-shaped profile given a path. This button spares
    // people from having to know that.
    auto *cookieRow = new QWidget(this);
    auto *cookieLayout = new QHBoxLayout(cookieRow);
    cookieLayout->setContentsMargins(0, 0, 0, 0);
    cookieLayout->addWidget(m_cookiesBrowser, 1);

    auto *browseProfile = new QPushButton(tr("Profile folder…"), cookieRow);
    browseProfile->setToolTip(
        tr("For a Firefox derivative such as LibreWolf: open about:profiles in it, "
           "copy the Root Directory, and choose that folder here."));
    connect(browseProfile, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select the browser profile folder"),
            QDir::homePath());
        if (dir.isEmpty())
            return;
        // yt-dlp reads any Firefox-format profile when told it is "firefox".
        m_cookiesBrowser->setCurrentText(QStringLiteral("firefox:") + dir);
    });
    cookieLayout->addWidget(browseProfile);

    accessForm->addRow(tr("Use cookies from browser"), cookieRow);

    auto *cookiesNote = new QLabel(
        tr("Pick the browser you are signed in to. Cookies are read from its profile "
           "each time, so nothing is stored here and nothing needs exporting.<br><br>"
           "Firefox derivatives such as LibreWolf, Floorp and Zen are not in the list "
           "because yt-dlp does not know them by name. Use <b>Profile folder</b> "
           "instead: their cookie databases are in Firefox format and are read the "
           "same way.<br><br>"
           "Close the browser first - some keep the cookie database locked. Set this "
           "only when you need it: passing cookies can make some formats unavailable, "
           "and ties your downloads to your account."), this);
    cookiesNote->setWordWrap(true);
    cookiesNote->setObjectName(QStringLiteral("hint"));
    accessForm->addRow(cookiesNote);

    m_cookiesOnlyWhenNeeded =
        new QCheckBox(tr("Only send cookies when a video needs them"), this);
    m_cookiesOnlyWhenNeeded->setChecked(current.cookiesOnlyWhenNeeded);
    accessForm->addRow(tr("When to use"), m_cookiesOnlyWhenNeeded);

    auto *whenNote = new QLabel(
        tr("Sending cookies on every request makes the service offer formats that "
           "cannot be downloaded, so videos that would work anonymously start "
           "failing with <i>Requested format is not available</i>.<br><br>"
           "With this on, each video is tried without cookies first, and retried "
           "with them only when the failure says an account was needed. Turn it off "
           "only if you specifically want every request authenticated."), this);
    whenNote->setWordWrap(true);
    whenNote->setObjectName(QStringLiteral("hint"));
    accessForm->addRow(whenNote);

    m_cookiesFile = new QLineEdit(current.cookiesFile, this);
    accessForm->addRow(tr("Cookies file"),
                       withBrowseButton(m_cookiesFile, this, "Browse…",
                                        [this] { browseForFile(m_cookiesFile, tr("Select cookies file")); }));

    auto *accessNote = new QLabel(
        tr("Needed only for videos your account can see but an anonymous visitor cannot, "
           "such as age-restricted or unlisted uploads you have access to."), this);
    accessNote->setWordWrap(true);
    accessNote->setObjectName(QStringLiteral("hint"));
    accessForm->addRow(accessNote);

    tabs->addTab(scrollable(access), tr("Access"));

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
    s.cookiesOnlyWhenNeeded = m_cookiesOnlyWhenNeeded->isChecked();
    s.cookiesFile       = m_cookiesFile->text().trimmed();

    if (s.ytDlpPath.isEmpty())
        s.ytDlpPath = QStringLiteral("yt-dlp");
    if (s.archiveRoot.isEmpty())
        s.archiveRoot = m_original.archiveRoot;
    return s;
}
