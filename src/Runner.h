#pragma once
#include <QObject>
#include <QFile>
#include <QProcess>

#include "Core.h"

// Runs redumper as a child process, parses its progress output, and keeps a
// full copy of the output in <folder>/<name>_redumper_stdout.log.
class Runner : public QObject {
    Q_OBJECT
public:
    explicit Runner(QObject *parent = nullptr);
    bool running() const;
    // Returns false (with err) if it cannot start.
    // `exe` is the engine selected by req.engine.
    bool start(const QString &exe, const core::DumpRequest &req, QString *err);
    void cancel();

signals:
    void phase(const QString &name);
    void progress(int percent, qint64 lba, qint64 total, int scsi, int c2, int q);
    void logLine(const QString &line);
    void finished(int exitCode, bool crashed);

private:
    void onData();
    void feed(const QByteArray &chunk);
    QProcess *m_proc = nullptr;
    QByteArray m_buf;
    QFile *m_log = nullptr;
    QStringList m_phases;
    bool m_dic = false;
};
