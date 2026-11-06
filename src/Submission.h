#pragma once
#include <QString>

// Helper for a Redump submission: summarises a finished dump and zips the logs Redump asks for.
namespace submission {

// Writes <folder>/<name>_submission.txt and <folder>/<name>_logs.zip.
// Works on redumper dumps and on DiscImageCreator dumps (the logs are zipped unmodified).
bool write(const QString &folder, QString *txtPath, QString *zipPath, QString *err);

} // namespace submission
