#include "Submission.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

#include "Core.h"

namespace submission {

bool write(const QString &folder, QString *txtPath, QString *zipPath, QString *err)
{
    const QDir d(folder);
    if (!d.exists()) { if (err) *err = "no such folder: " + folder; return false; }

    QStringList images;
    for (const QString &n : d.entryList({"*.iso", "*.bin"}, QDir::Files, QDir::Name))
        if (!n.endsWith("_DMI.bin") && !n.endsWith("_PFI.bin") && !n.endsWith("_PIC.bin")) images << n;
    if (images.isEmpty()) { if (err) *err = "no .iso or .bin image in " + folder; return false; }

    // DiscImageCreator writes <name>_disc.txt / _mainInfo.txt; redumper writes <name>.log.
    const bool dic = !d.entryList({"*_disc.txt"}, QDir::Files).isEmpty();
    const QStringList logPatterns = dic
        ? QStringList{"*_disc.txt", "*_drive.txt", "*_mainInfo.txt", "*_mainError.txt", "*_volDesc.txt", "*_c2Error.txt",
                      "*_subError.txt", "*_subReadable.txt", "*_subIntention.txt", "*_PFI.bin", "*_DMI.bin", "*_PIC.bin",
                      "*.dat", "*.cue", "*.ccd", "*.sub", "*.c2", "*_20*.txt"}
        : QStringList{"*.log", "*.cue", "*.physical", "*.manufacturer", "*.toc", "*.fulltoc", "*.pma", "*.atip"};
    QStringList logs;
    for (const QString &n : d.entryList(logPatterns, QDir::Files, QDir::Name))
        if (!n.endsWith("_submission.txt") && !n.endsWith("_logs.zip")) logs << n;

    const QString base = QFileInfo(images.first()).completeBaseName();
    QString text;
    QTextStream t(&text);
    t << "Submission summary for Redump (generated; fill in the rest on redump.info)\n\n";
    t << "Dump tool       : " << (dic ? "DiscImageCreator" : "redumper") << "\n";
    t << "Dump folder     : " << d.dirName() << "\n";
    t << "Summary date    : " << QDate::currentDate().toString(Qt::ISODate) << "\n\n";

    QString serial;
    for (const QString &n : images) {
        core::Hashes h;
        if (!core::hashFile(d.filePath(n), h)) { if (err) *err = "cannot hash " + n; return false; }
        t << "File            : " << n << "\n"
          << "  Size          : " << h.size << "\n"
          << "  CRC32         : " << h.crc << "\n"
          << "  MD5           : " << h.md5 << "\n"
          << "  SHA-1         : " << h.sha1 << "\n";
        if (serial.isEmpty()) serial = core::imageSerial(d.filePath(n));
    }
    t << "\nSerial          : " << (serial.isEmpty() ? "(not found)" : serial) << "\n";

    if (dic) {
        const QFileInfo ef(d.filePath(base + "_mainError.txt"));
        t << "Read errors     : " << (ef.exists() ? (ef.size() == 0 ? "none (error log is empty)" : "SEE _mainError.txt") : "(no error log)") << "\n";
    } else {
        const core::LogInfo li = core::pickLog(folder);
        if (li.found) {
            t << "Drive           : " << li.drive << "\n"
              << "Read offset     : " << li.readOffset << "\n"
              << "Errors          : " << (li.errors.isEmpty() ? "none" : li.errors.join("; ")) << "\n";
        }
    }

    t << "\nLogs included in the zip:\n";
    for (const QString &n : logs) t << "  " << n << "\n";
    t << "\nStill needed from you: redump.info account, disc title/region/languages, ring codes,\n"
         "barcode, label/cover photos, comments. Logs must stay unmodified. Dump the disc twice and\n"
         "compare the hashes before submitting (Redump requires two matching dumps).\n";

    const QString txt = d.filePath(base + "_submission.txt");
    QFile f(txt);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = "cannot write " + txt; return false; }
    f.write(text.toUtf8());
    f.close();

    // Windows 10+ ships bsdtar, which can write zip archives.
    const QString zip = d.filePath(base + "_logs.zip");
    QFile::remove(zip);
    QStringList args = {"-a", "-cf", zip, "-C", folder};
    args << logs;
    QProcess p;
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
    p.start("tar", args);
    if (!p.waitForFinished(60000) || p.exitCode() != 0) {
        if (err) *err = "could not create the zip: " + QString::fromUtf8(p.readAll()).left(200);
        return false;
    }
    if (txtPath) *txtPath = txt;
    if (zipPath) *zipPath = zip;
    return true;
}

} // namespace submission
