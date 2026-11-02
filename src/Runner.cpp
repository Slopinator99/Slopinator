#include "Runner.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

Runner::Runner(QObject *parent) : QObject(parent) {}

bool Runner::running() const { return m_proc && m_proc->state() != QProcess::NotRunning; }

bool Runner::start(const QString &redumper, const core::DumpRequest &req, QString *err)
{
    if (running()) { if (err) *err = "already running"; return false; }
    const QString folder = core::dumpFolder(req);
    if (!QDir().mkpath(folder)) { if (err) *err = "cannot create " + folder; return false; }

    if (m_log) m_log->deleteLater();
    m_log = new QFile(QDir(folder).filePath(req.name + "_redumper_stdout.log"), this);
    // refine / split continue the same log instead of wiping the original dump's output
    const auto mode = req.command == "disc" ? QIODevice::WriteOnly | QIODevice::Truncate
                                            : QIODevice::WriteOnly | QIODevice::Append;
    if (!m_log->open(mode)) {
        if (err) *err = "cannot write log in " + folder;
        return false;
    }
    if (m_proc) m_proc->deleteLater();
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    m_proc->setWorkingDirectory(folder);
#ifdef Q_OS_WIN
    m_proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= 0x08000000;     // CREATE_NO_WINDOW
    });
#endif
    m_buf.clear();
    m_phases.clear();
    connect(m_proc, &QProcess::readyRead, this, &Runner::onData);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) emit finished(-1, true);
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        onData();
        if (!m_buf.isEmpty()) { feed(m_buf + "\n"); m_buf.clear(); }
        m_log->close();
        emit finished(code, st == QProcess::CrashExit);
    });
    const QStringList args = core::buildDumpArgs(req);
    emit logLine("launching: " + redumper + " " + args.join(' '));
    m_proc->start(redumper, args);
    return true;
}

void Runner::cancel()
{
    if (!running()) return;
    m_proc->terminate();
    if (!m_proc->waitForFinished(3000)) m_proc->kill();
}

void Runner::onData()
{
    const QByteArray d = m_proc->readAll();
    if (d.isEmpty()) return;
    if (m_log && m_log->isOpen()) m_log->write(d);
    feed(d);
}

void Runner::feed(const QByteArray &chunk)
{
    // CD: "errors: { SCSIs: 0, C2s: 0, Q: 0 }"; DVD/BD carry fewer counters, so read them by name.
    static const QRegularExpression spin(R"(\[\s*(\d+)%\] LBA:\s*(\d+)/(\d+), errors: \{([^}]*)\})");
    static const QRegularExpression reScsi(R"(SCSIs?:\s*(\d+))"), reC2(R"(C2s?:\s*(\d+))"), reQ(R"(Q:\s*(\d+))");
    static const QRegularExpression ph(R"(\*\*\* ([A-Za-z:]+))");
    m_buf += chunk;
    const QList<QByteArray> parts = m_buf.split('\n');
    m_buf = parts.last();       // incomplete tail
    QList<QByteArray> lines;
    for (int i = 0; i < parts.size() - 1; ++i)
        for (const QByteArray &seg : parts[i].split('\r'))
            lines << seg;
    // A progress spinner uses bare \r with no \n, so the tail may be a spinner too.
    const QList<QByteArray> tailSegs = m_buf.split('\r');
    if (tailSegs.size() > 1) {
        for (int i = 0; i < tailSegs.size() - 1; ++i) lines << tailSegs[i];
        m_buf = tailSegs.last();
    }
    for (const QByteArray &raw : lines) {
        const QString s = QString::fromUtf8(raw).trimmed();
        if (s.isEmpty()) continue;
        if (const auto m = spin.match(s); m.hasMatch()) {
            const QString errs = m.captured(4);
            auto num = [&](const QRegularExpression &re) {
                const auto em = re.match(errs);
                return em.hasMatch() ? em.captured(1).toInt() : 0;
            };
            emit progress(m.captured(1).toInt(), m.captured(2).toLongLong(), m.captured(3).toLongLong(),
                          num(reScsi), num(reC2), num(reQ));
            const QString tail = s.mid(m.capturedEnd()).trimmed();
            if (!tail.isEmpty()) emit logLine(tail);
            continue;
        }
        if (const auto m = ph.match(s); m.hasMatch()) {
            const QString name = m.captured(1);
            if (!m_phases.contains(name)) { m_phases << name; emit phase(name); }
            continue;
        }
        emit logLine(s);
    }
}
