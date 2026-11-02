#include "LibraryTab.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "Chd.h"

using namespace core;

static QString human(qint64 b)
{
    if (b <= 0) return "";
    const double gb = b / 1073741824.0;
    return gb >= 1 ? QString::number(gb, 'f', 2) + " GB" : QString::number(b / 1048576.0, 'f', 0) + " MB";
}

LibraryTab::LibraryTab(Config *cfg, QWidget *parent) : QWidget(parent), m_cfg(cfg)
{
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_root = new QLineEdit(cfg->outputDir);
    auto *browse = new QPushButton("Browse...");
    m_scan = new QPushButton("Rescan");
    top->addWidget(new QLabel("Library folder"));
    top->addWidget(m_root, 1);
    top->addWidget(browse);
    top->addWidget(m_scan);
    v->addLayout(top);

    m_table = new QTableWidget(0, 9);
    m_table->setHorizontalHeaderLabels({"Folder", "System", "Redump title", "Serial", "Status", "Format", "Size",
                                        "Checked", "Note"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->setColumnWidth(0, 230);
    m_table->setColumnWidth(1, 70);
    m_table->setColumnWidth(2, 230);
    m_table->setColumnWidth(3, 90);
    m_table->setColumnWidth(4, 90);
    v->addWidget(m_table, 1);

    auto *row1 = new QHBoxLayout;
    m_verify = new QPushButton("Verify selected");
    m_verifyNew = new QPushButton("Verify everything not checked");
    m_open = new QPushButton("Open folder");
    m_delete = new QPushButton("Delete selected...");
    row1->addWidget(m_verify);
    row1->addWidget(m_verifyNew);
    row1->addWidget(m_open);
    row1->addStretch(1);
    row1->addWidget(m_delete);
    v->addLayout(row1);

    auto *box = new QWidget;
    auto *bl = new QHBoxLayout(box);
    bl->setContentsMargins(0, 0, 0, 0);
    m_compress = new QPushButton("Compress selected to CHD");
    m_delOriginals = new QCheckBox("Then delete the bin/cue or iso (only after the CHD extracts back to the verified data)");
    m_delScratch = new QCheckBox("and redumper's scratch files");
    bl->addWidget(m_compress);
    bl->addWidget(m_delOriginals);
    bl->addWidget(m_delScratch);
    bl->addStretch(1);
    v->addWidget(box);

    m_bar = new QProgressBar;
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setMaximumHeight(130);
    v->addWidget(m_bar);
    v->addWidget(m_log);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getExistingDirectory(this, "Library folder", m_root->text());
        if (!p.isEmpty()) { m_root->setText(p); refresh(); }
    });
    connect(m_scan, &QPushButton::clicked, this, &LibraryTab::refresh);
    connect(m_verify, &QPushButton::clicked, this, [this] { verifyRows(selectedRows()); });
    connect(m_verifyNew, &QPushButton::clicked, this, [this] {
        QVector<int> rows;
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].status != "PASS" && m_entries[i].status != "FAIL") rows << i;
        verifyRows(rows);
    });
    connect(m_compress, &QPushButton::clicked, this, [this] { compressRows(selectedRows()); });
    connect(m_delete, &QPushButton::clicked, this, [this] { deleteRows(selectedRows()); });
    connect(m_open, &QPushButton::clicked, this, [this] {
        for (int r : selectedRows()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_entries[r].folder));
    });
    refresh();
}

void LibraryTab::logLine(const QString &s) { m_log->appendPlainText(s); }

void LibraryTab::setBusy(bool busy)
{
    m_busy = busy;
    for (auto *b : {m_scan, m_verify, m_verifyNew, m_compress, m_open, m_delete}) b->setEnabled(!busy);
    if (!busy) m_bar->setValue(0);
}

void LibraryTab::refresh()
{
    if (m_busy) return;
    m_cfg->outputDir = m_root->text();
    m_entries = lib::scan(m_root->text());
    fillTable();
}

void LibraryTab::fillTable()
{
    m_table->setSortingEnabled(false);
    m_table->setRowCount(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries[i];
        Platform p;
        const QString sys = (!e.platformId.isEmpty() && platformFromId(e.platformId, p)) ? e.platformId.toUpper() : "";
        QStringList notes;
        if (e.duplicate) notes << "duplicate";
        if (e.hasChd && e.hasOriginal) notes << "CHD + original";
        else if (e.hasChd) notes << "CHD";
        const QStringList cells = {e.name, sys, e.game, e.serial, e.status, e.format, human(e.bytes),
                                   e.checkedAt.left(16).replace('T', ' '), notes.join(", ")};
        for (int c = 0; c < cells.size(); ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            if (c == 0) it->setData(Qt::UserRole, i);
            if (c == 4) {
                const QColor col = e.status == "PASS" ? QColor("#1b7a1b")
                                 : e.status == "FAIL" ? QColor("#b71c1c")
                                 : e.status == "UNKNOWN" ? QColor("#b26a00") : QColor("#666666");
                it->setForeground(col);
            }
            if (c == 8 && e.duplicate) it->setForeground(QColor("#b26a00"));
            m_table->setItem(i, c, it);
        }
    }
    m_table->setSortingEnabled(true);
}

