#include "Library.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace lib {

static QString cachePath(const QString &folder) { return QDir(folder).filePath(".psxrdc.json"); }

bool DatCache::ensure(core::Platform p)
{
    const int i = int(p);
    if (index[i].loaded()) return true;
    if (tried[i]) return false;
    tried[i] = true;
    QString err;
    return core::loadDat(p, index[i], &err);
}

void writeCache(const QString &folder, const core::VerifyResult &r, const QString &platformId)
{
    QJsonObject o = r.toJson();
    o["platform"] = platformId;
    o["checkedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QFile f(cachePath(folder));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson());
}

Entry readEntry(const QString &folder)
{
    Entry e;
    e.folder = folder;
    e.name = QFileInfo(folder).fileName();
    e.status = "not checked";

    bool bincue = false, iso = false;
    const QDir d(folder);
    for (const QFileInfo &fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        const QString s = fi.suffix().toLower();
        if (s == "bin" || s == "cue") { bincue = true; e.hasOriginal = true; }
        else if (s == "iso") { iso = true; e.hasOriginal = true; }
        else if (s == "chd") e.hasChd = true;
        // the dump images, not redumper's multi-GB scratch files, count towards the size
        if (s == "bin" || s == "iso" || s == "chd" || s == "cue") e.bytes += fi.size();
    }
    const int kinds = int(bincue) + int(iso) + int(e.hasChd);
    e.format = kinds > 1 ? "mixed" : bincue ? "bin/cue" : iso ? "iso" : e.hasChd ? "chd" : "";

    QFile f(cachePath(folder));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        e.platformId = o["platform"].toString();
        e.game = o["game"].toString();
        e.serial = o["serial"].toString();
        // Older records could hold junk read from game data; only a real serial is shown.
        if (!QRegularExpression(R"(^[A-Z]{4}-\d{5}$)").match(e.serial).hasMatch()) e.serial.clear();
        e.status = o["overall"].toString(e.status);
        e.checkedAt = o["checkedAt"].toString();
        // A stale cache (images changed since) must not claim a pass.
        const QJsonArray files = o["files"].toArray();
        for (const auto &v : files) {
            const QJsonObject fo = v.toObject();
            const QFileInfo fi(d.filePath(fo["name"].toString()));
            if (!fi.exists() || fi.size() != qint64(fo["size"].toDouble())) {
                e.status = "changed since last check";
                break;
            }
        }
    }
    return e;
}

QVector<Entry> scan(const QString &root)
{
    QVector<Entry> out;
    const QDir d(root);
    for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (sub.startsWith('.') || sub.startsWith('_')) continue;
        out.push_back(readEntry(d.filePath(sub)));
    }
    QHash<QString, int> seen;
    for (const auto &e : out)
        if (!e.game.isEmpty()) seen[e.game]++;
    for (auto &e : out)
        e.duplicate = !e.game.isEmpty() && seen.value(e.game) > 1;
    return out;
}

core::VerifyResult verifyAuto(const QString &folder, DatCache &dats, QString *platformIdOut,
                              const core::VerifyProgress &progress)
{
    // Try the platform from the last check first, then PS1/PS2 (most common), then the rest.
    QVector<core::Platform> order;
    const Entry prev = readEntry(folder);
    core::Platform p;
    if (!prev.platformId.isEmpty() && core::platformFromId(prev.platformId, p)) order << p;
    for (core::Platform q : {core::Platform::PSX, core::Platform::PS2})
        if (!order.contains(q)) order << q;
    for (const auto &pi : core::allPlatforms())
        if (!order.contains(pi.platform)) order << pi.platform;

    core::VerifyResult best;
    best.overall = "UNKNOWN";
    best.folder = folder;
    QString bestId;
    for (core::Platform q : order) {
        if (!dats.ensure(q)) continue;
        core::VerifyResult r = core::verifyFolder(folder, q, dats.index[int(q)], progress);
        if (r.overall == "PASS" || r.overall == "FAIL") {
            best = r;
            bestId = core::platformId(q);
            if (r.overall == "PASS") break;
        }
        if (best.overall == "UNKNOWN" && best.files.isEmpty()) best = r;
    }
    if (platformIdOut) *platformIdOut = bestId;
    writeCache(folder, best, bestId);
    return best;
}

} // namespace lib
