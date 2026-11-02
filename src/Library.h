#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "Core.h"

namespace lib {

// One dump folder under the library root, as shown in the Library tab.
struct Entry {
    QString folder;         // absolute path
    QString name;           // folder name
    QString platformId;     // "psx", "ps2", ... or empty if never identified
    QString game;           // Redump title, empty if unknown
    QString serial;
    QString status;         // PASS / FAIL / UNKNOWN / "not checked"
    QString format;         // "bin/cue", "iso", "chd", "mixed"
    qint64 bytes = 0;
    QString checkedAt;
    bool duplicate = false; // the same Redump title appears in another folder
    bool hasChd = false;
    bool hasOriginal = false;   // bin/cue or iso present
};

// Reads <folder>/.psxrdc.json written by the last verification (cheap, no hashing).
Entry readEntry(const QString &folder);
// Lists every immediate sub-folder of root, marking duplicates.
QVector<Entry> scan(const QString &root);

// Verifies a folder against every downloaded database until one identifies it, and
// stores the outcome in the folder's .psxrdc.json. `dats` caches loaded databases.
struct DatCache {
    core::DatIndex index[core::kPlatformCount];
    bool tried[core::kPlatformCount] = {};
    bool ensure(core::Platform p);      // loads on first use; false if no database downloaded
};
core::VerifyResult verifyAuto(const QString &folder, DatCache &dats, QString *platformIdOut,
                              const core::VerifyProgress &progress = {});
void writeCache(const QString &folder, const core::VerifyResult &r, const QString &platformId);

} // namespace lib
