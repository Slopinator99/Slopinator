#include "Core.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QDateTime>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QXmlStreamReader>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace core {

const QVector<PlatformInfo> &allPlatforms()
{
    static const QString nintendo =
        "Needs a drive redumper supports for these discs (for example an MT1959-based Hitachi-LG "
        "Blu-ray drive flashed with OmniDrive). A normal drive, including a Lite-On iHAS124, cannot read them; "
        "redumper will report that and nothing is dumped.";
    static const QString xbox =
        "Needs a drive that can read the console's security sectors (usually a firmware-modified drive). "
        "A normal drive will fail; redumper reports it.";
    static const QString psdvd = "Blu-ray discs need a Blu-ray drive.";
    static const QString gd = "GD-ROM discs cannot be read by a normal drive; only standard CD-R / CD media works.";
    static const QVector<PlatformInfo> v = {
        {Platform::PSX, "psx", "Sony PlayStation", {}},
        {Platform::PS2, "ps2", "Sony PlayStation 2", {}},
        {Platform::PS3, "ps3", "Sony PlayStation 3", psdvd},
        {Platform::PSP, "psp", "Sony PlayStation Portable", "UMD discs need a UMD drive or a PSP; not a normal optical drive."},
        {Platform::Saturn, "ss", "Sega Saturn", {}},
        {Platform::MegaCD, "mcd", "Sega Mega CD / Sega CD", {}},
        {Platform::Dreamcast, "dc", "Sega Dreamcast", gd},
        {Platform::GameCube, "gc", "Nintendo GameCube", nintendo},
        {Platform::Wii, "wii", "Nintendo Wii", nintendo},
        {Platform::Xbox, "xbox", "Microsoft Xbox", xbox},
        {Platform::Xbox360, "xbox360", "Microsoft Xbox 360", xbox},
        {Platform::PCEngineCD, "pce", "NEC PC Engine CD / TurboGrafx CD", {}},
        {Platform::ThreeDO, "3do", "Panasonic 3DO", {}},
        {Platform::CDi, "cdi", "Philips CD-i", {}},
        {Platform::NeoGeoCD, "ngcd", "SNK Neo Geo CD", {}},
        {Platform::PC, "pc", "IBM PC compatible", {}},
        {Platform::CD32, "cd32", "Commodore Amiga CD32", {}},
        {Platform::CDTV, "cdtv", "Commodore Amiga CDTV", {}},
    };
    return v;
}

const PlatformInfo &platformInfo(Platform p) { return allPlatforms().at(int(p)); }
QString platformId(Platform p) { return platformInfo(p).id; }
QString platformName(Platform p) { return platformInfo(p).name; }

bool platformFromId(const QString &id, Platform &out)
{
    QString s = id.trimmed().toLower();
    static const QHash<QString, QString> alias = {
        {"ps1", "psx"}, {"saturn", "ss"}, {"segacd", "mcd"}, {"megacd", "mcd"}, {"gamecube", "gc"},
        {"gcn", "gc"}, {"dreamcast", "dc"}, {"x360", "xbox360"}, {"360", "xbox360"}, {"pcengine", "pce"},
        {"turbografx", "pce"}, {"neogeo", "ngcd"}, {"amiga", "cd32"}};
    s = alias.value(s, s);
    for (const auto &pi : allPlatforms())
        if (pi.id == s) { out = pi.platform; return true; }
    return false;
}

// ---------------------------------------------------------------- config

QJsonObject DriveProfile::toJson() const
{
    return QJsonObject{{"inquiry", inquiry}, {"readOffset", readOffset}, {"c2Shift", c2Shift},
                       {"pregapStart", pregapStart}, {"readMethod", readMethod},
                       {"sectorOrder", sectorOrder}};
}
DriveProfile DriveProfile::fromJson(const QJsonObject &o)
{
    DriveProfile d;
    d.inquiry = o["inquiry"].toString();
    d.readOffset = o["readOffset"].toString();
    d.c2Shift = o["c2Shift"].toString();
    d.pregapStart = o["pregapStart"].toString();
    d.readMethod = o["readMethod"].toString();
    d.sectorOrder = o["sectorOrder"].toString();
    return d;
}

QString appDir() { return QCoreApplication::applicationDirPath(); }
// Portable when portable.flag (or an existing config.json) sits beside the exe; otherwise per-user data,
// so an install under Program Files never needs to write to its own folder.
QString dataDir()
{
    static const QString d = [] {
        const QString exe = appDir();
        if (QFileInfo::exists(exe + "/portable.flag") || QFileInfo::exists(exe + "/config.json")) return exe;
        const QString base = qEnvironmentVariable("LOCALAPPDATA");
        if (base.isEmpty()) return exe;
        QDir().mkpath(base + "/psxrdc2");
        return base + "/psxrdc2";
    }();
    return d;
}
QString configPath() { return dataDir() + "/config.json"; }

Config loadConfig()
{
    Config c;
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return c;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    c.redumperPath = o["redumperPath"].toString();
    c.chdmanPath = o["chdmanPath"].toString();
    c.outputDir = o["outputDir"].toString(c.outputDir);
    c.lastDrive = o["lastDrive"].toString();
    c.speed = o["speed"].toString(c.speed);
    c.retries = o["retries"].toInt(c.retries);
    c.useProfile = o["useProfile"].toBool(true);
    c.autoRename = o["autoRename"].toBool(true);
    c.autoFix = o["autoFix"].toBool(true);
    const QJsonObject pr = o["profiles"].toObject();
    for (auto it = pr.begin(); it != pr.end(); ++it)
        c.profiles[it.key()] = DriveProfile::fromJson(it.value().toObject());
    return c;
}

