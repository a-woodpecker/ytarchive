#pragma once

#include "Settings.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(const Settings &current, QWidget *parent = nullptr);

    Settings result() const;

private:
    void browseForFolder(QLineEdit *target);
    void browseForFile(QLineEdit *target, const QString &caption);

    Settings m_original;

    QLineEdit *m_archiveRoot;
    QLineEdit *m_ytDlpPath;
    QLineEdit *m_ffmpegPath;
    QLineEdit *m_jsRuntimes;
    QLineEdit *m_cookiesBrowser;
    QLineEdit *m_cookiesFile;
    QLineEdit *m_filenameTemplate;
    QComboBox *m_format;
    QComboBox *m_container;
    QSpinBox  *m_concurrency;
    QSpinBox  *m_rateLimit;
    QSpinBox  *m_retries;
    QCheckBox *m_infoJson;
    QCheckBox *m_thumbnail;
    QCheckBox *m_descriptionFile;
    QCheckBox *m_subtitles;
    QCheckBox *m_autoSubs;
    QCheckBox *m_comments;
    QCheckBox *m_embedMetadata;
    QCheckBox *m_embedChapters;
    QCheckBox *m_checkUpdates;
    QComboBox *m_theme;
};
