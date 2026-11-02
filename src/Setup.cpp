#include "Setup.h"

#include "Chd.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace setup {

QString toolName(Tool t) { return t == Tool::Redumper ? "redumper" : "chdman"; }

bool present(Tool t, const core::Config &cfg)
{
    return t == Tool::Redumper ? !core::findRedumper(cfg).isEmpty() : !chd::findChdman(cfg).isEmpty();
}

// Blocking GET. Writes into `sink` when given (file download), else returns the body.
static bool fetch(const QString &url, QByteArray *body, QFile *sink, QString *err,
                  const std::function<void(qint64, qint64)> &progress = {})
{
    QNetworkAccessManager nam;
    QNetworkRequest rq{QUrl(url)};
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    rq.setHeader(QNetworkRequest::UserAgentHeader, "psxrdc2");
    QNetworkReply *rep = nam.get(rq);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (sink)
        QObject::connect(rep, &QNetworkReply::readyRead, [&] { sink->write(rep->readAll()); });
    if (progress)
        QObject::connect(rep, &QNetworkReply::downloadProgress, [&](qint64 d, qint64 t) { progress(d, t); });
    loop.exec();
    const bool ok = rep->error() == QNetworkReply::NoError;
    if (!ok && err) *err = rep->errorString();
    if (ok && sink) sink->write(rep->readAll());
    else if (ok && body) *body = rep->readAll();
    rep->deleteLater();
    return ok;
}

bool latest(Tool t, Release *out, QString *err)
{
    const QString repo = t == Tool::Redumper ? "superg/redumper" : "mamedev/mame";
    QByteArray body;
    if (!fetch("https://api.github.com/repos/" + repo + "/releases/latest", &body, nullptr, err)) return false;
    const QJsonObject rel = QJsonDocument::fromJson(body).object();
    const QRegularExpression want(t == Tool::Redumper ? R"(^redumper-.*-windows-x64\.zip$)" : R"(^mame\d+b_x64\.exe$)");
    QString sumsUrl;
    Release r;
    r.tag = rel["tag_name"].toString();
    for (const auto &v : rel["assets"].toArray()) {
        const QJsonObject a = v.toObject();
        const QString name = a["name"].toString();
        if (want.match(name).hasMatch()) {
            r.name = name;
            r.url = a["browser_download_url"].toString();
            r.size = qint64(a["size"].toDouble());
            const QString digest = a["digest"].toString();
            if (digest.startsWith("sha256:")) r.sha256 = digest.mid(7).toLower();
        } else if (name.compare("SHA256SUMS", Qt::CaseInsensitive) == 0) {
            sumsUrl = a["browser_download_url"].toString();
        }
    }
    if (r.url.isEmpty()) { if (err) *err = "no suitable download found in the latest release"; return false; }
    if (r.sha256.isEmpty() && !sumsUrl.isEmpty()) {
        QByteArray sums;
        if (fetch(sumsUrl, &sums, nullptr, err)) {
            for (const QByteArray &line : sums.split('\n')) {
                const QString s = QString::fromUtf8(line).trimmed();
                if (s.endsWith(r.name)) { r.sha256 = s.section(QRegularExpression(R"(\s+)"), 0, 0).toLower(); break; }
            }
        }
    }
    if (r.sha256.isEmpty()) { if (err) *err = "the release publishes no checksum, so the download cannot be verified"; return false; }
    *out = r;
    return true;
}

static bool run(const QString &exe, const QStringList &args, QString *err)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
    p.start(exe, args);
    if (!p.waitForFinished(600000) || p.exitCode() != 0) {
        if (err) *err = QFileInfo(exe).fileName() + " failed: " + QString::fromUtf8(p.readAll()).trimmed().left(300);
        return false;
    }
    return true;
}

static QString find7z()
{
    for (const QString &c : {QString("C:/Program Files/7-Zip/7z.exe"), QString("C:/Program Files (x86)/7-Zip/7z.exe")})
        if (QFileInfo::exists(c)) return c;
    return {};
}

QString install(Tool t, const Release &rel, const std::function<void(qint64, qint64)> &progress, QString *err)
{
    QTemporaryDir tmp(core::dataDir() + "/_download_XXXXXX");
    if (!tmp.isValid()) { if (err) *err = "cannot create a temporary folder next to the program"; return {}; }
    const QString archive = tmp.filePath(rel.name);
    {
        QFile f(archive);
        if (!f.open(QIODevice::WriteOnly)) { if (err) *err = "cannot write " + archive; return {}; }
        if (!fetch(rel.url, nullptr, &f, err, progress)) return {};
    }
    QFile f(archive);
    f.open(QIODevice::ReadOnly);
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd()) h.addData(f.read(8 << 20));
    f.close();
    if (QString(h.result().toHex()).toLower() != rel.sha256) {
        if (err) *err = "the download does not match its published SHA-256; it was discarded";
        return {};
    }

    const QString outDir = tmp.filePath("x");
    QDir().mkpath(outDir);
    const QString exeName = toolName(t) + ".exe";
    bool ok = false;
    QString why;
    if (t == Tool::Chdman) {
        // The MAME download is a self-extracting 7z: take only chdman.exe out of it.
        const QString z = find7z();
        if (!z.isEmpty()) ok = run(z, {"e", "-y", "-o" + outDir, archive, exeName}, &why);
        if (!ok) ok = run("tar", {"-xf", archive, "-C", outDir, exeName}, &why);
    } else {
        ok = run("tar", {"-xf", archive, "-C", outDir}, &why);
    }
    if (!ok) { if (err) *err = why; return {}; }

    QString found;
    for (QDirIterator it(outDir, {exeName}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) { found = it.next(); break; }
    if (found.isEmpty()) { if (err) *err = exeName + " was not found in the download"; return {}; }
    const QString dest = core::dataDir() + "/" + exeName;
    QFile::remove(dest);
    if (!QFile::copy(found, dest)) { if (err) *err = "cannot write " + dest; return {}; }
    return dest;
}

} // namespace setup
