#include "Cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QTextStream>

#include "Core.h"
#include "Chd.h"
#include "Library.h"
#include "Runner.h"
#include "Setup.h"
#include "Submission.h"

namespace cli {

static QTextStream &out() { static QTextStream s(stdout); return s; }
static QTextStream &err() { static QTextStream s(stderr); return s; }

static const QStringList kCommands = {"verify", "dump", "drives", "update-dat", "config", "help",
                                      "install-menu", "uninstall-menu", "library", "compress", "setup", "dic-dump", "submission", "--help", "-h", "--version"};

bool wantsCli(const QStringList &args) { return args.size() > 1 && kCommands.contains(args[1]); }

static void usage()
{
    out() << "psxrdc2 - PlayStation / PlayStation 2 dump tool (no arguments opens the GUI)\n\n"
             "  psxrdc2 verify <folder> [--platform psx|ps2] [--json] [--diagnose] [--fix] [--rename]\n"
             "  psxrdc2 dump --drive H: --name NAME [--platform psx|ps2] [--out DIR]\n"
             "               [--speed N] [--retries N] [--overwrite] [--overread-leadout]\n"
             "               [--no-profile] [--no-verify] [--no-rename]\n"
             "  psxrdc2 --dump --drive H: --name NAME [--system psx|ps2] ...   (same, in the GUI window)\n"
             "  psxrdc2 library [FOLDER] [--verify]     list every dump with its Redump status\n"
             "  psxrdc2 compress <folder> [--delete-originals] [--delete-scratch]   (needs chdman.exe)\n"
             "  psxrdc2 setup [redumper|chdman] [--yes]   show / download the tools this program does not contain\n"
             "  psxrdc2 drives\n"
             "  psxrdc2 update-dat [--platform ID]      ID: psx ps2 ps3 psp ss mcd dc gc wii xbox xbox360 pce 3do cdi ngcd pc cd32 cdtv\n"
             "  psxrdc2 config [--redumper PATH] [--mpf MPF_CONFIG_JSON] [--out DIR] [--speed N]\n"
             "  psxrdc2 install-menu | uninstall-menu    (Explorer right-click on folders)\n\n"
             "Exit codes: 0 PASS, 1 FAIL, 2 UNKNOWN, 3 error.\n";
    out().flush();
}

struct Opts {
    QStringList pos;
    QHash<QString, QString> kv;
    QSet<QString> flags;
};

static Opts parse(const QStringList &args)
{
    static const QSet<QString> boolFlags = {"--json", "--overwrite", "--no-profile", "--no-verify", "--no-rename", "--rename", "--overread-leadout", "--diagnose", "--fix", "--dvd", "--verify", "--delete-originals", "--delete-scratch", "--yes"};
    Opts o;
    for (int i = 2; i < args.size(); ++i) {
        const QString &a = args[i];
        if (boolFlags.contains(a)) o.flags.insert(a);
        else if (a.startsWith("--") && i + 1 < args.size()) o.kv[a] = args[++i];
        else o.pos << a;
    }
    return o;
}

static bool pickPlatform(const Opts &o, core::Platform &p)
{
    p = core::Platform::PSX;
    return !o.kv.contains("--platform") || core::platformFromId(o.kv["--platform"], p);
}

static void printRename(const core::RenameResult &rr)
{
    if (rr.ok) {
        for (const auto &a : rr.actions) out() << "renamed: " << a << "\n";
        out() << "now at: " << rr.folder << "\n";
    } else {
        out() << "not renamed: " << rr.error << "\n";
    }
}

static bool diagnose(const core::VerifyResult &r, const core::DatIndex &dat, bool fix)
{
    if (r.overall != "FAIL" || r.game.isEmpty()) return false;
    const auto fixes = core::findAudioShifts(r.folder, r, dat);
    if (fixes.isEmpty()) {
        out() << "\nDiagnosis: no simple audio offset shift explains the mismatch.\n";
        return false;
    }
    bool all = true;
    for (const auto &f : fixes) {
        out() << "\nDiagnosis: " << f.file << " equals Redump track '" << f.datRom << "' after shifting "
              << f.samples << " samples (" << f.samples * 4 << " bytes), zero-filling the end.\n"
              << "           Typical cause: write offset differs (drive cannot read the lead-out).\n";
        if (fix) {
            QString e;
            if (core::applyShift(r.folder, f, &e)) out() << "           Fixed (original kept in _original/).\n";
            else { out() << "           Fix failed: " << e << "\n"; all = false; }
        } else {
            out() << "           Re-run with --fix to apply it.\n";
        }
    }
    return fix && all;
}

static int exitFor(const QString &overall)
{
    return overall == "PASS" ? 0 : overall == "FAIL" ? 1 : 2;
}

static int doVerify(const Opts &o)
{
    core::Platform p;
    if (o.pos.isEmpty() || !pickPlatform(o, p)) { usage(); return 3; }
    core::DatIndex dat;
    QString e;
    if (!core::loadDat(p, dat, &e)) { err() << "error: " << e << "\n"; err().flush(); return 3; }
    const bool json = o.flags.contains("--json");
    int lastPct = -1;
    const auto r = core::verifyFolder(QDir::fromNativeSeparators(o.pos[0]), p, dat,
        [&](const QString &stage, qint64 done, qint64 total) {
            if (json) return true;
            const int pct = total ? int(done * 100 / total) : 0;
            if (pct != lastPct && pct % 10 == 0) { err() << stage << " " << pct << "%\n"; err().flush(); }
            lastPct = pct;
            return true;
        });
    if (json) out() << QJsonDocument(r.toJson()).toJson();
    else out() << r.toText();
    auto r2 = r;
    if (o.flags.contains("--diagnose") || o.flags.contains("--fix")) {
        if (diagnose(r, dat, o.flags.contains("--fix"))) {
            r2 = core::verifyFolder(r.folder, p, dat);
            out() << "\nRe-verified after fix:\n" << r2.toText();
        }
    }
    if (o.flags.contains("--rename")) {
        const auto rr = core::renameToRedump(r2, dat);
        printRename(rr);
    }
    out().flush();
    return exitFor(r2.overall);
}

static int doDump(const Opts &o)
{
    core::Platform p;
    if (!pickPlatform(o, p) || !o.kv.contains("--drive") || !o.kv.contains("--name")) {
        usage();
        return 3;
    }
    core::Config cfg = core::loadConfig();
    const QString redumper = core::findRedumper(cfg);
    if (redumper.isEmpty()) { err() << "error: redumper not configured (psxrdc2 config --redumper PATH)\n"; err().flush(); return 3; }

    core::DumpRequest req;
    req.platform = p;
    req.drive = o.kv["--drive"].toUpper();
    if (!req.drive.endsWith(':')) req.drive += ':';
    req.name = o.kv["--name"];
    req.outDir = o.kv.value("--out", cfg.outputDir);
    req.speed = o.kv.value("--speed", cfg.speed);
    req.retries = o.kv.value("--retries", QString::number(cfg.retries)).toInt();
    req.overwrite = o.flags.contains("--overwrite");
    req.overreadLeadout = o.flags.contains("--overread-leadout");
    req.extraArgs = QProcess::splitCommand(o.kv.value("--extra"));
    req.dvd = o.flags.contains("--dvd") || core::probeDisc(req.drive).isDvd;
    if (cfg.useProfile && !o.flags.contains("--no-profile") && cfg.profiles.contains(req.drive))
        req.profile = cfg.profiles[req.drive];

    Runner runner;
    int exitCode = 3;
    QEventLoop loop;
    QObject::connect(&runner, &Runner::logLine, [](const QString &l) { out() << l << "\n"; out().flush(); });
    QObject::connect(&runner, &Runner::phase, [](const QString &n) { out() << "=== " << n << "\n"; out().flush(); });
    int lastPct = -1;
    QObject::connect(&runner, &Runner::progress, [&](int pct, qint64 lba, qint64 total, int s, int c, int q) {
        if (pct != lastPct && pct % 5 == 0) {
            out() << QString("[%1%] LBA %2/%3  SCSI=%4 C2=%5 Q=%6\n").arg(pct).arg(lba).arg(total).arg(s).arg(c).arg(q);
            out().flush();
        }
        lastPct = pct;
    });
    QObject::connect(&runner, &Runner::finished, [&](int code, bool crashed) {
        exitCode = (code == 0 && !crashed) ? 0 : 3;
        loop.quit();
    });
    QString e;
    if (!runner.start(redumper, req, &e)) { err() << "error: " << e << "\n"; err().flush(); return 3; }
    loop.exec();
    if (!req.dvd && (core::lastC2Samples(req) > 0 ||
        (exitCode != 0 && core::pickLog(core::dumpFolder(req)).errors.join(' ').contains("data errors detected")))) {
        exitCode = 0;
        // Unrecoverable C2 errors remained at the dump speed. The drive often reads the bad
        // sectors cleanly when slowed right down: re-read them, rebuild the bin/cue, and let
        // the Redump hash decide whether the result is right.
        out() << "data errors remain: re-reading the bad sectors slowly (4x)\n";
        out().flush();
        auto again = req;
        again.command = "refine";
        again.speed = "4";
        again.retries = 100;
        // Each pass leaves fewer bad samples (29 -> 18 -> 9 -> 2 -> 0 on a real disc); repeat until clean.
        for (int pass = 1; pass <= 10; ++pass) {
            if (!runner.start(redumper, again, &e)) break;
            loop.exec();
            if (exitCode != 0) break;
            const int c2 = core::lastC2Samples(again);
            out() << "refine pass " << pass << ": " << c2 << " C2 samples left\n";
            out().flush();
            if (c2 == 0) break;
        }
        if (exitCode == 0) {
            again.command = "split";
            if (runner.start(redumper, again, &e)) loop.exec();
        }
    }
    // Sectors that fail the ECC check have no C2 flag, so refine skips them: flag them and re-read at 1x.
    for (int attempt = 1; exitCode == 0 && !req.dvd && attempt <= 3; ++attempt) {
        const QVector<int> bad = core::eccBadLbas(redumper, req);
        if (bad.isEmpty() || !core::markForReread(req, bad)) break;
        out() << bad.size() << " sector(s) fail the error check (first: LBA " << bad.first()
              << "); re-reading them at 1x, attempt " << attempt << " of 3\n";
        out().flush();
        auto again = req;
        again.command = "refine";
        again.speed = "1";
        again.retries = 100;
        for (int pass = 1; pass <= 10 && exitCode == 0; ++pass) {
            if (!runner.start(redumper, again, &e)) { exitCode = 3; break; }
            loop.exec();
            if (exitCode == 0 && core::lastC2Samples(again) == 0) break;
        }
        if (exitCode == 0) {
            again.command = "split";
            if (runner.start(redumper, again, &e)) loop.exec(); else exitCode = 3;
        }
    }
    if (exitCode != 0) { err() << "redumper failed\n"; err().flush(); return 3; }

    // Learn the drive profile so the next dump skips autodetection.
    const QString folder = core::dumpFolder(req);
    const auto li = core::pickLog(folder);
    const auto prof = core::profileFromLog(li);
    if (prof.valid()) {
        cfg.profiles[req.drive] = prof;
        core::saveConfig(cfg);
        out() << "saved drive profile for " << req.drive << " (" << prof.inquiry << ")\n";
    }
    if (o.flags.contains("--no-verify")) return 0;

    core::DatIndex dat;
    if (!core::loadDat(p, dat, &e)) { err() << "error: " << e << "\n"; err().flush(); return 3; }
    auto r = core::verifyFolder(folder, p, dat);
    out() << "\n" << r.toText();
    if (r.overall == "FAIL" && diagnose(r, dat, cfg.autoFix)) {
        r = core::verifyFolder(folder, p, dat);
        out() << "\nRe-verified after fix:\n" << r.toText();
    }
    lib::writeCache(folder, r, core::platformId(p));
    if (cfg.autoRename && !o.flags.contains("--no-rename") && r.overall == "PASS")
        printRename(core::renameToRedump(r, dat));
    out().flush();
    return exitFor(r.overall);
}

static int doLibrary(const Opts &o)
{
    const core::Config cfg = core::loadConfig();
    const QString root = o.pos.isEmpty() ? cfg.outputDir : QDir::fromNativeSeparators(o.pos[0]);
    if (o.flags.contains("--verify")) {
        lib::DatCache dats;
        for (const auto &e : lib::scan(root)) {
            if (e.status == "PASS") continue;
            QString pid;
            const auto r = lib::verifyAuto(e.folder, dats, &pid);
            out() << e.name << ": " << r.overall << (r.game.isEmpty() ? "" : " - " + r.game) << "\n";
            out().flush();
        }
    }
    for (const auto &e : lib::scan(root))
        out() << QString("%1  %2  %3  %4  %5%6\n").arg(e.status, -22).arg(e.platformId.toUpper(), -8)
                     .arg(e.serial, -11).arg(e.format, -8).arg(e.name, e.duplicate ? "  [duplicate]" : "");
    out().flush();
    return 0;
}

static int doCompress(const Opts &o)
{
    if (o.pos.isEmpty()) { usage(); return 3; }
    const core::Config cfg = core::loadConfig();
    const QString chdman = chd::findChdman(cfg);
    if (chdman.isEmpty()) { err() << "error: chdman.exe not found (put it next to psxrdc2.exe or set chdmanPath in config.json)\n"; err().flush(); return 3; }
    chd::Options opt;
    opt.deleteOriginals = o.flags.contains("--delete-originals");
    opt.deleteScratch = o.flags.contains("--delete-scratch");
    int last = -1;
    const auto r = chd::compress(QDir::fromNativeSeparators(o.pos[0]), chdman, opt,
        [](const QString &s) { out() << s << "\n"; out().flush(); },
        [&](int pct) { if (pct != last && pct % 10 == 0) { out() << pct << "%\n"; out().flush(); } last = pct; });
    if (!r.ok) { err() << "error: " << r.error << "\n"; err().flush(); return 3; }
    out() << "created " << r.chdPath << "\n";
    for (const auto &d : r.deleted) out() << "deleted " << d << "\n";
    out().flush();
    return 0;
}

static int doDrives()
{
    const auto drives = core::listOpticalDrives();
    if (drives.isEmpty()) out() << "no optical drives found\n";
    const core::Config cfg = core::loadConfig();
    for (const auto &d : drives) {
        out() << d.letter << "  " << (d.label.isEmpty() ? "(no disc)" : d.label);
        if (cfg.profiles.contains(d.letter))
            out() << "  profile: " << cfg.profiles[d.letter].inquiry;
        out() << "\n";
    }
    out().flush();
    return 0;
}

static bool download(const QString &url, const QString &dest, QString *e)
{
    QNetworkAccessManager nam;
    QNetworkRequest rq{QUrl(url)};
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    rq.setHeader(QNetworkRequest::UserAgentHeader, "psxrdc2");
    QNetworkReply *rep = nam.get(rq);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (rep->error() != QNetworkReply::NoError) { *e = rep->errorString(); rep->deleteLater(); return false; }
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { *e = "cannot write " + dest; rep->deleteLater(); return false; }
    f.write(rep->readAll());
    rep->deleteLater();
    return true;
}

static int doSetup(const Opts &o)
{
    const core::Config cfg = core::loadConfig();
    const bool yes = o.flags.contains("--yes");
    QList<setup::Tool> tools;
    if (o.pos.isEmpty() || o.pos[0] == "redumper") tools << setup::Tool::Redumper;
    if (o.pos.isEmpty() || o.pos[0] == "chdman") tools << setup::Tool::Chdman;
    if (!o.pos.isEmpty() && o.pos[0] == "dic") tools << setup::Tool::Dic;
    int rc = 0;
    for (auto t : tools) {
        const QString name = setup::toolName(t);
        if (setup::present(t, cfg)) { out() << name << ": present\n"; continue; }
        setup::Release rel;
        QString e;
        if (!setup::latest(t, &rel, &e)) { err() << name << ": " << e << "\n"; rc = 3; continue; }
        out() << name << ": missing; newest is " << rel.tag << " (" << rel.name << ", "
              << QString::number(rel.size / 1048576.0, 'f', 1) << " MB, sha256 " << rel.sha256 << ")\n";
        if (!yes) { out() << "  run again with --yes to download it into " << core::dataDir() << "\n"; continue; }
        const QString path = setup::install(t, rel, {}, &e);
        if (path.isEmpty()) { err() << "  failed: " << e << "\n"; rc = 3; continue; }
        out() << "  installed " << path << "\n";
    }
    out().flush();
    err().flush();
    return rc;
}

static int doDicDump(const Opts &o)
{
    core::Config cfg = core::loadConfig();
    const QString exe = setup::findDic(cfg);
    if (exe.isEmpty()) { err() << "DiscImageCreator is missing; run: psxrdc2 setup dic --yes\n"; err().flush(); return 3; }
    if (!o.kv.contains("--drive") || !o.kv.contains("--name")) { usage(); return 3; }
    const QString drive = o.kv["--drive"].left(1).toUpper();
    const QString name = o.kv["--name"];
    const QString outDir = QDir::fromNativeSeparators(o.kv.value("--out", cfg.outputDir)) + "/" + name;
    const core::DiscProbe pr = core::probeDisc(drive + ":");
    if (QFileInfo::exists(outDir) && !QDir(outDir).isEmpty()) { err() << "error: " << outDir << " is not empty\n"; err().flush(); return 3; }
    QDir().mkpath(outDir);
    const QString speed = o.kv.value("--speed", pr.isDvd ? "8" : cfg.speed);
    QStringList args;
    if (pr.isDvd) args << "dvd" << drive << QDir::toNativeSeparators(outDir + "/" + name + ".iso") << speed;
    else args << "cd" << drive << QDir::toNativeSeparators(outDir + "/" + name + ".bin") << speed << "/c2" << "/nl";
    out() << "running DiscImageCreator " << args.join(' ') << "\n"; out().flush();
    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
    p.start(exe, args);
    p.write("\n");   // it waits for a key press at the end
    p.closeWriteChannel();
    p.waitForFinished(-1);
    if (p.exitCode() != 0) { err() << "DiscImageCreator exited with code " << p.exitCode() << "\n"; err().flush(); return 3; }
    out() << "dump written to " << outDir << "\nnext: psxrdc2 verify, then psxrdc2 submission\n";
    out().flush();
    return 0;
}

static int doSubmission(const Opts &o)
{
    if (o.pos.isEmpty()) { usage(); return 3; }
    QString txt, zip, e;
    if (!submission::write(o.pos[0], &txt, &zip, &e)) { err() << "error: " << e << "\n"; err().flush(); return 3; }
    out() << "info file: " << txt << "\nlogs zip : " << zip << "\n";
    out().flush();
    return 0;
}

static int doUpdateDat(const Opts &o)
{
    core::Platform p;
    if (!pickPlatform(o, p)) { usage(); return 3; }
    QDir().mkpath(core::datDir(p));
    const QString zip = core::datDir(p) + "/download.zip";
    QString e;
    out() << "downloading " << core::datUrl(p) << "\n"; out().flush();
    if (!download(core::datUrl(p), zip, &e)) { err() << "error: " << e << "\n"; err().flush(); return 3; }
    // Windows 10+ ships bsdtar, which reads zip archives.
    QProcess tar;
    tar.start("tar", {"-xf", zip, "-C", core::datDir(p)});
    tar.waitForFinished(60000);
    QFile::remove(zip);
    if (tar.exitCode() != 0) { err() << "error: extracting the archive failed (is it a zip? redump may require login)\n"; err().flush(); return 3; }
    out() << "installed: " << QFileInfo(core::findDatFile(p)).fileName() << "\n";
    out().flush();
    return 0;
}

static int doConfig(const Opts &o)
{
    core::Config cfg = core::loadConfig();
    QString e;
    if (o.kv.contains("--mpf") && !core::importMpf(o.kv["--mpf"], cfg, &e)) {
        err() << "error: " << e << "\n"; err().flush(); return 3;
    }
    if (o.kv.contains("--redumper")) cfg.redumperPath = QDir::fromNativeSeparators(o.kv["--redumper"]);
    if (o.kv.contains("--out")) cfg.outputDir = QDir::fromNativeSeparators(o.kv["--out"]);
    if (o.kv.contains("--speed")) cfg.speed = o.kv["--speed"];
    if (o.kv.contains("--retries")) cfg.retries = o.kv["--retries"].toInt();
    core::saveConfig(cfg);
    out() << "redumper : " << cfg.redumperPath << "\noutput   : " << cfg.outputDir
          << "\nspeed    : " << (cfg.speed.isEmpty() ? "auto (redumper optimal)" : cfg.speed)
          << "\nretries  : " << cfg.retries << "\nprofiles : " << cfg.profiles.size() << "\n";
    out().flush();
    return 0;
}

int run(const QStringList &args)
{
    const QString cmd = args.value(1);
    if (cmd == "--version") { out() << "psxrdc2 0.2\n"; out().flush(); return 0; }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { usage(); return 0; }
    const Opts o = parse(args);
    if (cmd == "verify") return doVerify(o);
    if (cmd == "dump") return doDump(o);
    if (cmd == "drives") return doDrives();
    if (cmd == "update-dat") return doUpdateDat(o);
    if (cmd == "setup") return doSetup(o);
    if (cmd == "dic-dump") return doDicDump(o);
    if (cmd == "submission") return doSubmission(o);
    if (cmd == "config") return doConfig(o);
    if (cmd == "library") return doLibrary(o);
    if (cmd == "compress") return doCompress(o);
    if (cmd == "install-menu" || cmd == "uninstall-menu") {
        // Per-user registry key, no admin needed.
        const QString key = "HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\PSXRedumpChecker2";
        QSettings reg(key.left(key.indexOf('\\')) + "\\Software\\Classes", QSettings::NativeFormat);
        if (cmd == "uninstall-menu") {
            reg.remove("Directory/shell/PSXRedumpChecker2");
            out() << "context menu removed\n";
        } else {
            const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
            reg.setValue("Directory/shell/PSXRedumpChecker2/Default", "Check PS1/PS2 Redump dump");
            reg.setValue("Directory/shell/PSXRedumpChecker2/Icon", exe);
            reg.setValue("Directory/shell/PSXRedumpChecker2/command/Default",
                         "\"" + exe + "\" --open \"%1\"");
            out() << "context menu installed for folders (right-click a folder > Check PS1/PS2 Redump dump)\n";
        }
        reg.sync();
        out().flush();
        return reg.status() == QSettings::NoError ? 0 : 3;
    }
    usage();
    return 3;
}

} // namespace cli
