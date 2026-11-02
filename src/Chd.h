#pragma once
#include <QString>
#include <QStringList>
#include <functional>

#include "Core.h"

namespace chd {

// chdman.exe next to the app, or the path set in the configuration. Empty if not found.
QString findChdman(const core::Config &cfg);

struct Options {
    bool deleteOriginals = false;       // remove .bin/.cue/.iso after the CHD is proven good
    bool deleteScratch = false;         // also remove redumper's .scram/.state/.subcode/... files
};

struct Result {
    bool ok = false;
    QString error;
    QString chdPath;
    qint64 originalBytes = 0, chdBytes = 0;
    QStringList deleted;
};

// Compresses a PASSed dump folder to CHD, in a way that can be undone:
//   1. chdman createcd / createdvd
//   2. chdman verify (the CHD's own checksums)
//   3. extract it again and compare the track hashes with the verified originals
// Only when all three agree are originals touched, and only if asked to.
// log(line) gets progress text; percent(0..100) the current step's progress.
Result compress(const QString &folder, const QString &chdman, const Options &opt,
                const std::function<void(const QString &)> &log,
                const std::function<void(int)> &percent);

} // namespace chd
