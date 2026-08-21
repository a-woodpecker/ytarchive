#pragma once

#include "Models.h"

#include <QString>
#include <QStringList>
#include <QVector>

class Database;

// Reading and writing the catalog as CSV.
//
// Video titles contain commas, quotation marks and occasionally newlines, so
// this follows RFC 4180 rather than splitting on commas: fields are quoted when
// they need to be, and a quote inside a field is doubled.
namespace Csv {

QString escape(const QString &field);
QString row(const QStringList &fields);

// Parses a whole document at once, because a quoted field may span lines.
QVector<QStringList> parse(const QString &text);

struct ImportResult {
    int channelsAdded = 0;
    int videosAdded = 0;
    int videosAlreadyKnown = 0;
    int rowsSkipped = 0;
    QStringList problems;      // at most a handful, for reporting
    bool ok = false;
};

// One row per video, with its channel repeated on each row so the file stands
// alone. Written with a byte order mark, which is what spreadsheets need to
// read UTF-8 correctly.
bool exportVideos(const QVector<VideoInfo> &videos, const QString &path, QString *error);

// Adds channels and videos the catalog does not already have.
//
// Download state, file paths and checksums are deliberately **not** imported:
// they describe files on the machine that produced the export, and claiming
// them here would mark videos as archived when nothing is on disk.
ImportResult importCatalog(Database &db, const QString &path);

} // namespace Csv
