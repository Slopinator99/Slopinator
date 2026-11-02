#pragma once
#include <QStringList>

namespace cli {
// True if the arguments request CLI mode (any known subcommand or --help).
bool wantsCli(const QStringList &args);
// Runs the command; returns the process exit code (0 pass, 1 fail, 2 unknown, 3 error).
int run(const QStringList &args);
}
