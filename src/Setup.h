#pragma once
#include <QString>
#include <QStringList>
#include <functional>

#include "Core.h"

// First-run helper: fetches the external tools this program does not contain into its own folder.
namespace setup {

enum class Tool { Redumper, Chdman, Dic };

struct Release {
    QString tag;
    QString url;        // asset to download
    QString name;
    qint64 size = 0;
    QString sha256;     // expected hash, if the release publishes one
    bool selfRecorded = false;  // hash recorded by this program's author, not published by the tool's author
};

QString toolName(Tool t);
bool present(Tool t, const core::Config &cfg);

// Looks up the newest release on GitHub (redumper: superg/redumper, chdman: mamedev/mame).
bool latest(Tool t, Release *out, QString *err);

// Downloads, checks the SHA-256 when known, extracts only the tool's exe into the program folder.
// progress(done, total) is called while downloading. Returns the installed path, or empty and *err.
QString install(Tool t, const Release &rel, const std::function<void(qint64, qint64)> &progress, QString *err);

} // namespace setup

namespace setup {
// DiscImageCreator (saramibreak): second dump engine, installed into <data>/dic.
QString findDic(const core::Config &cfg);
}
