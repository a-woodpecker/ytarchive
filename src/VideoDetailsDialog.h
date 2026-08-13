#pragma once

#include "Models.h"

#include <QDateTime>
#include <QDialog>
#include <QVector>

class QTextBrowser;
class QLineEdit;
class QComboBox;
class QLabel;
class QTabWidget;

// Reads the sidecar files saved beside a downloaded video and shows them.
//
// The description is a plain .description file; the comments live inside
// .info.json, which yt-dlp only populates when --write-comments was enabled.
// Nothing here touches the network - if it was not downloaded, it is not here.
class VideoDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Page { Description, Comments };

    struct Comment {
        QString   id;
        QString   parentId;      // "root" for a top-level comment
        QString   author;
        QString   text;
        QDateTime posted;
        qint64    likeCount = -1;
        bool      pinned = false;
        bool      byUploader = false;
        bool      hearted = false;
    };

    VideoDetailsDialog(const VideoInfo &video, Page page, QWidget *parent = nullptr);

    // Cheap existence checks for enabling menu items. Neither parses the JSON,
    // because an .info.json with comments can run to several megabytes and the
    // menu must not stall while it is being built.
    static bool descriptionAvailable(const VideoInfo &video);
    static bool infoJsonAvailable(const VideoInfo &video);

    static QString descriptionPath(const VideoInfo &video);
    static QString infoJsonPath(const VideoInfo &video);

private:
    void buildUi();
    void loadDescription();
    void loadComments();
    void renderComments();
    QString commentsToHtml(const QVector<int> &order) const;

    VideoInfo m_video;
    QVector<Comment> m_comments;
    QString   m_commentsError;
    int       m_renderedCount = 0;

    QTabWidget    *m_tabs = nullptr;
    QTextBrowser  *m_description = nullptr;
    QTextBrowser  *m_commentsView = nullptr;
    QLineEdit     *m_filter = nullptr;
    QComboBox     *m_sort = nullptr;
    QLabel        *m_commentCount = nullptr;

    // Rendering every comment of a heavily discussed video as one HTML document
    // is slow and rarely useful; the filter narrows it when more are needed.
    static constexpr int kRenderLimit = 3000;
};
