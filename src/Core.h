#pragma once
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace core {

// Every system a Redump database exists for that this tool knows. The id doubles as the
// redump.org datfile slug and the folder name under data/.
enum class Platform {
    PSX, PS2, PS3, PSP, Saturn, MegaCD, Dreamcast, GameCube, Wii, Xbox, Xbox360,
    PCEngineCD, ThreeDO, CDi, NeoGeoCD, PC, CD32, CDTV, Count
};

struct PlatformInfo {
    Platform platform;
    QString id;             // "psx", "ps2", "ss", "gc", ...: also the redump.org slug
    QString name;
    QString dumpNote;       // what the drive must be able to do (empty = a normal CD/DVD drive works)
};
const QVector<PlatformInfo> &allPlatforms();
const PlatformInfo &platformInfo(Platform p);
QString platformId(Platform p);
QString platformName(Platform p);
bool platformFromId(const QString &id, Platform &out);   // also accepts "ps1", "saturn", "gamecube", ...
constexpr int kPlatformCount = int(Platform::Count);

// ---------------------------------------------------------------- config

// What redumper learned about a specific drive. Replaying these as overrides
// skips the (slow, sometimes flaky) autodetection on every dump.
struct DriveProfile {
    QString inquiry;        // drive identity the profile belongs to
    QString readOffset;
    QString c2Shift;
    QString pregapStart;
    QString readMethod;
    QString sectorOrder;
    bool valid() const { return !inquiry.isEmpty() && !readOffset.isEmpty(); }
    QJsonObject toJson() const;
    static DriveProfile fromJson(const QJsonObject &o);
};

struct Config {
    QString redumperPath;
    QString chdmanPath;         // CHD compression tool (optional)
    QString outputDir = "E:/PSX/RIP";
    QString lastDrive;
    QString speed = "24";   // fixed: redumper's automatic speed gave unreliable subchannel on a generic drive
    int retries = 8;
    bool useProfile = true;
    bool autoRename = true;     // rename PASSed dumps to their Redump names
    bool autoFix = true;        // apply a hash-verified audio offset fix after a dump
    QMap<QString, DriveProfile> profiles;   // key: drive letter
};

QString appDir();       // folder of the exe
QString dataDir();      // config, DATs, downloaded tools: the exe folder when portable, else %LOCALAPPDATA%\psxrdc2
QString configPath();
Config loadConfig();
void saveConfig(const Config &c);
// Read RedumperPath / preferred CD speed out of an MPF config.json.
bool importMpf(const QString &mpfConfig, Config &cfg, QString *err);
QString findRedumper(const Config &cfg);

// ---------------------------------------------------------------- DAT

struct DatRom {
    QString game, rom, crc, md5, sha1;
    qint64 size = 0;
    bool isCue() const { return rom.endsWith(".cue", Qt::CaseInsensitive); }
};

struct DatIndex {
    QString file, description;
    QHash<QString, DatRom> byMd5;
    QMap<QString, QVector<DatRom>> games;
    QMultiHash<QString, QString> md5Games;      // md5 -> every game that has it
    bool loaded() const { return !games.isEmpty(); }
};

QString datDir(Platform p);
QString findDatFile(Platform p);
bool loadDat(Platform p, DatIndex &out, QString *err);
QString datUrl(Platform p);

// ---------------------------------------------------------------- hashing

struct Hashes {
    qint64 size = 0;
    QString md5, sha1, crc;
};
// progress(done,total) returns false to cancel.
bool hashFile(const QString &path, Hashes &out,
              const std::function<bool(qint64, qint64)> &progress = {});

// ---------------------------------------------------------------- redumper log

struct LogInfo {
    bool found = false;
    QString drive, sectorOrder, readMethod;
    QString readOffset, c2Shift, pregapStart;
    bool sectorOrderAuto = false;
    int scsi = -1, c2 = -1, q = -1;
    QString writeOffset;
    QStringList errors, warnings, trackErrors;
    QStringList tocMismatches;      // "track 2: TOC says LBA 229170, subchannel says 228975"
    QString path;
};
LogInfo parseRedumperLog(const QString &path);
LogInfo pickLog(const QString &folder);
DriveProfile profileFromLog(const LogInfo &li);

// ---------------------------------------------------------------- verify

struct FileVerdict {
    QString name, verdict;      // MATCH / MISMATCH / UNKNOWN
    QString md5, game, detail;
    qint64 size = 0;
};
struct HealthItem {
    QString level;              // OK / INFO / WARN / FAIL
    QString msg;
};
struct VerifyResult {
    QString folder;
    QString overall;            // PASS / FAIL / UNKNOWN
    QString game, serial, identifiedBy;
    QVector<FileVerdict> files;
    QVector<HealthItem> health;
    QJsonObject toJson() const;
    QString toText() const;
};

using VerifyProgress = std::function<bool(const QString &stage, qint64 done, qint64 total)>;
VerifyResult verifyFolder(const QString &folder, Platform p, const DatIndex &dat,
                          const VerifyProgress &progress = {});

// Rename a PASSed dump (folder, bins, cue, redumper sidecar files) to the exact
// Redump names, e.g. "Castlevania - Symphony of the Night (USA)". Only touches
// dumps whose every file matched the dat.
struct RenameResult {
    bool ok = false;
    QString folder;             // folder path after the rename
    QStringList actions;
    QString error;
};
RenameResult renameToRedump(const VerifyResult &r, const DatIndex &dat);

// ---------------------------------------------------------------- audio offset diagnosis

// An audio track whose bytes equal a Redump track after sliding the whole file
// by N samples (zero-filling the end that slid in/out). This is what a wrong
// write offset looks like on a drive that cannot read the lead-out.
struct ShiftFix {
    QString file;               // local file name
    QString datRom;             // Redump track it becomes
    int samples = 0;            // >0: content moves earlier (drop head, zero-fill tail)
    QString md5;                // resulting md5 (== dat)
};
QVector<ShiftFix> findAudioShifts(const QString &folder, const VerifyResult &r, const DatIndex &dat,
                                  int maxSamples = 96);
// Writes the corrected file in place; the original is kept in <folder>/_original/.
bool applyShift(const QString &folder, const ShiftFix &fix, QString *err);

// ---------------------------------------------------------------- drives / redumper args

struct DriveInfo {
    QString letter;     // "H:"
    QString label;      // volume label, empty if no disc
};
QVector<DriveInfo> listOpticalDrives();

// What Windows can read from the disc without touching the dump: the game's boot
// file name from SYSTEM.CNF (a serial like SLUS-00551 / SLES-50001).
struct DiscProbe {
    bool ok = false;        // SYSTEM.CNF could be read
    bool isPs2 = false;     // BOOT2 (PS2) rather than BOOT (PS1)
    QString serial;
    qint64 bytes = 0;       // size Windows reports for the disc
    bool isDvd = false;     // bigger than any CD (> 750 MB)
};
DiscProbe probeDisc(const QString &drive);

struct DumpRequest {
    Platform platform = Platform::PSX;
    QString drive, name, outDir, speed;
    int retries = 8;
    bool overwrite = false;
    bool dvd = false;               // DVD media: no CD drive profile / CD-only options
    QStringList extraArgs;          // passed to redumper unchanged (advanced)
    QString command = "disc";       // "disc" (full dump), "refine" (re-read bad sectors) or "split"
    bool overreadLeadout = false;   // read the lead-out past 100 sectors (generic drives)
    DriveProfile profile;       // applied when valid()
};
QStringList buildDumpArgs(const DumpRequest &r);
// C2 error samples redumper reported in its most recent pass (-1 if the log has none).
int lastC2Samples(const DumpRequest &r);
// Sectors of a finished CD dump whose data fails the ECC/EDC check (from `redumper info`).
// These carry no C2 flag, so refine never retries them by itself.
QVector<int> eccBadLbas(const QString &redumper, const DumpRequest &r);
// Flags those sectors (and a few neighbours) as unread in the .state file so the next refine re-reads them.
bool markForReread(const DumpRequest &r, const QVector<int> &lbas);
// Serial from the BOOT/BOOT2 line of SYSTEM.CNF inside an iso or bin (empty if there is none).
QString imageSerial(const QString &path);
QString dumpFolder(const DumpRequest &r);

} // namespace core