void saveConfig(const Config &c)
{
    QJsonObject pr;
    for (auto it = c.profiles.begin(); it != c.profiles.end(); ++it)
        pr[it.key()] = it.value().toJson();
    const QJsonObject o{{"redumperPath", c.redumperPath}, {"chdmanPath", c.chdmanPath}, {"outputDir", c.outputDir},
                        {"lastDrive", c.lastDrive}, {"speed", c.speed}, {"retries", c.retries},
                        {"useProfile", c.useProfile}, {"autoRename", c.autoRename}, {"autoFix", c.autoFix}, {"profiles", pr}};
    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson());
}

bool importMpf(const QString &mpfConfig, Config &cfg, QString *err)
{
    QFile f(mpfConfig);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "cannot open " + mpfConfig;
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QString p = o["RedumperPath"].toString();
    if (!p.isEmpty() && QFileInfo(p).isRelative())
        p = QFileInfo(mpfConfig).absoluteDir().filePath(p);
    if (p.isEmpty() || !QFileInfo::exists(p)) {
        if (err) *err = "RedumperPath missing or not found in MPF config";
        return false;
    }
    cfg.redumperPath = QDir::fromNativeSeparators(p);
    return true;
}

QString findRedumper(const Config &cfg)
{
    if (!cfg.redumperPath.isEmpty() && QFileInfo::exists(cfg.redumperPath))
        return cfg.redumperPath;
    for (const QString &dir : {dataDir(), appDir()}) {
        const QString local = dir + "/redumper.exe";
        if (QFileInfo::exists(local)) return local;
    }
    return {};
}

// ---------------------------------------------------------------- DAT

QString datDir(Platform p) { return dataDir() + "/data/" + platformId(p); }
QString datUrl(Platform p) { return "http://redump.org/datfile/" + platformId(p) + "/"; }

QString findDatFile(Platform p)
{
    const QDir d(datDir(p));
    const QStringList dats = d.entryList({"*.dat"}, QDir::Files, QDir::Name);
    return dats.isEmpty() ? QString() : d.filePath(dats.last());
}

bool loadDat(Platform p, DatIndex &out, QString *err)
{
    const QString path = findDatFile(p);
    if (path.isEmpty()) {
        if (err) *err = "no .dat file in " + datDir(p);
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    out = DatIndex();
    out.file = QFileInfo(path).fileName();
    QXmlStreamReader xml(&f);
    QString curGame;
    bool inHeader = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto n = xml.name();
            if (n == u"header") inHeader = true;
            else if (inHeader && n == u"description") out.description = xml.readElementText();
            else if (n == u"game") curGame = xml.attributes().value("name").toString();
            else if (n == u"rom") {
                DatRom r;
                r.game = curGame;
                r.rom = xml.attributes().value("name").toString();
                r.size = xml.attributes().value("size").toLongLong();
                r.crc = xml.attributes().value("crc").toString().toLower();
                r.md5 = xml.attributes().value("md5").toString().toLower();
                r.sha1 = xml.attributes().value("sha1").toString().toLower();
                out.games[curGame].push_back(r);
                if (!r.md5.isEmpty()) { out.byMd5.insert(r.md5, r); out.md5Games.insert(r.md5, curGame); }
            }
        } else if (xml.isEndElement() && xml.name() == u"header") {
            inHeader = false;
        }
    }
    if (xml.hasError()) {
        if (err) *err = "dat parse error: " + xml.errorString();
        return false;
    }
    return out.loaded();
}

// ---------------------------------------------------------------- hashing

static quint32 g_crcTable[256];
static bool g_crcInit = false;
static void initCrc()
{
    if (g_crcInit) return;
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crcTable[i] = c;
    }
    g_crcInit = true;
}

bool hashFile(const QString &path, Hashes &out, const std::function<bool(qint64, qint64)> &progress)
{
    initCrc();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash md5(QCryptographicHash::Md5), sha1(QCryptographicHash::Sha1);
    quint32 crc = 0xFFFFFFFFu;
    const qint64 total = f.size();
    qint64 done = 0;
    QByteArray buf(8 << 20, 0);
    while (true) {
        const qint64 n = f.read(buf.data(), buf.size());
        if (n <= 0) break;
        md5.addData(QByteArrayView(buf.constData(), n));
        sha1.addData(QByteArrayView(buf.constData(), n));
        const auto *p = reinterpret_cast<const uchar *>(buf.constData());
        for (qint64 i = 0; i < n; ++i)
            crc = g_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        done += n;
        if (progress && !progress(done, total))
            return false;
    }
    out.size = total;
    out.md5 = md5.result().toHex();
    out.sha1 = sha1.result().toHex();
    out.crc = QString("%1").arg(crc ^ 0xFFFFFFFFu, 8, 16, QChar('0'));
    return true;
}

// ---------------------------------------------------------------- redumper log

static QString capture(const QString &text, const QString &pattern, int group = 1)
{
    const auto m = QRegularExpression(pattern, QRegularExpression::MultilineOption).match(text);
    return m.hasMatch() ? m.captured(group) : QString();
}

