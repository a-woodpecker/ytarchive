#include "PreferencesDialog.h"

#include <QCheckBox>
#include <functional>
#include <QCoreApplication>
#include <QComboBox>
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

    QWidget* withBrowseButton(QLineEdit* edit, QWidget* parent, const char* label,
        const std::function<void()>& onClick)
    {
        auto* container = new QWidget(parent);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(edit);
        auto* button = new QPushButton(QCoreApplication::translate("PreferencesDialog", label),
            container);
        QObject::connect(button, &QPushButton::clicked, container, onClick);
        layout->addWidget(button);
        return container;
    }

} // namespace

PreferencesDialog::PreferencesDialog(const Settings& current, QWidget* parent)
    : QDialog(parent)
    , m_original(current)
{
    setWindowTitle(tr("Preferences"));
    setMinimumWidth(620);

    auto* tabs = new QTabWidget(this);

    // ---- Locations --------------------------------------------------------
    auto* locations = new QWidget(this);
    auto* locationForm = new QFormLayout(locations);

    m_archiveRoot = new QLineEdit(current.archiveRoot, this);
    locationForm->addRow(tr("Archive folder"),
        withBrowseButton(m_archiveRoot, this, "Browse…",
            [this] { browseForFolder(m_archiveRoot); }));

    auto* note = new QLabel(tr("Each channel gets its own subfolder. The catalog database "
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

    m_checkUpdates = new QCheckBox(tr("Check for new versions on startup"), this);
    m_checkUpdates->setChecked(current.checkUpdatesOnStartup);
    locationForm->addRow(tr("Updates"), m_checkUpdates);

    auto* updateNote = new QLabel(tr("Checks the project's GitHub releases at most once a day. "
        "Nothing is downloaded or installed automatically."), this);
    updateNote->setWordWrap(true);
    updateNote->setObjectName(QStringLiteral("hint"));
    locationForm->addRow(QString(), updateNote);

    tabs->addTab(locations, tr("Locations"));

    // ---- Downloading ------------------------------------------------------
    auto* downloads = new QWidget(this);
    auto* downloadForm = new QFormLayout(downloads);

    m_format = new QComboBox(this);
    m_format->setEditable(true);
    m_format->addItems({
        QStringLiteral("bv*+ba/b"),
        QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]"),
        QStringLiteral("bv*[height<=720]+ba/b[height<=720]"),
        QStringLiteral("bestvideo[vcodec^=avc1]+bestaudio[ext=m4a]/b")
        });
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

    m_filenameTemplate = new QLineEdit(current.filenameTemplate, this);
    downloadForm->addRow(tr("Filename template"), m_filenameTemplate);
    auto* templateNote = new QLabel(
        tr("yt-dlp output template. The default puts the upload date first so files sort "
            "chronologically even without reading timestamps."), this);
    templateNote->setWordWrap(true);
    templateNote->setObjectName(QStringLiteral("hint"));
    downloadForm->addRow(QString(), templateNote);

    tabs->addTab(downloads, tr("Downloading"));

    // ---- What to keep -----------------------------------------------------
    auto* artefacts = new QWidget(this);
    auto* artefactLayout = new QVBoxLayout(artefacts);

    auto* intro = new QLabel(tr("Extra files saved next to each video. These are what turn a "
        "folder of media into an archive with provenance."), this);
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("hint"));
    artefactLayout->addWidget(intro);

    auto addCheck = [&](QCheckBox*& target, const QString& text, bool value) {
        target = new QCheckBox(text, this);
        target->setChecked(value);
        artefactLayout->addWidget(target);
        };
    addCheck(m_infoJson, tr("Full metadata (.info.json)"), current.writeInfoJson);
    addCheck(m_thumbnail, tr("Thumbnail image"), current.writeThumbnail);
    addCheck(m_descriptionFile, tr("Description (.description)"), current.writeDescription);
    addCheck(m_subtitles, tr("Uploaded subtitles"), current.writeSubtitles);
    addCheck(m_autoSubs, tr("Auto-generated subtitles"), current.writeAutoSubs);
    addCheck(m_comments, tr("Comments (slow; adds minutes per video)"), current.writeComments);
    addCheck(m_embedMetadata, tr("Embed title and date in the media file"), current.embedMetadata);
    addCheck(m_embedChapters, tr("Embed chapter markers"), current.embedChapters);
    artefactLayout->addStretch();

    tabs->addTab(artefacts, tr("What to keep"));

    // ---- Access -----------------------------------------------------------
    auto* access = new QWidget(this);
    auto* accessForm = new QFormLayout(access);

    m_cookiesBrowser = new QLineEdit(current.cookiesFromBrowser, this);
    m_cookiesBrowser->setPlaceholderText(tr("firefox, chrome, edge, safari…"));
    accessForm->addRow(tr("Use cookies from browser"), m_cookiesBrowser);

    m_cookiesFile = new QLineEdit(current.cookiesFile, this);
    accessForm->addRow(tr("Cookies file"),
        withBrowseButton(m_cookiesFile, this, "Browse…",
            [this] { browseForFile(m_cookiesFile, tr("Select cookies file")); }));

    auto* accessNote = new QLabel(
        tr("Needed only for videos your account can see but an anonymous visitor cannot, "
            "such as age-restricted or unlisted uploads you have access to."), this);
    accessNote->setWordWrap(true);
    accessNote->setObjectName(QStringLiteral("hint"));
    accessForm->addRow(QString(), accessNote);

    tabs->addTab(access, tr("Access"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

void PreferencesDialog::browseForFolder(QLineEdit* target)
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder"),
        target->text());
    if (!dir.isEmpty())
        target->setText(dir);
}

void PreferencesDialog::browseForFile(QLineEdit* target, const QString& caption)
{
    const QString file = QFileDialog::getOpenFileName(this, caption, target->text());
    if (!file.isEmpty())
        target->setText(file);
}

Settings PreferencesDialog::result() const
{
    Settings s = m_original;
    s.archiveRoot = m_archiveRoot->text().trimmed();
    s.ytDlpPath = m_ytDlpPath->text().trimmed();
    s.ffmpegPath = m_ffmpegPath->text().trimmed();
    s.formatSelector = m_format->currentText().trimmed();
    s.mergeContainer = m_container->currentText().trimmed();
    s.maxConcurrent = m_concurrency->value();
    s.rateLimitKiB = m_rateLimit->value();
    s.retries = m_retries->value();
    s.filenameTemplate = m_filenameTemplate->text().trimmed();
    s.writeInfoJson = m_infoJson->isChecked();
    s.writeThumbnail = m_thumbnail->isChecked();
    s.writeDescription = m_descriptionFile->isChecked();
    s.writeSubtitles = m_subtitles->isChecked();
    s.writeAutoSubs = m_autoSubs->isChecked();
    s.writeComments = m_comments->isChecked();
    s.embedMetadata = m_embedMetadata->isChecked();
    s.embedChapters = m_embedChapters->isChecked();
    s.checkUpdatesOnStartup = m_checkUpdates->isChecked();
    s.cookiesFromBrowser = m_cookiesBrowser->text().trimmed();
    s.cookiesFile = m_cookiesFile->text().trimmed();

    if (s.ytDlpPath.isEmpty())
        s.ytDlpPath = QStringLiteral("yt-dlp");
    if (s.archiveRoot.isEmpty())
        s.archiveRoot = m_original.archiveRoot;
    return s;
}