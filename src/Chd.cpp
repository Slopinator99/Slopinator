#include "Chd.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>

#include "Library.h"

namespace chd {

QString findChdman(const core::Config &cfg)
{
    if (!cfg.chdmanPath.isEmpty() && QFileInfo::exists(cfg.chdmanPath)) return cfg.chdmanPath;
    for (const QString &dir : {core::dataDir(), core::appDir()}) {
        const QString local = dir + "/chdman.exe";
        if (QFileInfo::exists(local)) return local;
    }
    return {};
}

// Runs chdman, forwarding its output. Returns the exit code (-1 if it could not start).
static int run(const QString &chdman, const QStringList &args,
               const std::function<void(const QString &)> &log, const std::function<void(int)> &percent)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
#endif
    p.start(chdman, args);
    if (!p.waitForStarted(10000)) return -1;
    static const QRegularExpression pct(R"((\d+(?:\.\d+)?)% complete)");
    QByteArray buf;
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(500);
        buf += p.readAll();
        const QList<QByteArray> parts = buf.split('\r');
        buf = parts.last();
        for (int i = 0; i < parts.size() - 1; ++i)
            for (const QByteArray &line : parts[i].split('\n')) {
                const QString s = QString::fromUtf8(line).trimmed();
                if (s.isEmpty()) continue;
                const auto m = pct.match(s);
                if (m.hasMatch()) { if (percent) percent(int(m.captured(1).toDouble())); }
                else if (log) log(s);
            }
    }
    buf += p.readAll();
    for (const QByteArray &line : buf.split('\n')) {
        const QString s = QString::fromUtf8(line).trimmed();
        if (!s.isEmpty() && log) log(s);
    }
    return p.exitCode();
}

static QString md5Of(const QString &path)
{
    core::Hashes h;
    return core::hashFile(path, h) ? h.md5 : QString();
}