LogInfo parseRedumperLog(const QString &path)
{
    LogInfo li;
    li.path = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return li;
    const QString text = QString::fromUtf8(f.readAll());
    li.found = true;
    li.drive = capture(text, R"(inquiry: ([^\r\n]+))").trimmed();

    const QString cfgLine = capture(text, R"(read offset: [+-]?\d+[^\r\n]*)", 0);
    if (!cfgLine.isEmpty()) {
        li.readOffset = capture(cfgLine, R"(read offset: ([+-]?\d+))");
        li.c2Shift = capture(cfgLine, R"(C2 shift: (-?\d+))");
        li.pregapStart = capture(cfgLine, R"(pre-gap start: (-?\d+))");
        li.readMethod = capture(cfgLine, R"(read method: (\w+))");
        li.sectorOrder = capture(cfgLine, R"(sector order: (\w+))");
    }
    const QString autoOrder = capture(text, R"(auto-detected sector order: (\S+))");
    if (!autoOrder.isEmpty()) {
        li.sectorOrder = autoOrder;
        li.sectorOrderAuto = true;
    }
    auto num = [&](const QString &pat) {
        const QString s = capture(text, pat);
        return s.isEmpty() ? -1 : s.toInt();
    };
    li.scsi = num(R"(SCSI: (\d+) samples)");
    li.c2 = num(R"(C2: (\d+) samples)");
    li.q = num(R"(^\s*Q: (\d+)\s*$)");
    li.writeOffset = capture(text, R"(disc write offset: (-?\d+))");

    static const QRegularExpression trk(
        R"(errors detected, track: (\S+), sectors: \{SKIP: (\d+), C2: (\d+)\}, samples: \{SKIP: (\d+), C2: (\d+)\})");
    for (auto it = trk.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        li.trackErrors << QString("track %1: SKIP %2 sectors/%3 samples, C2 %4 sectors/%5 samples")
                              .arg(m.captured(1), m.captured(2), m.captured(4), m.captured(3), m.captured(5));
    }
    // The TOC read from the disc and the subchannel Q data disagree on where a track starts.
    // (A QTOC entry for the lead-out "track A" is normal and ignored.)
    static const QRegularExpression toc(
        R"(TOC / QTOC mismatch, track index (\d+) \(track: (\d+), LBA: (-?\d+) <=> (-?\d+)\))");
    for (auto it = toc.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString s = QString("track %1 index %2: the TOC says LBA %3, the subchannel says LBA %4")
                              .arg(m.captured(2), m.captured(1), m.captured(3), m.captured(4));
        if (!li.tocMismatches.contains(s)) li.tocMismatches << s;
    }
    static const QRegularExpression err(R"(^error: (.+)$)", QRegularExpression::MultilineOption);
    for (auto it = err.globalMatch(text); it.hasNext();) {
        const QString e = it.next().captured(1).trimmed();
        if (!li.errors.contains(e)) li.errors << e;
    }
    static const QRegularExpression warn(R"(^warning: (.+)$)", QRegularExpression::MultilineOption);
    static const QStringList noise = {"unable to read CD-TEXT", "TOC / QTOC", "incomplete pre-gap",
                                      "lead-in starts", "lead-out ends", "unscrambled"};
    for (auto it = warn.globalMatch(text); it.hasNext();) {
        const QString w = it.next().captured(1).trimmed();
        bool skip = false;
        for (const auto &n : noise) skip |= w.startsWith(n);
        if (!skip && !li.warnings.contains(w)) li.warnings << w;
    }
    return li;
}

LogInfo pickLog(const QString &folder)
{
    LogInfo best;
    int bestScore = -1;
    const QDir d(folder);
    for (const QString &n : d.entryList({"*.log"}, QDir::Files)) {
        if (QFileInfo(d.filePath(n)).size() > (64 << 20)) continue;
        const LogInfo li = parseRedumperLog(d.filePath(n));
        const int score = (li.sectorOrderAuto ? 5 : 0) + (!li.readOffset.isEmpty() ? 10 : 0)
                          + (li.scsi >= 0 ? 3 : 0);
        if (score > bestScore) { best = li; bestScore = score; }
    }
    return best;
}

DriveProfile profileFromLog(const LogInfo &li)
{
    DriveProfile d;
    // Only a CD dump where redumper detected the sector order says anything about the drive.
    // A DVD log (or one replaying an existing profile) only repeats redumper's generic default.
    if (!li.sectorOrderAuto) return d;
    d.inquiry = li.drive;
    d.readOffset = li.readOffset;
    d.c2Shift = li.c2Shift;
    d.pregapStart = li.pregapStart;
    d.readMethod = li.readMethod;
    d.sectorOrder = li.sectorOrder;
    return d;
}

// ---------------------------------------------------------------- verify

static QString extractSerial(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QString text = QString::fromLatin1(f.read(6 << 20));
    // Only SYSTEM.CNF's BOOT / BOOT2 line counts: game data holds plenty of other "cdrom:" strings.
    // Anything that does not look like a serial is ignored, never shown.
    const QRegularExpressionMatch m =
        QRegularExpression(R"(BOOT2?\s*=\s*cdrom0?:\\*([A-Z]{4})_(\d{3})\.(\d{2}))",
                           QRegularExpression::CaseInsensitiveOption).match(text);
    return m.hasMatch() ? m.captured(1).toUpper() + "-" + m.captured(2) + m.captured(3) : QString();
}

QString imageSerial(const QString &path) { return extractSerial(path); }

static QString romNameIn(const DatIndex &dat, const QString &game, const QString &md5)
{
    for (const auto &rom : dat.games.value(game))
        if (rom.md5 == md5) return rom.rom;
    return {};
}

