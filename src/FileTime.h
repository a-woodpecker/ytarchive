#pragma once

#include <QDateTime>
#include <QString>

// Sets a file's modification (and, where the platform allows, creation) time.
// This is what makes the archive sort correctly by upload date in any file
// manager, independent of when the download happened.
bool setFileModificationTime(const QString &path, const QDateTime &when);
