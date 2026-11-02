#include <QApplication>
#include <QCoreApplication>
#include <QMap>

#include "Cli.h"
#include "MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
// The exe is a GUI-subsystem binary; in CLI mode, hook up to the launching console.
static void attachConsole()
{
    // Already redirected to a pipe/file by the parent: leave the handles alone.
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    // Qt plugins (platforms/qwindows.dll) resolve their own dependencies from the plugin's
    // folder and PATH only. Add the exe folder so the bundled DLLs are always found,
    // whatever environment the exe was started from.
    {
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            for (DWORD i = n; i > 0; --i)
                if (path[i - 1] == L'\\') { path[i - 1] = 0; break; }
            SetDllDirectoryW(path);
        }
    }
#endif
    QStringList args;
    for (int i = 0; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);

    if (cli::wantsCli(args)) {
#ifdef Q_OS_WIN
        attachConsole();
#endif
        QCoreApplication app(argc, argv);
        return cli::run(app.arguments());
    }

    QApplication app(argc, argv);
    app.setApplicationName("psxrdc2");
    MainWindow w;
    // `psxrdc2 --open <folder>` (Explorer context menu) jumps to Verify.
    const QStringList a = app.arguments();
    const int oi = a.indexOf("--open");
    if (oi >= 0 && oi + 1 < a.size()) w.openFolder(a[oi + 1]);
    // `psxrdc2 --dump --drive H: --name X [--speed N] [--overread] ...` starts a dump
    // in the GUI right away (progress is also written to status.json next to the exe).
    if (a.contains("--dump")) {
        QMap<QString, QString> o;
        for (int i = 1; i < a.size(); ++i) {
            if (!a[i].startsWith("--") || a[i] == "--dump") continue;
            const QString k = a[i].mid(2);
            if (k == "overread" || k == "norename" || k == "overwrite") o[k] = "1";
            else if (i + 1 < a.size()) o[k] = a[++i];
        }
        w.show();
        w.autoDump(o);
        return app.exec();
    }
    w.show();
    return app.exec();
}