VerifyResult verifyFolder(const QString &folder, Platform p, const DatIndex &dat,
                          const VerifyProgress &progress)
{
    VerifyResult r;
    r.folder = folder;
    const QDir d(folder);
    const QStringList names = d.entryList({"*.cue", "*.bin", "*.iso"}, QDir::Files, QDir::Name);
    if (names.isEmpty()) {
        r.overall = "UNKNOWN";
        r.health.push_back({"FAIL", "no .cue/.bin/.iso files in folder"});
        return r;
    }

    // Hash everything.
    QHash<QString, QVector<DatRom>> candidates;     // game -> matched roms
    qint64 totalBytes = 0, doneBytes = 0;
    for (const QString &n : names) totalBytes += QFileInfo(d.filePath(n)).size();
    for (const QString &n : names) {
        FileVerdict fv;
        fv.name = n;
        Hashes h;
        const qint64 base = doneBytes;
        // Hashing a multi-GB image takes a while: remember results so verifying the same
        // file against several databases (the library's auto-detect) hashes it once.
        static QHash<QString, Hashes> hashCache;
        static QMutex hashMutex;
        const QFileInfo hfi(d.filePath(n));
        const QString hkey = hfi.absoluteFilePath() + "|" + QString::number(hfi.size()) + "|"
                             + QString::number(hfi.lastModified().toMSecsSinceEpoch());
        bool ok = false;
        {
            QMutexLocker lk(&hashMutex);
            if (hashCache.contains(hkey)) { h = hashCache.value(hkey); ok = true; }
        }
        if (!ok) {
            ok = hashFile(d.filePath(n), h, [&](qint64 done, qint64) {
                return !progress || progress("hashing " + n, base + done, totalBytes);
            });
            if (ok) { QMutexLocker lk(&hashMutex); hashCache.insert(hkey, h); }
        }
        if (!ok) {
            r.overall = "UNKNOWN";
            r.health.push_back({"FAIL", "hashing cancelled or failed: " + n});
            return r;
        }
        doneBytes += h.size;
        fv.size = h.size;
        fv.md5 = h.md5;
        const QStringList gs = dat.md5Games.values(h.md5);
        if (!gs.isEmpty()) {
            fv.verdict = "MATCH";
            fv.game = gs.first();
            fv.detail = "dat: " + dat.byMd5.value(h.md5).rom;
            for (const QString &g : gs) candidates[g].push_back(dat.byMd5.value(h.md5));
        } else {
            fv.verdict = "UNKNOWN";
        }
        r.files.push_back(fv);
    }

    // Pick the game: the one matching the most data files.
    QString best;
    int bestCount = 0;
    for (auto it = candidates.begin(); it != candidates.end(); ++it)
        if (it->size() > bestCount || (it->size() == bestCount && it.key() < best)) { best = it.key(); bestCount = it->size(); }
    if (!best.isEmpty()) {
        r.game = best;
        r.identifiedBy = "md5";
        // Several releases can share a hash; label every file against the chosen game.
        for (auto &f : r.files) {
            if (f.verdict != "MATCH") continue;
            if (!dat.md5Games.contains(f.md5, best)) { f.verdict = "UNKNOWN"; f.game.clear(); f.detail.clear(); continue; }
            f.game = best;
            for (const auto &rom : dat.games.value(best))
                if (rom.md5 == f.md5) { f.detail = "dat: " + rom.rom; break; }
        }
    } else {
        // Fall back to the layout: same list of bin/iso sizes.
        QVector<qint64> sizes;
        for (const auto &f : r.files)
            if (!f.name.endsWith(".cue", Qt::CaseInsensitive)) sizes << f.size;
        std::sort(sizes.begin(), sizes.end());
        for (auto it = dat.games.begin(); it != dat.games.end(); ++it) {
            QVector<qint64> gs;
            for (const auto &rom : it.value())
                if (!rom.isCue()) gs << rom.size;
            std::sort(gs.begin(), gs.end());
            if (gs == sizes && !gs.isEmpty()) { r.game = it.key(); r.identifiedBy = "layout"; break; }
        }
    }

    // Serial from the first data file (cheap sanity info).
    for (const auto &f : r.files)
        if (!f.name.endsWith(".cue", Qt::CaseInsensitive)) {
            r.serial = extractSerial(d.filePath(f.name));
            if (!r.serial.isEmpty()) break;
        }

    if (r.game.isEmpty()) {
        r.overall = "UNKNOWN";
        r.health.push_back({"WARN", "no dat entry matches by hash or layout (not in Redump, or a bad dump)"});
    } else {
        // Compare each file to the identified game's roms.
        const auto roms = dat.games.value(r.game);

        // A cue embeds the bin file names, so a dump named differently from
        // Redump (e.g. by serial) never hashes the same. Rename the FILE
        // entries to the dat names (bins matched by hash) and hash again.
        QHash<QString, QString> rename;     // local bin name (lower) -> dat name
        for (const auto &f : r.files)
            if (f.verdict == "MATCH" && f.game == r.game && !f.name.endsWith(".cue", Qt::CaseInsensitive))
                rename.insert(f.name.toLower(), romNameIn(dat, r.game, f.md5));
        for (auto &f : r.files) {
            if (f.verdict == "MATCH" || !f.name.endsWith(".cue", Qt::CaseInsensitive)) continue;
            QFile cf(d.filePath(f.name));
            if (!cf.open(QIODevice::ReadOnly)) continue;
            QByteArray text = cf.readAll();
            static const QRegularExpression fileRe(R"re(FILE\s+"([^"]+)")re");
            QString s = QString::fromUtf8(text);
            QString rebuilt;
            qsizetype last = 0;
            for (auto it = fileRe.globalMatch(s); it.hasNext();) {
                const auto m = it.next();
                const QString to = rename.value(m.captured(1).toLower());
                if (to.isEmpty()) continue;
                rebuilt += s.mid(last, m.capturedStart(1) - last) + to;
                last = m.capturedEnd(1);
            }
            rebuilt += s.mid(last);
            const QString md5 = QCryptographicHash::hash(rebuilt.toUtf8(), QCryptographicHash::Md5).toHex();
            const auto it = dat.byMd5.constFind(md5);
            if (it != dat.byMd5.constEnd() && dat.md5Games.contains(md5, r.game)) {
                f.verdict = "MATCH";
                f.game = r.game;
                f.detail = "matches dat after renaming FILE entries to " + it->rom;
                candidates[r.game].push_back(*it);
            }
        }
        QHash<QString, DatRom> byName;
        for (const auto &rom : roms) byName.insert(rom.rom.toLower(), rom);
        bool allMatch = true;
        for (auto &f : r.files) {
            if (f.verdict == "MATCH" && f.game == r.game) continue;
            // Find the expected rom: same extension and same slot.
            const QString ext = QFileInfo(f.name).suffix().toLower();
            QVector<DatRom> same;
            for (const auto &rom : roms)
                if (QFileInfo(rom.rom).suffix().toLower() == ext) same << rom;
            f.verdict = "MISMATCH";
            if (same.size() == 1) {
                f.detail = QString("expected md5 %1 size %2, got size %3")
                               .arg(same[0].md5).arg(same[0].size).arg(f.size);
                if (same[0].size == f.size) f.detail += " (same size: read errors or offset/sector order)";
            } else {
                f.detail = "hash not in dat for " + r.game;
            }
            allMatch = false;
        }
        // Missing roms.
        for (const auto &rom : roms) {
            const QString ext = QFileInfo(rom.rom).suffix().toLower();
            bool have = false;
            for (const auto &f : r.files) have |= (f.md5 == rom.md5);
            bool slot = false;
            for (const auto &f : r.files) slot |= (QFileInfo(f.name).suffix().toLower() == ext);
            if (!have && !slot) {
                r.health.push_back({"FAIL", "missing file for dat entry: " + rom.rom});
                allMatch = false;
            }
        }
        r.overall = allMatch ? "PASS" : "FAIL";
    }

    // Dat-independent check: an audio track that opens with a short burst of non-silent
    // bytes followed by digital silence has had data leaked in by a wrong write offset.
    for (const auto &f : r.files) {
        if (!f.name.endsWith(".cue", Qt::CaseInsensitive)) continue;
        QFile cf(d.filePath(f.name));
        if (!cf.open(QIODevice::ReadOnly)) continue;
        const QString cue = QString::fromUtf8(cf.readAll());
        static const QRegularExpression re(
            R"re(FILE\s+"([^"]+)"[^\n]*\n\s*TRACK\s+(\d+)\s+(\w+))re", QRegularExpression::CaseInsensitiveOption);
        for (auto m = re.globalMatch(cue); m.hasNext();) {
            const auto mm = m.next();
            if (mm.captured(3).compare("AUDIO", Qt::CaseInsensitive) != 0) continue;
            QFile af(d.filePath(mm.captured(1)));
            if (!af.open(QIODevice::ReadOnly)) continue;
            const QByteArray head = af.read(4096);
            // first window of one sector (2352 bytes) of pure silence, searched within the first 256 bytes
            int silenceAt = -1;
            for (int i = 1; i <= 256 && i + 2352 <= head.size(); ++i) {
                bool zero = true;
                for (int k = i; k < i + 2352 && zero; ++k) zero = head[k] == 0;
                if (zero) { silenceAt = i; break; }
            }
            if (silenceAt < 0) continue;
            bool burst = false;
            for (int k = 0; k < silenceAt; ++k) burst |= head[k] != 0;
            if (!burst) continue;
            const int samples = (silenceAt + 3) / 4;
            r.health.push_back({"WARN", QString("audio track %1 starts with %2 bytes (about %3 samples) of non-silent "
                                                "data, then silence: data from the previous track has probably leaked "
                                                "in through a wrong write offset (try verify --diagnose)")
                                            .arg(mm.captured(2)).arg(silenceAt).arg(samples)});
        }
    }

    // Log health.
    const LogInfo li = pickLog(folder);
    if (!li.found) {
        r.health.push_back({"INFO", "no redumper log in folder"});
    } else {
        if (!li.drive.isEmpty()) r.health.push_back({"INFO", "drive: " + li.drive});
        if (!li.sectorOrder.isEmpty())
            r.health.push_back({"INFO", QString("sector order: %1%2").arg(li.sectorOrder,
                                li.sectorOrderAuto ? " (auto-detected)" : "")});
        if (li.c2 >= 0)
            r.health.push_back({li.c2 == 0 ? "OK" : "WARN", QString("C2 errors: %1").arg(li.c2)});
        if (li.scsi >= 0)
            r.health.push_back({li.scsi == 0 ? "OK" : "WARN", QString("SCSI errors: %1").arg(li.scsi)});
        if (li.q >= 0)
            r.health.push_back({li.q == 0 ? "OK" : "INFO", QString("subchannel Q errors: %1").arg(li.q)});
        if (!li.writeOffset.isEmpty())
            r.health.push_back({"INFO", "disc write offset: " + li.writeOffset});
        for (const auto &t : li.tocMismatches)
            r.health.push_back({"FAIL", "table of contents mismatch: " + t +
                                        ". Track boundaries may be wrong; re-dump at a fixed 24x or slower"});
        for (const auto &t : li.trackErrors) r.health.push_back({"WARN", t});
        for (const auto &e : li.errors) r.health.push_back({"WARN", "log error: " + e});
        for (const auto &w : li.warnings.mid(0, 6)) r.health.push_back({"INFO", "log warning: " + w});
    }
    Q_UNUSED(p);
    return r;
}

QJsonObject VerifyResult::toJson() const
{
    QJsonArray fa, ha;
    for (const auto &f : files)
        fa.append(QJsonObject{{"name", f.name}, {"verdict", f.verdict}, {"md5", f.md5},
                              {"size", double(f.size)}, {"game", f.game}, {"detail", f.detail}});
    for (const auto &h : health)
        ha.append(QJsonObject{{"level", h.level}, {"msg", h.msg}});
    return QJsonObject{{"folder", folder}, {"overall", overall}, {"game", game},
                       {"serial", serial}, {"identifiedBy", identifiedBy},
                       {"files", fa}, {"health", ha}};
}

QString VerifyResult::toText() const
{
    QString s;
    s += "Folder : " + folder + "\n";
    s += "Result : " + overall + "\n";
    s += "Game   : " + (game.isEmpty() ? QString("(unidentified)") : game);
    if (!identifiedBy.isEmpty()) s += "  [by " + identifiedBy + "]";
    s += "\n";
    if (!serial.isEmpty()) s += "Serial : " + serial + "\n";
    s += "\nFiles:\n";
    for (const auto &f : files) {
        s += QString("  %1  %2  %3\n").arg(f.verdict, -8).arg(f.name).arg(f.md5);
        if (!f.detail.isEmpty()) s += "            " + f.detail + "\n";
    }
    if (!health.isEmpty()) {
        s += "\nHealth:\n";
        for (const auto &h : health) s += QString("  [%1] %2\n").arg(h.level, -4).arg(h.msg);
    }
    return s;
}

// ---------------------------------------------------------------- audio offset diagnosis

QVector<ShiftFix> findAudioShifts(const QString &folder, const VerifyResult &r, const DatIndex &dat, int maxSamples)
{
    QVector<ShiftFix> out;
    const QDir d(folder);
    const auto roms = dat.games.value(r.game);
    for (const auto &f : r.files) {
        if (f.verdict != "MISMATCH" || f.name.endsWith(".cue", Qt::CaseInsensitive)) continue;
        QVector<DatRom> targets;
        for (const auto &rom : roms)
            if (!rom.isCue() && rom.size == f.size && rom.md5 != f.md5) targets << rom;
        if (targets.isEmpty()) continue;
        QFile file(d.filePath(f.name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = file.readAll();
        file.close();
        for (int s = -maxSamples; s <= maxSamples; ++s) {
            if (s == 0) continue;
            const qint64 bytes = qint64(qAbs(s)) * 4;
            if (bytes >= data.size()) continue;
            QCryptographicHash h(QCryptographicHash::Md5);
            const QByteArray zeros(int(bytes), 0);
            if (s > 0) { h.addData(QByteArrayView(data.constData() + bytes, data.size() - bytes)); h.addData(zeros); }
            else { h.addData(zeros); h.addData(QByteArrayView(data.constData(), data.size() - bytes)); }
            const QString md5 = h.result().toHex();
            for (const auto &t : targets)
                if (md5 == t.md5) { out.push_back({f.name, t.rom, s, md5}); goto nextFile; }
        }
        nextFile:;
    }
    return out;
}

bool applyShift(const QString &folder, const ShiftFix &fix, QString *err)
{
    const QDir d(folder);
    QFile file(d.filePath(fix.file));
    if (!file.open(QIODevice::ReadOnly)) { if (err) *err = "cannot read " + fix.file; return false; }
    const QByteArray data = file.readAll();
    file.close();
    const qint64 bytes = qint64(qAbs(fix.samples)) * 4;
    QByteArray fixed;
    fixed.reserve(data.size());
    if (fix.samples > 0) { fixed.append(data.constData() + bytes, data.size() - bytes); fixed.append(QByteArray(int(bytes), 0)); }
    else { fixed.append(QByteArray(int(bytes), 0)); fixed.append(data.constData(), data.size() - bytes); }
    if (QCryptographicHash::hash(fixed, QCryptographicHash::Md5).toHex() != fix.md5) {
        if (err) *err = "internal check failed (hash differs)";
        return false;
    }
    QDir().mkpath(d.filePath("_original"));
    if (!QFile::copy(d.filePath(fix.file), d.filePath("_original/" + fix.file))) {
        if (err) *err = "cannot keep original in _original/";
        return false;
    }
    QFile outf(d.filePath(fix.file));
    if (!outf.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = "cannot write " + fix.file; return false; }
    outf.write(fixed);
    return true;
}

// ---------------------------------------------------------------- rename

static QString safeName(QString s)
{
    for (const QChar ch : {'<', '>', ':', '"', '/', '\\', '|', '?', '*'}) s.replace(ch, '_');
    return s.trimmed();
}

RenameResult renameToRedump(const VerifyResult &r, const DatIndex &dat)
{
    RenameResult out;
    out.folder = r.folder;
    if (r.overall != "PASS" || r.game.isEmpty()) {
        out.error = "only fully verified (PASS) dumps are renamed";
        return out;
    }
    {
        // Refuse before touching any file if the destination folder is taken.
        QDir parent(r.folder);
        parent.cdUp();
        const QString target = safeName(r.game);
        if (QFileInfo(r.folder).fileName() != target && QFileInfo::exists(parent.filePath(target))) {
            out.error = "a folder named \"" + target + "\" already exists next to this one; nothing was renamed";
            return out;
        }
    }
    QDir d(r.folder);
    const auto roms = dat.games.value(r.game);
    QString datCue;
    for (const auto &rom : roms) if (rom.isCue()) { datCue = rom.rom; break; }

    // old name -> Redump name
    QHash<QString, QString> map;
    QString oldBase;
    for (const auto &f : r.files) {
        if (f.verdict != "MATCH") { out.error = "file not verified: " + f.name; return out; }
        const bool isCue = f.name.endsWith(".cue", Qt::CaseInsensitive);
        const QString to = isCue ? datCue : romNameIn(dat, r.game, f.md5);
        if (to.isEmpty()) { out.error = "no Redump name for " + f.name; return out; }
        map.insert(f.name, to);
        // The image's base name prefixes redumper's sidecar files (.log, .state, ...); a DVD
        // dump has an .iso and no cue, so fall back to that.
        if (isCue || (oldBase.isEmpty() && f.name.endsWith(".iso", Qt::CaseInsensitive)))
            oldBase = QFileInfo(f.name).completeBaseName();
    }
    if (datCue.isEmpty())
        for (const auto &rom : roms) if (rom.rom.endsWith(".iso", Qt::CaseInsensitive)) { datCue = rom.rom; break; }
    // Two-phase rename through temporary names so swapped names cannot collide.
    QHash<QString, QString> staged;     // temp -> final
    int n = 0;
    auto stage = [&](const QString &from, const QString &to) -> bool {
        if (from == to) return true;
        if (QFileInfo::exists(d.filePath(to)) && !map.contains(to) && to.compare(from, Qt::CaseInsensitive) != 0) {
            out.error = "target already exists: " + to;
            return false;
        }
        const QString tmp = QString(".psxrdc_tmp_%1").arg(n++);
        if (!d.rename(from, tmp)) { out.error = "cannot rename " + from; return false; }
        staged.insert(tmp, to);
        out.actions << from + "  ->  " + to;
        return true;
    };

    // Rewrite the cue's FILE entries first (cue content must use the new bin names).
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (!it.key().endsWith(".cue", Qt::CaseInsensitive)) continue;
        QFile cf(d.filePath(it.key()));
        if (!cf.open(QIODevice::ReadOnly)) { out.error = "cannot read " + it.key(); return out; }
        const QString s = QString::fromUtf8(cf.readAll());
        cf.close();
        static const QRegularExpression fileRe(R"re(FILE\s+"([^"]+)")re");
        QString rebuilt;
        qsizetype last = 0;
        for (auto m = fileRe.globalMatch(s); m.hasNext();) {
            const auto mm = m.next();
            QString to;
            for (auto b = map.begin(); b != map.end(); ++b)
                if (b.key().compare(mm.captured(1), Qt::CaseInsensitive) == 0) to = b.value();
            if (to.isEmpty()) continue;
            rebuilt += s.mid(last, mm.capturedStart(1) - last) + to;
            last = mm.capturedEnd(1);
        }
        rebuilt += s.mid(last);
        if (!cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) { out.error = "cannot write " + it.key(); return out; }
        cf.write(rebuilt.toUtf8());
        cf.close();
    }
    for (auto it = map.begin(); it != map.end(); ++it)
        if (!stage(it.key(), it.value())) { out.error += " (partial rename, check folder)"; return out; }

    // Sidecar files from redumper: <oldBase>.log, .scram, .state, ..., <oldBase>_redumper_stdout.log
    const QString newBase = QFileInfo(datCue).completeBaseName();
    if (!oldBase.isEmpty() && oldBase != newBase) {
        for (const QString &n2 : d.entryList(QDir::Files)) {
            if (map.contains(n2) || n2.startsWith(".psxrdc_tmp_")) continue;
            if (n2.startsWith(oldBase + ".") || n2.startsWith(oldBase + "_"))
                if (!stage(n2, newBase + n2.mid(oldBase.size()))) { out.error += " (partial rename, check folder)"; return out; }
        }
    }
    for (auto it = staged.begin(); it != staged.end(); ++it)
        if (!d.rename(it.key(), it.value())) { out.error = "cannot finalize " + it.value(); return out; }

    // Finally the folder itself.
    QDir parent(r.folder);
    parent.cdUp();
    const QString folderName = safeName(r.game);
    if (QFileInfo(r.folder).fileName() != folderName) {
        if (QFileInfo::exists(parent.filePath(folderName))) {
            out.error = "folder already exists: " + parent.filePath(folderName);
            return out;
        }
        if (!parent.rename(QFileInfo(r.folder).fileName(), folderName)) {
            out.error = "cannot rename folder";
            return out;
        }
        out.actions << QFileInfo(r.folder).fileName() + "/  ->  " + folderName + "/";
        out.folder = parent.filePath(folderName);
    }
    // Keep the library record (.psxrdc.json) in step with the new file names.
    {
        QFile jf(QDir(out.folder).filePath(".psxrdc.json"));
        if (jf.open(QIODevice::ReadOnly)) {
            QJsonObject o = QJsonDocument::fromJson(jf.readAll()).object();
            jf.close();
            QJsonArray files;
            for (const auto &v : o["files"].toArray()) {
                QJsonObject fo = v.toObject();
                const QString nn = map.value(fo["name"].toString());
                if (!nn.isEmpty()) fo["name"] = nn;
                files.append(fo);
            }
            o["files"] = files;
            o["folder"] = out.folder;
            if (jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) jf.write(QJsonDocument(o).toJson());
        }
    }
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------- drives / args

QVector<DriveInfo> listOpticalDrives()
{
    QVector<DriveInfo> out;
#ifdef Q_OS_WIN
    for (wchar_t c = L'A'; c <= L'Z'; ++c) {
        const wchar_t root[4] = {c, L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_CDROM) continue;
        wchar_t label[256] = {0};
        DriveInfo di;
        di.letter = QString(QChar(c)) + ":";
        if (GetVolumeInformationW(root, label, 256, nullptr, nullptr, nullptr, nullptr, 0))
            di.label = QString::fromWCharArray(label);
        out.push_back(di);
    }
#endif
    return out;
}

DiscProbe probeDisc(const QString &drive)
{
    DiscProbe p;
    p.bytes = QStorageInfo(drive + "/").bytesTotal();
    p.isDvd = p.bytes > 750000000;
    QFile f(drive + "/SYSTEM.CNF");
    if (!f.open(QIODevice::ReadOnly)) return p;
    const QString text = QString::fromLatin1(f.read(4096));
    p.ok = true;
    static const QRegularExpression re(R"re(BOOT(2?)\s*=\s*cdrom0?:\\?([A-Za-z]{4})[_-]?(\d{3})\.?(\d{2}))re",
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (m.hasMatch()) {
        p.isPs2 = m.captured(1) == "2";
        p.serial = m.captured(2).toUpper() + "-" + m.captured(3) + m.captured(4);
    }
    return p;
}

int lastC2Samples(const DumpRequest &r)
{
    QFile f(QDir(dumpFolder(r)).filePath(r.name + "_redumper_stdout.log"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QString text = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(R"(C2:\s*(\d+) samples)");
    int last = -1;
    for (auto it = re.globalMatch(text); it.hasNext();) last = it.next().captured(1).toInt();
    return last;
}

QVector<int> eccBadLbas(const QString &redumper, const DumpRequest &r)
{
    QVector<int> lbas;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
    p.start(redumper, {"info", "--image-path=" + dumpFolder(r), "--image-name=" + r.name});
    if (!p.waitForFinished(120000)) { p.kill(); return lbas; }
    const QString text = QString::fromUtf8(p.readAll());
    static const QRegularExpression line(R"((?:ECC|EDC) errors:\s*\d+\s*\(LBA:\s*([^)]*)\))");
    static const QRegularExpression item(R"((\d+)(?:\s*-\s*(\d+))?)");
    for (auto m = line.globalMatch(text); m.hasNext();) {
        const QString list = m.next().captured(1);
        for (auto it = item.globalMatch(list); it.hasNext();) {
            const auto n = it.next();
            const int a = n.captured(1).toInt();
            const int b = n.captured(2).isEmpty() ? a : n.captured(2).toInt();
            for (int l = a; l <= qMin(b, a + 5000); ++l)
                if (!lbas.contains(l)) lbas << l;
        }
    }
    return lbas;
}

bool markForReread(const DumpRequest &r, const QVector<int> &lbas)
{
    // The .state file holds one byte per sample, 588 per sector, starting at LBA -45150; 1 = C2 error.
    QFile f(QDir(dumpFolder(r)).filePath(r.name + ".state"));
    if (!f.open(QIODevice::ReadWrite)) return false;
    const qint64 perSector = 588, first = -45150, margin = 3;
    for (int lba : lbas) {
        const qint64 start = qMax<qint64>(0, (lba - first - margin) * perSector);
        const qint64 end = qMin<qint64>(f.size(), (lba - first + margin + 1) * perSector);
        if (end <= start || !f.seek(start)) continue;
        f.write(QByteArray(int(end - start), '\x01'));
    }
    return true;
}

QString dumpFolder(const DumpRequest &r) { return QDir(r.outDir).filePath(r.name); }

QStringList buildDumpArgs(const DumpRequest &r)
{
    if (r.command == "split") {
        // Rebuild the bin/cue from the existing dump; needs no drive.
        QStringList s{"split", "--image-path=" + dumpFolder(r), "--image-name=" + r.name, "--overwrite"};
        return s;
    }
    QStringList a{r.command, "--drive=" + r.drive, "--image-path=" + dumpFolder(r),
                  "--image-name=" + r.name};
    // CD speeds (24x) are not valid DVD speeds: DVDs always use redumper's optimal speed.
    if (!r.speed.isEmpty() && !r.dvd) a << "--speed=" + r.speed;
    a << "--retries=" + QString::number(r.retries);
    if (r.dvd) {
        // DVD: no sector order / read offset / pre-gap handling, redumper reads logical sectors.
    } else if (r.profile.valid()) {
        a << "--drive-read-offset=" + r.profile.readOffset;
        if (!r.profile.c2Shift.isEmpty()) a << "--drive-c2-shift=" + r.profile.c2Shift;
        if (!r.profile.pregapStart.isEmpty()) a << "--drive-pregap-start=" + r.profile.pregapStart;
        if (!r.profile.readMethod.isEmpty()) a << "--drive-read-method=" + r.profile.readMethod;
        if (!r.profile.sectorOrder.isEmpty()) a << "--drive-sector-order=" + r.profile.sectorOrder;
    } else {
        a << "--auto-detect";
    }
    if (!r.dvd) a << "--generic-skip-leadin";      // any CD on a generic drive, PS1 or PS2
    if (r.overreadLeadout && !r.dvd) a << "--overread-leadout";
    if (r.overwrite) a << "--overwrite";
    a << r.extraArgs;
    return a;
}

} // namespace core
