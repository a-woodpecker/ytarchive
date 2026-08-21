#pragma once

#include <QString>

class Database;

// Matches media already on disk against catalog entries, and marks them
// archived.
//
// This is what makes an archive re-adoptable. The catalog can be rebuilt from a
// CSV, or lost entirely and re-synced from the channels, and the files are
// found again because the filename template embeds the video id in brackets:
//
//     2024-03-11 [dQw4w9WgXcQ] Steam-Bending the New Frames.mkv
//
// That id survives being moved between machines, renamed folders and a
// different archive root, none of which a stored absolute path does.
namespace Archive {

struct ScanResult {
    int filesSeen = 0;
    int adopted = 0;          // catalogued, found on disk, now marked archived
    int alreadyArchived = 0;
    int unmatched = 0;        // on disk but not in the catalog
    bool ok = false;
};

// Walks `root`, adopting anything the catalog knows about but does not have.
// Never marks a video archived without a file actually being there.
ScanResult adopt(Database &db, const QString &root);

} // namespace Archive