QVector<int> LibraryTab::selectedRows() const
{
    QVector<int> rows;
    for (const auto &idx : m_table->selectionModel()->selectedRows()) {
        const auto *it = m_table->item(idx.row(), 0);
        if (it) rows << it->data(Qt::UserRole).toInt();
    }
    return rows;
}

void LibraryTab::verifyRows(const QVector<int> &rows)
{
    if (rows.isEmpty() || m_busy) return;
    setBusy(true);
    QStringList folders;
    for (int r : rows) folders << m_entries[r].folder;
    logLine(QString("Verifying %1 folder(s)...").arg(folders.size()));
    QThread *t = QThread::create([this, folders] {
        for (int i = 0; i < folders.size(); ++i) {
            QString pid;
            const VerifyResult r = lib::verifyAuto(folders[i], m_dats, &pid,
                [this](const QString &, qint64 done, qint64 total) {
                    const int pct = total ? int(done * 100 / total) : 0;
                    QMetaObject::invokeMethod(this, [this, pct] { m_bar->setValue(pct); });
                    return true;
                });
            const QString line = QString("%1: %2%3").arg(QFileInfo(folders[i]).fileName(), r.overall,
                                                          r.game.isEmpty() ? QString() : " - " + r.game);
            QMetaObject::invokeMethod(this, [this, line] { logLine(line); });
        }
        QMetaObject::invokeMethod(this, [this] {
            setBusy(false);
            refresh();
            logLine("Done.");
        });
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void LibraryTab::compressRows(const QVector<int> &rows)
{
    if (rows.isEmpty() || m_busy) return;
    QString chdman = chd::findChdman(*m_cfg);
    if (chdman.isEmpty() && onNeedChdman && onNeedChdman()) chdman = chd::findChdman(*m_cfg);
    if (chdman.isEmpty()) return;
    chd::Options opt;
    opt.deleteOriginals = m_delOriginals->isChecked();
    opt.deleteScratch = m_delScratch->isChecked();
    if (opt.deleteOriginals || opt.deleteScratch) {
        const auto ans = QMessageBox::warning(this, "Delete after compressing",
            "After each CHD is built, checked and proven to extract back to the verified data, the "
            "original files will be deleted. This cannot be undone.\n\nContinue?",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (ans != QMessageBox::Yes) return;
    }
    QStringList folders;
    for (int r : rows) folders << m_entries[r].folder;
    setBusy(true);
    QThread *t = QThread::create([this, folders, chdman, opt] {
        for (const QString &f : folders) {
            const QString name = QFileInfo(f).fileName();
            QMetaObject::invokeMethod(this, [this, name] { logLine("=== " + name); });
            const chd::Result r = chd::compress(f, chdman, opt,
                [this](const QString &s) { QMetaObject::invokeMethod(this, [this, s] { logLine(s); }); },
                [this](int pct) { QMetaObject::invokeMethod(this, [this, pct] { m_bar->setValue(pct); }); });
            const QString line = r.ok
                ? QString("OK: %1 -> %2 (saved %3)").arg(human(r.originalBytes), human(r.chdBytes),
                                                         human(qMax<qint64>(0, r.originalBytes - r.chdBytes)))
                : "FAILED: " + r.error;
            QMetaObject::invokeMethod(this, [this, line, deleted = r.deleted] {
                logLine(line);
                if (!deleted.isEmpty()) logLine("deleted: " + deleted.join(", "));
            });
        }
        QMetaObject::invokeMethod(this, [this] { setBusy(false); refresh(); logLine("Done."); });
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void LibraryTab::deleteRows(const QVector<int> &rows)
{
    if (rows.isEmpty() || m_busy) return;
    QStringList names;
    qint64 bytes = 0;
    for (int r : rows) { names << m_entries[r].name; bytes += m_entries[r].bytes; }
    const auto ans = QMessageBox::warning(this, "Delete folders",
        QString("Permanently delete these %1 folder(s) with everything inside?\n\n%2\n\n(about %3 of images; this cannot be undone)")
            .arg(rows.size()).arg(names.join("\n")).arg(human(bytes)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (ans != QMessageBox::Yes) return;
    for (int r : rows) {
        const bool ok = QDir(m_entries[r].folder).removeRecursively();
        logLine((ok ? "deleted " : "could not delete ") + m_entries[r].name);
    }
    refresh();
}