Result compress(const QString &folder, const QString &chdman, const Options &opt,
                const std::function<void(const QString &)> &log,
                const std::function<void(int)> &percent)
{
    Result res;
    const QDir d(folder);
    auto say = [&](const QString &s) { if (log) log(s); };

    // Only dumps proven good against Redump are worth compressing and possibly deleting.
    const lib::Entry e = lib::readEntry(folder);
    if (e.status != "PASS") { res.error = "the dump is not verified (status: " + e.status + "); verify it first"; return res; }
    QFile cf(QDir(folder).filePath(".psxrdc.json"));
    if (!cf.open(QIODevice::ReadOnly)) { res.error = "no verification record found"; return res; }
    const QJsonObject rec = QJsonDocument::fromJson(cf.readAll()).object();
    cf.close();

    QString cue, iso;
    QStringList imageFiles;
    for (const QString &n : d.entryList(QDir::Files)) {
        const QString s = QFileInfo(n).suffix().toLower();
        if (s == "cue") cue = n;
        else if (s == "iso") iso = n;
        if (s == "cue" || s == "bin" || s == "iso") { imageFiles << n; res.originalBytes += QFileInfo(d.filePath(n)).size(); }
    }
    if (cue.isEmpty() && iso.isEmpty()) { res.error = "no cue/bin or iso to compress"; return res; }

    const QString base = QFileInfo(cue.isEmpty() ? iso : cue).completeBaseName();
    const QString chdFile = d.filePath(base + ".chd");
    res.chdPath = chdFile;
    if (QFileInfo::exists(chdFile)) { res.error = "a CHD with this name already exists"; return res; }

    // 1. create
    say("Creating " + base + ".chd");
    const QStringList create = cue.isEmpty()
        ? QStringList{"createdvd", "-i", d.filePath(iso), "-o", chdFile}
        : QStringList{"createcd", "-i", d.filePath(cue), "-o", chdFile};
    if (run(chdman, create, log, percent) != 0 || !QFileInfo::exists(chdFile)) {
        QFile::remove(chdFile);
        res.error = "chdman could not create the CHD";
        return res;
    }
    res.chdBytes = QFileInfo(chdFile).size();

    // 2. chdman's own check
    say("Checking the CHD's internal checksums");
    if (run(chdman, {"verify", "-i", chdFile}, log, percent) != 0) {
        QFile::remove(chdFile);
        res.error = "chdman verify failed; the CHD was removed";
        return res;
    }

    // 3. round trip: extract again and compare with the hashes verified against Redump
    say("Extracting it again to compare with the verified original");
    const QString tmp = d.filePath("_chd_check");
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    QStringList origMd5;      // md5 of every .bin / .iso that was verified
    for (const auto &v : rec["files"].toArray()) {
        const QJsonObject fo = v.toObject();
        if (!fo["name"].toString().endsWith(".cue", Qt::CaseInsensitive)) origMd5 << fo["md5"].toString();
    }
    QStringList extractedMd5;
    bool okExtract = false;
    if (cue.isEmpty()) {
        okExtract = run(chdman, {"extractdvd", "-i", chdFile, "-o", tmp + "/check.iso"}, log, percent) == 0;
        if (okExtract) extractedMd5 << md5Of(tmp + "/check.iso");
    } else {
        okExtract = run(chdman, {"extractcd", "-i", chdFile, "-o", tmp + "/check.cue", "-ob", tmp + "/check_%t.bin", "--splitbin"},
                        log, percent) == 0;
        if (okExtract) {
            for (const QString &n : QDir(tmp).entryList({"*.bin"}, QDir::Files)) extractedMd5 << md5Of(QDir(tmp).filePath(n));
        }
    }
    bool same = okExtract && !extractedMd5.isEmpty();
    if (same) {
        QStringList a = origMd5, b = extractedMd5;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        same = (a == b);
        if (!same && b.size() == 1 && a.size() > 1) {
            // This chdman wrote one bin for the whole disc: compare with the tracks joined in cue order.
            QCryptographicHash h(QCryptographicHash::Md5);
            QFile cueFile(d.filePath(cue));
            cueFile.open(QIODevice::ReadOnly);
            const QString cueText = QString::fromUtf8(cueFile.readAll());
            static const QRegularExpression re(R"re(FILE\s+"([^"]+)")re");
            for (auto m = re.globalMatch(cueText); m.hasNext();) {
                QFile bf(d.filePath(m.next().captured(1)));
                if (!bf.open(QIODevice::ReadOnly)) break;
                while (!bf.atEnd()) h.addData(bf.read(8 << 20));
            }
            same = (QString(h.result().toHex()) == b.first());
        }
    }
    QDir(tmp).removeRecursively();
    if (!same) {
        QFile::remove(chdFile);
        res.error = "the CHD did not extract back to the verified data; it was removed and nothing else was touched";
        return res;
    }
    say("The CHD extracts back to exactly the verified data.");

    // 4. optional deletions, only after the proof above
    if (opt.deleteOriginals) {
        for (const QString &n : imageFiles)
            if (QFile::remove(d.filePath(n))) res.deleted << n;
    }
    if (opt.deleteScratch) {
        for (const QString &suffix : {QString("scram"), QString("state"), QString("subcode"), QString("toc"), QString("fulltoc"),
                                      QString("manufacturer"), QString("physical")})
            for (const QString &n : d.entryList({"*." + suffix}, QDir::Files))
                if (QFile::remove(d.filePath(n))) res.deleted << n;
    }

    // Keep the library's record truthful: the dump is now the CHD.
    QJsonObject out = rec;
    QJsonArray files;
    files.append(QJsonObject{{"name", base + ".chd"}, {"verdict", "MATCH"}, {"size", double(res.chdBytes)},
                             {"detail", "round-trip verified against the Redump data"},
                             {"sourceMd5", QJsonArray::fromStringList(origMd5)}});
    if (!opt.deleteOriginals)
        for (const auto &v : rec["files"].toArray()) files.append(v);
    out["files"] = files;
    out["chd"] = true;
    QFile wf(QDir(folder).filePath(".psxrdc.json"));
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) wf.write(QJsonDocument(out).toJson());

    res.ok = true;
    return res;
}

} // namespace chd
