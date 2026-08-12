#include "FileTime.h"

#include <QFile>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#endif

bool setFileModificationTime(const QString &path, const QDateTime &when)
{
    if (!when.isValid() || !QFile::exists(path))
        return false;

    const qint64 secs = when.toSecsSinceEpoch();

#ifdef Q_OS_WIN
    // Windows FILETIME counts 100ns intervals since 1601-01-01.
    const qint64 kUnixEpochInFileTime = 116444736000000000LL;
    const qint64 ft100ns = kUnixEpochInFileTime + secs * 10000000LL;

    FILETIME fileTime;
    fileTime.dwLowDateTime  = static_cast<DWORD>(ft100ns & 0xFFFFFFFFLL);
    fileTime.dwHighDateTime = static_cast<DWORD>((ft100ns >> 32) & 0xFFFFFFFFLL);

    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),
                           FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    // Creation time is set too so Explorer's "Date created" column agrees.
    const BOOL ok = SetFileTime(h, &fileTime, nullptr, &fileTime);
    CloseHandle(h);
    return ok != FALSE;
#else
    struct timeval times[2];
    times[0].tv_sec = static_cast<time_t>(secs);   // access time
    times[0].tv_usec = 0;
    times[1].tv_sec = static_cast<time_t>(secs);   // modification time
    times[1].tv_usec = 0;
    return ::utimes(QFile::encodeName(path).constData(), times) == 0;
#endif
}
