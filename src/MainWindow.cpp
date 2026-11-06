#include "MainWindow.h"

#include "Library.h"
#include "LibraryTab.h"
#include "Setup.h"
#include "Submission.h"
#include <QDesktopServices>
#include <QUrl>

#include <QApplication>
#include <QProgressDialog>
#include <QTimer>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

using namespace core;

static QPushButton *browseButton(QLineEdit *target, bool file, QWidget *parent)
{
    auto *b = new QPushButton("Browse...", parent);
    QObject::connect(b, &QPushButton::clicked, parent, [=] {
        const QString p = file ? QFileDialog::getOpenFileName(parent, "Select file", target->text())
                               : QFileDialog::getExistingDirectory(parent, "Select folder", target->text());
        if (!p.isEmpty()) target->setText(p);
    });
    return b;
}

// The bar's own centered text is unreadable over the green blocks, so the
// percentage is drawn as a large label beside the bar instead.
static QWidget *barWithPercent(QProgressBar *bar)
{
    bar->setTextVisible(false);
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *pct = new QLabel("0%");
    pct->setMinimumWidth(64);
    pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pct->setStyleSheet("font-size: 18px; font-weight: bold;");
    QObject::connect(bar, &QProgressBar::valueChanged, pct, [pct](int v) { pct->setText(QString::number(v) + "%"); });
    l->addWidget(bar, 1);
    l->addWidget(pct);
    return w;
}

static QWidget *row(QLineEdit *e, QPushButton *b)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(e, 1);
    l->addWidget(b);
    return w;
}

MainWindow::MainWindow()
{
    setWindowTitle("PSX / PS2 Redump Checker");
    resize(860, 640);
    setAcceptDrops(true);
    m_cfg = loadConfig();
    // Nothing is tied to this machine: missing tools are offered for download after the window is up.
    QTimer::singleShot(0, this, [this] { offerMissingTools(); });

    m_tabs = new QTabWidget;
    m_tabs->addTab(buildDumpTab(), "Dump");
    m_tabs->addTab(buildVerifyTab(), "Verify");
    auto *library = new LibraryTab(&m_cfg);
    library->onNeedChdman = [this] { return downloadTool(setup::Tool::Chdman); };
    m_tabs->addTab(library, "Library");
    m_tabs->addTab(buildSettingsTab(), "Settings");

    // Classic segmented progress bar: a row of small vertical blocks.
    setStyleSheet(
        // Sunken trough with a raised, lit-from-above green block (gradient + bevel borders).
        "QProgressBar { border: 2px solid; border-color: #6b6b6b #ffffff #ffffff #6b6b6b;"
        "  background: #e4e4e4; height: 20px; text-align: center; color: #000000; padding: 1px; }"
        // Square green blocks like the classic bar, raised: lit top-left edge, dark bottom-right edge.
        "QProgressBar::chunk { width: 8px; margin: 0px 1px; border-radius: 0px;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #7fdc7f, stop:0.5 #38b038, stop:1 #218a21);"
        "  border: 1px solid; border-color: #c4f5c4 #12521a #12521a #c4f5c4; }");
    setCentralWidget(m_tabs);

    connect(&m_runner, &Runner::logLine, this, [this](const QString &l) {
        m_dLog->appendPlainText(l);
        m_status["lastLog"] = l;
    });
    connect(&m_runner, &Runner::phase, this, [this](const QString &n) {
        m_dStatus->setText("Phase: " + n);
        m_dLog->appendPlainText("=== " + n);
    });
    connect(&m_runner, &Runner::progress, this, [this](int pct, qint64 lba, qint64 total, int s, int c, int q) {
        m_dBar->setValue(pct);
        m_dStatus->setText(QString("LBA %1/%2   SCSI %3   C2 %4   Q %5").arg(lba).arg(total).arg(s).arg(c).arg(q));
    });
    connect(&m_runner, &Runner::finished, this, &MainWindow::onDumpFinished);
    connect(&m_runner, &Runner::phase, this, [this](const QString &n) {
        writeStatus("dumping", {{"phase", n}});
    });
    connect(&m_runner, &Runner::progress, this, [this](int pct, qint64 lba, qint64 total, int s, int c, int q) {
        if (pct == m_lastStatusPct) return;
        m_lastStatusPct = pct;
        writeStatus("dumping", {{"percent", pct}, {"lba", double(lba)}, {"totalLba", double(total)},
                                {"scsi", s}, {"c2", c}, {"q", q}});
    });
    refreshDrives();
}

QWidget *MainWindow::buildDumpTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *form = new QFormLayout;

    m_dPlatform = new QComboBox;
    for (const auto &pi : allPlatforms()) m_dPlatform->addItem(pi.name, int(pi.platform));
    m_dDrive = new QComboBox;
    auto *refresh = new QPushButton("Refresh");
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshDrives);
    auto *driveRow = new QWidget;
    auto *dl = new QHBoxLayout(driveRow);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->addWidget(m_dDrive, 1);
    dl->addWidget(refresh);

    m_dName = new QLineEdit;
    m_dName->setPlaceholderText("e.g. SLUS_00551 (also the output sub-folder name)");
    m_dOut = new QLineEdit(m_cfg.outputDir);
    m_dSpeed = new QLineEdit(m_cfg.speed);
    m_dSpeed->setPlaceholderText("blank = redumper picks the drive's optimal speed");
    m_dRetries = new QSpinBox;
    m_dRetries->setRange(0, 100);
    m_dRetries->setValue(m_cfg.retries);
    m_dOverwrite = new QCheckBox("Overwrite existing dump files");
    m_dOverread = new QCheckBox("Overread lead-out (generic drives: capture the end of the last track)");
    m_dProfile = new QCheckBox("Use saved drive profile (skips autodetection)");
    m_dProfile->setChecked(m_cfg.useProfile);
    m_dRename = new QCheckBox("Rename to Redump names when the dump verifies (PASS)");
    m_dRename->setChecked(m_cfg.autoRename);
    m_dAutoFix = new QCheckBox("Auto-apply an audio offset fix when it makes the dump match Redump (original kept)");
    m_dAutoFix->setChecked(m_cfg.autoFix);
    m_dProfileLbl = new QLabel;
    m_dProfileLbl->setStyleSheet("color: gray");

    m_dNote = new QLabel;
    m_dNote->setWordWrap(true);
    m_dNote->setStyleSheet("color: #b26a00;");
    m_dExtra = new QLineEdit;
    m_dExtra->setPlaceholderText("advanced: extra redumper arguments, e.g. --dvd-raw");
    m_dEngine = new QComboBox;
    for (const auto &e : engines()) m_dEngine->addItem(e.name, e.id);
    form->addRow("Engine", m_dEngine);
    form->addRow("Platform", m_dPlatform);
    form->addRow("", m_dNote);
    form->addRow("Drive", driveRow);
    form->addRow("Name", m_dName);
    form->addRow("Output", row(m_dOut, browseButton(m_dOut, false, w)));
    form->addRow("Speed", m_dSpeed);
    form->addRow("Retries", m_dRetries);
    form->addRow("", m_dProfile);
    form->addRow("", m_dProfileLbl);
    form->addRow("", m_dAutoFix);
    form->addRow("", m_dRename);
    form->addRow("", m_dOverread);
    form->addRow("", m_dOverwrite);
    form->addRow("Extra args", m_dExtra);
    v->addLayout(form);

    auto *btns = new QHBoxLayout;
    m_dStart = new QPushButton("Dump && verify");
    m_dCancel = new QPushButton("Cancel");
    m_dCancel->setEnabled(false);
    btns->addWidget(m_dStart);
    btns->addWidget(m_dCancel);
    btns->addStretch(1);
    v->addLayout(btns);

    m_dBar = new QProgressBar;
    m_dStatus = new QLabel("Idle");
    m_dLog = new QPlainTextEdit;
    m_dLog->setReadOnly(true);
    m_dLog->setMaximumBlockCount(5000);
    m_dStage = new QLabel("Ready");
    m_dStage->setWordWrap(true);
    m_dStage->setStyleSheet("font-size: 15px; font-weight: bold; color: #555555;");
    v->addWidget(m_dStage);
    v->addWidget(barWithPercent(m_dBar));
    v->addWidget(m_dStatus);
    v->addWidget(m_dLog, 1);

    connect(m_dPlatform, &QComboBox::currentIndexChanged, this, [this] {
        m_dNote->setText(platformInfo(dumpPlatform()).dumpNote);
    });
    m_dNote->setText(platformInfo(dumpPlatform()).dumpNote);
    // Settings that only mean something to redumper.
    auto engineChanged = [this] {
        const bool rd = m_dEngine->currentData().toString() == "redumper";
        for (QWidget *x : {static_cast<QWidget *>(m_dProfile), static_cast<QWidget *>(m_dOverread), static_cast<QWidget *>(m_dExtra),
                           static_cast<QWidget *>(m_dRetries), static_cast<QWidget *>(m_dAutoFix)})
            x->setEnabled(rd);
        for (const auto &e : engines())
            if (e.id == m_dEngine->currentData().toString())
                m_dNote->setText(platformInfo(dumpPlatform()).dumpNote + (rd || e.note.isEmpty() ? QString() : "\n" + e.note));
    };
    connect(m_dEngine, &QComboBox::currentIndexChanged, this, engineChanged);
    engineChanged();
    connect(m_dStart, &QPushButton::clicked, this, &MainWindow::startDump);
    connect(m_dCancel, &QPushButton::clicked, this, [this] { m_runner.cancel(); });
    connect(m_dDrive, &QComboBox::currentTextChanged, this, [this] { refreshProfileLabel(); });
    return w;
}

QWidget *MainWindow::buildVerifyTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *top = new QHBoxLayout;
    m_vPlatform = new QComboBox;
    for (const auto &pi : allPlatforms()) m_vPlatform->addItem(pi.name, int(pi.platform));
    m_vFolder = new QLineEdit;
    m_vFolder->setPlaceholderText("Dump folder (or drop one onto this window)");
    m_vGo = new QPushButton("Verify");
    m_vRename = new QPushButton("Rename to Redump names");
    m_vRename->setEnabled(false);
    m_vFix = new QPushButton("Apply offset fix");
    m_vFix->setEnabled(false);
    m_vSubmit = new QPushButton("Submission files");
    m_vSubmit->setToolTip("Write the Redump submission summary and a zip of the unmodified logs into the dump folder");
    top->addWidget(m_vPlatform);
    top->addWidget(m_vFolder, 1);
    top->addWidget(browseButton(m_vFolder, false, w));
    top->addWidget(m_vGo);
    top->addWidget(m_vFix);
    top->addWidget(m_vRename);
    top->addWidget(m_vSubmit);
    v->addLayout(top);
    m_vBar = new QProgressBar;
    m_vBanner = new QLabel;
    m_vBanner->setAlignment(Qt::AlignCenter);
    m_vBanner->setMinimumHeight(40);
    m_vOut = new QTextEdit;
    m_vOut->setReadOnly(true);
    m_vOut->setFontFamily("Consolas");
    v->addWidget(barWithPercent(m_vBar));
    v->addWidget(m_vBanner);
    v->addWidget(m_vOut, 1);
    connect(m_vSubmit, &QPushButton::clicked, this, [this] {
        const QString folder = m_vFolder->text();
        QString txt, zip, e;
        if (folder.isEmpty() || !submission::write(folder, &txt, &zip, &e)) {
            QMessageBox::warning(this, "Submission files", folder.isEmpty() ? "Pick a dump folder first." : e);
            return;
        }
        m_vOut->append(QString("\nSubmission files written:\n  ") + txt + "\n  " + zip);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(txt).absolutePath()));
    });
    connect(m_vRename, &QPushButton::clicked, this, &MainWindow::renameLast);
    connect(m_vFix, &QPushButton::clicked, this, &MainWindow::applyFixes);
    connect(m_vGo, &QPushButton::clicked, this, [this] {
        m_afterDump = false;
        if (!m_vFolder->text().isEmpty()) startVerify(m_vFolder->text(), verifyPlatform());
    });
    return w;
}

static QString humanSize(qint64 b) { return QString::number(b / 1048576.0, 'f', 1) + " MB"; }

bool MainWindow::downloadTool(setup::Tool t, bool confirm)
{
    const QString name = setup::toolName(t);
    setup::Release rel;
    QString err;
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = setup::latest(t, &rel, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) { QMessageBox::warning(this, "Download " + name, "Could not look up the newest release:\n" + err); return false; }
    }
    if (confirm) {
        const QString src = t == setup::Tool::Redumper ? "github.com/superg/redumper"
                          : t == setup::Tool::Dic ? "github.com/saramibreak/DiscImageCreator" : "github.com/mamedev/mame";
        QString note = t == setup::Tool::Chdman
            ? "\n\nThis is the whole MAME package; only chdman.exe is taken out of it and the rest is thrown away."
            : QString();
        if (rel.selfRecorded)
            note = "\n\nThe author publishes no checksum. The SHA-256 checked here was recorded by this program's author when the file was first downloaded, so it detects a changed file but is not an official value.";
        if (QMessageBox::question(this, "Download " + name,
                QString("Download %1 (%2) from %3?\n\nFile: %4 (%5)\nIts SHA-256 is checked before anything is kept.%6\n\nIt goes into: %7")
                    .arg(name, rel.tag, src, rel.name, humanSize(rel.size), note, dataDir()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return false;
    }
    QProgressDialog pd("Downloading " + rel.name + "...", "", 0, 100, this);
    pd.setCancelButton(nullptr);
    pd.setWindowModality(Qt::WindowModal);
    pd.setMinimumDuration(0);
    pd.setValue(0);
    const QString path = setup::install(t, rel, [&](qint64 d, qint64 total) {
        pd.setValue(total > 0 ? int(d * 100 / total) : 0);
        QApplication::processEvents();
    }, &err);
    pd.close();
    if (path.isEmpty()) { QMessageBox::warning(this, "Download " + name, "Failed:\n" + err); return false; }
    if (t == setup::Tool::Dic) { /* found by location, nothing to store */ }
    else if (t == setup::Tool::Redumper) { m_cfg.redumperPath = path; m_sRedumper->setText(path); }
    else { m_cfg.chdmanPath = path; m_sChdman->setText(path); }
    saveConfig(m_cfg);
    m_dLog->appendPlainText("installed " + name + " " + rel.tag + " -> " + path);
    return true;
}

void MainWindow::offerMissingTools()
{
    // redumper is needed to dump; chdman only for CHD compression, so it is not nagged about.
    if (!setup::present(setup::Tool::Redumper, m_cfg)) downloadTool(setup::Tool::Redumper);
}

QWidget *MainWindow::buildSettingsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *form = new QFormLayout;
    m_sRedumper = new QLineEdit(m_cfg.redumperPath);
    form->addRow("redumper.exe", row(m_sRedumper, browseButton(m_sRedumper, true, w)));
    m_sChdman = new QLineEdit(m_cfg.chdmanPath);
    m_sChdman->setPlaceholderText("optional: chdman.exe for CHD compression (or place it next to this program)");
    form->addRow("chdman.exe", row(m_sChdman, browseButton(m_sChdman, true, w)));
    v->addLayout(form);

    auto *save = new QPushButton("Save settings");
    connect(save, &QPushButton::clicked, this, [this] { saveSettings(); });
    auto *getRedumper = new QPushButton("Download redumper");
    auto *getChdman = new QPushButton("Download chdman");
    getRedumper->setToolTip("Fetches the newest redumper from github.com/superg/redumper into this program's folder");
    getChdman->setToolTip("Fetches chdman.exe from the newest MAME release (github.com/mamedev/mame) into this program's folder");
    connect(getRedumper, &QPushButton::clicked, this, [this] { downloadTool(setup::Tool::Redumper); });
    auto *about = new QPushButton("About && credits");
    connect(about, &QPushButton::clicked, this, [this] {
        QMessageBox::about(this, "About psxrdc2",
            "<b>psxrdc2</b> (MIT licence, Slopinator99)<br><br>"
            "Built on the work of:<ul>"
            "<li><a href='http://redump.org'>Redump</a>: the database and dumping standards</li>"
            "<li><a href='https://github.com/superg/redumper'>redumper</a> by superg (GPL-3.0)</li>"
            "<li><a href='https://github.com/saramibreak/DiscImageCreator'>DiscImageCreator</a> by saramibreak (Apache-2.0)</li>"
            "<li>chdman from <a href='https://github.com/mamedev/mame'>MAME</a> (BSD-3-Clause / GPL-2.0+)</li>"
            "<li><a href='https://www.qt.io'>Qt 6</a> (LGPL-3.0, linked statically)</li></ul>"
            "These tools are downloaded on request and keep their own licences. "
            "Not affiliated with or endorsed by Redump.<br>See THIRD-PARTY-NOTICES.md.");
    });
    connect(getChdman, &QPushButton::clicked, this, [this] { downloadTool(setup::Tool::Chdman); });
    auto *btns = new QHBoxLayout;
    btns->addWidget(save);
    btns->addWidget(getRedumper);
    btns->addWidget(getChdman);
    btns->addWidget(about);
    btns->addStretch(1);
    v->addLayout(btns);

    auto *box = new QGroupBox("Redump DAT files");
    auto *bl = new QVBoxLayout(box);
    m_sDat = new QLabel;
    bl->addWidget(m_sDat);
    auto *br = new QHBoxLayout;
    auto *datPick = new QComboBox;
    for (const auto &pi : allPlatforms()) datPick->addItem(pi.name, int(pi.platform));
    br->addWidget(datPick);
    {
        auto *b = new QPushButton("Download / update this database");
        connect(b, &QPushButton::clicked, this, [this, datPick, b] {
            const Platform p = Platform(datPick->currentData().toInt());
            b->setEnabled(false);
            auto *proc = new QProcess(this);
            connect(proc, &QProcess::finished, this, [this, proc, b, p](int code) {
                b->setEnabled(true);
                m_dat[int(p)] = DatIndex();
                if (code != 0)
                    QMessageBox::warning(this, "DAT update failed",
                        "Could not download the DAT (Redump may need a login).\n"
                        "Download it manually from redump.org and put the .dat in:\n" + datDir(p));
                saveSettings();
                proc->deleteLater();
            });
            proc->start(QCoreApplication::applicationFilePath(), {"update-dat", "--platform", platformId(p)});
        });
        br->addWidget(b);
    }
    bl->addLayout(br);
    v->addWidget(box);
    v->addStretch(1);
    saveSettings();
    return w;
}

void MainWindow::saveSettings()
{
    m_cfg.redumperPath = m_sRedumper->text();
    m_cfg.chdmanPath = m_sChdman->text();
    m_cfg.outputDir = m_dOut->text();
    m_cfg.speed = m_dSpeed->text().trimmed();
    m_cfg.retries = m_dRetries->value();
    m_cfg.useProfile = m_dProfile->isChecked();
    m_cfg.autoRename = m_dRename->isChecked();
    m_cfg.autoFix = m_dAutoFix->isChecked();
    saveConfig(m_cfg);
    QStringList lines;
    for (const auto &pi : allPlatforms()) {
        const QString f = findDatFile(pi.platform);
        lines << pi.name + ": " + (f.isEmpty() ? QString("(not downloaded)") : QFileInfo(f).fileName());
    }
    m_sDat->setText(lines.join("\n"));
}

Platform MainWindow::dumpPlatform() const { return Platform(m_dPlatform->currentData().toInt()); }
Platform MainWindow::verifyPlatform() const { return Platform(m_vPlatform->currentData().toInt()); }

void MainWindow::refreshDrives()
{
    // Never touch the drive while a dump is reading it.
    if (m_runner.running()) return;
    m_dDrive->clear();
    for (const auto &d : listOpticalDrives()) {
        // Many PlayStation discs have no Windows volume label; read SYSTEM.CNF for the serial.
        const DiscProbe pr = probeDisc(d.letter);
        QString text = d.letter + "  ";
        if (!pr.serial.isEmpty())
            text += pr.serial + (pr.isPs2 ? "  (PS2 " : "  (PS1 ") + (pr.isDvd ? "DVD)" : "CD)");
        else if (!d.label.isEmpty()) text += d.label;
        else text += pr.ok ? "(disc, unreadable filesystem)" : "(no disc)";
        m_dDrive->addItem(text, d.letter);
        m_discs[d.letter] = pr;
    }
    const int i = m_dDrive->findData(m_cfg.lastDrive);
    if (i >= 0) m_dDrive->setCurrentIndex(i);
    refreshProfileLabel();
    // Pre-fill name and platform from the disc unless the user has typed a name.
    const DiscProbe cur = m_discs.value(m_dDrive->currentData().toString());
    if (!cur.serial.isEmpty() && (m_dName->text().isEmpty() || m_dName->text() == m_autoName)) {
        m_autoName = QString(cur.serial).replace('-', '_');
        m_dName->setText(m_autoName);
        m_dPlatform->setCurrentIndex(m_dPlatform->findData(int(cur.isPs2 ? Platform::PS2 : Platform::PSX)));
    }
}

void MainWindow::refreshProfileLabel()
{
    const QString d = m_dDrive->currentData().toString();
    if (m_cfg.profiles.contains(d)) {
        const auto &p = m_cfg.profiles[d];
        m_dProfileLbl->setText(QString("Saved: %1 | offset %2 | %3 | %4").arg(p.inquiry, p.readOffset,
                               p.readMethod, p.sectorOrder));
    } else {
        m_dProfileLbl->setText("No profile yet: the first dump autodetects and saves one.");
    }
}

void MainWindow::startDump()
{
    const QString engine = m_dEngine->currentData().toString();
    QString redumper = engine == "dic" ? setup::findDic(m_cfg) : findRedumper(m_cfg);
    if (redumper.isEmpty() && engine == "dic" && downloadTool(setup::Tool::Dic)) redumper = setup::findDic(m_cfg);
    if (redumper.isEmpty()) {
        if (engine != "dic") QMessageBox::warning(this, "redumper", "Set the redumper.exe path in Settings.");
        return;
    }
    if (m_dDrive->currentData().toString().isEmpty() || m_dName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Dump", "Pick a drive and enter a name.");
        return;
    }
    saveSettings();
    m_refinePass = 0;
    m_eccPass = 0;
    m_req = DumpRequest();
    m_req.engine = engine;
    m_req.platform = dumpPlatform();
    m_req.drive = m_dDrive->currentData().toString();
    m_req.name = m_dName->text().trimmed();
    m_req.outDir = m_dOut->text();
    m_req.speed = m_dSpeed->text().trimmed();
    m_req.retries = m_dRetries->value();
    m_req.overwrite = m_dOverwrite->isChecked();
    m_req.overreadLeadout = m_dOverread->isChecked();
    m_req.dvd = m_discs.value(m_req.drive).isDvd;
    m_req.extraArgs = QProcess::splitCommand(m_dExtra->text());
    if (m_dProfile->isChecked() && m_cfg.profiles.contains(m_req.drive)) m_req.profile = m_cfg.profiles[m_req.drive];
    m_cfg.lastDrive = m_req.drive;
    saveConfig(m_cfg);

    m_dLog->clear();
    m_dBar->setValue(0);
    QString err;
    if (!m_runner.start(redumper, m_req, &err)) { QMessageBox::warning(this, "Dump", err); return; }
    m_status = QJsonObject{{"name", m_req.name}, {"drive", m_req.drive}, {"folder", dumpFolder(m_req)}};
    m_lastStatusPct = -1;
    writeStatus("dumping", {{"phase", "starting"}});
    m_dStart->setEnabled(false);
    m_dCancel->setEnabled(true);
    m_dStatus->setText("Starting...");
    setStage("Step 1: dumping the disc");
}

void MainWindow::setStage(const QString &text, const QString &color)
{
    m_dStage->setText(text);
    m_dStage->setStyleSheet("font-size: 15px; font-weight: bold; color: " + color + ";");
    m_dLog->appendPlainText("=== " + text);
}

void MainWindow::onDumpFinished(int code, bool crashed)
{
    // Unrecoverable C2 errors left at the dump speed: re-read the bad sectors at 4x (drives
    // often return clean data that slowly), rebuild the bin/cue, and let the Redump hash decide.
    const bool failed = code != 0 || crashed;
    const QString redumper = findRedumper(m_cfg);
    QString startErr;
    const bool redumperRun = m_req.engine == "redumper";
    if (!redumperRun) {
        // single plain pass: nothing to refine
    } else if (m_req.command == "disc" && !m_req.dvd
        && (lastC2Samples(m_req) > 0
            || (failed && pickLog(dumpFolder(m_req)).errors.join(' ').contains("data errors detected")))) {
        m_dLog->appendPlainText("data errors remain after the dump: re-reading the bad sectors slowly (4x)");
        m_req.command = "refine";
        m_req.speed = "4";
        m_req.retries = 100;
        m_refinePass = 1;
        m_dBar->setValue(0);
        setStage("Step 2: data errors remain. Re-reading the bad sectors slowly at 4x, pass 1 (up to 10)", "#b26a00");
        if (m_runner.start(redumper, m_req, &startErr)) { writeStatus("refining", {{"pass", m_refinePass}}); return; }
    } else if (m_req.command == "refine" && !failed) {
        // Each pass leaves fewer bad samples (29 -> 18 -> 9 -> 2 -> 0 on a real disc); repeat until clean.
        const int c2 = lastC2Samples(m_req);
        m_dLog->appendPlainText(QString("refine pass %1: %2 C2 samples left").arg(m_refinePass).arg(c2));
        if (c2 != 0 && m_refinePass < 10) {
            ++m_refinePass;
            m_dBar->setValue(0);
            setStage(QString("Step 2: re-reading the bad sectors at 4x, pass %1 (up to 10). %2 bad samples were left after the last pass")
                         .arg(m_refinePass).arg(c2), "#b26a00");
            if (m_runner.start(redumper, m_req, &startErr)) {
                writeStatus("refining", {{"pass", m_refinePass}, {"c2Left", c2}});
                return;
            }
        }
        m_dLog->appendPlainText("rebuilding the bin/cue from the re-read dump");
        m_req.command = "split";
        setStage("Step 3: rebuilding the bin/cue from the re-read dump", "#b26a00");
        if (m_runner.start(redumper, m_req, &startErr)) { writeStatus("splitting"); return; }
    } else if ((m_req.command == "disc" || m_req.command == "split") && !failed && !m_req.dvd && m_eccPass < 3) {
        // Sectors that fail the ECC check carry no C2 flag, so refine skips them. Flag them and re-read at 1x.
        const QVector<int> bad = eccBadLbas(redumper, m_req);
        if (!bad.isEmpty() && markForReread(m_req, bad)) {
            ++m_eccPass;
            m_dLog->appendPlainText(QString("%1 sector(s) fail the error check (first: LBA %2); re-reading them at 1x")
                                        .arg(bad.size()).arg(bad.first()));
            m_req.command = "refine";
            m_req.speed = "1";
            m_req.retries = 100;
            m_refinePass = 1;
            m_dBar->setValue(0);
            setStage(QString("Step 2: %1 sector(s) fail the error check. Re-reading them at 1x (attempt %2 of 3)")
                         .arg(bad.size()).arg(m_eccPass), "#b26a00");
            if (m_runner.start(redumper, m_req, &startErr)) {
                writeStatus("refining", {{"eccSectors", bad.size()}, {"attempt", m_eccPass}});
                return;
            }
        }
    }
    if (code != 0 || crashed) writeStatus("dump-failed", {{"exitCode", code}});
    else writeStatus("verifying");
    m_dStart->setEnabled(true);
    m_dCancel->setEnabled(false);
    if (code != 0 || crashed) {
        m_dStatus->setText(QString("%1 exited with code %2").arg(redumperRun ? "redumper" : "DiscImageCreator").arg(code));
        const LogInfo fl = pickLog(dumpFolder(m_req));
        QString why = fl.errors.isEmpty() ? QString("redumper exited with code %1").arg(code) : fl.errors.last();
        if (m_req.command == "split" && m_refinePass > 0)
            why += QString(" (after %1 slow re-read pass%2)").arg(m_refinePass).arg(m_refinePass == 1 ? "" : "es");
        setStage("Dump failed: " + why + ". The partial dump is kept in " + dumpFolder(m_req), "#b71c1c");
        return;
    }
    m_dBar->setValue(100);
    const QString folder = dumpFolder(m_req);
    const LogInfo li = pickLog(folder);
    const DriveProfile prof = redumperRun ? profileFromLog(li) : DriveProfile();
    if (prof.valid()) {
        m_cfg.profiles[m_req.drive] = prof;
        saveConfig(m_cfg);
        refreshProfileLabel();
        m_dLog->appendPlainText("saved drive profile for " + m_req.drive);
    }
    m_afterDump = true;
    m_dStatus->setText("Dump finished, verifying...");
    setStage("Step 4: verifying against Redump");
    m_vFolder->setText(folder);
    m_vPlatform->setCurrentIndex(m_vPlatform->findData(int(m_req.platform)));
    m_tabs->setCurrentIndex(1);
    startVerify(folder, m_req.platform);
}

void MainWindow::openFolder(const QString &folder)
{
    m_vFolder->setText(QDir::fromNativeSeparators(folder));
    m_tabs->setCurrentIndex(1);
    startVerify(folder, verifyPlatform());
}

void MainWindow::startVerify(const QString &folder, Platform p)
{
    if (m_verifying) return;
    m_verifying = true;
    m_vGo->setEnabled(false);
    m_vBar->setValue(0);
    m_vBanner->setText("Verifying...");
    m_vBanner->setStyleSheet("");
    m_vOut->clear();
    const QString dir = QDir::fromNativeSeparators(folder);
    DatIndex *dat = &m_dat[int(p)];
    QThread *t = QThread::create([this, dir, p, dat] {
        QString err;
        if (!dat->loaded() && !loadDat(p, *dat, &err)) {
            QMetaObject::invokeMethod(this, [this, err] {
                m_verifying = false;
                m_vGo->setEnabled(true);
                m_vBanner->setText("DAT missing");
                m_vOut->setPlainText(err + "\nSee Settings > Redump DAT files.");
            });
            return;
        }
        const VerifyResult r = verifyFolder(dir, p, *dat, [this](const QString &, qint64 done, qint64 total) {
            const int pct = total ? int(done * 100 / total) : 0;
            QMetaObject::invokeMethod(this, [this, pct] { m_vBar->setValue(pct); });
            return true;
        });
        lib::writeCache(dir, r, platformId(p));     // the Library tab reads this
        // A failing audio track may only be off by a write-offset shift; look for one.
        QVector<ShiftFix> fixes;
        if (r.overall == "FAIL" && !r.game.isEmpty()) {
            QMetaObject::invokeMethod(this, [this] { m_vBanner->setText("Checking for an offset fix..."); });
            fixes = findAudioShifts(dir, r, *dat);
        }
        QMetaObject::invokeMethod(this, [this, r, fixes] {
            m_verifying = false;
            m_vGo->setEnabled(true);
            m_vBar->setValue(100);
            showResult(r);
            m_dStatus->setText("Idle");
            if (m_afterDump)
                setStage(r.overall == "PASS" ? "Done: PASS. " + r.game
                                             : "Verification: " + r.overall + (r.game.isEmpty() ? QString() : " (" + r.game + ")"),
                         r.overall == "PASS" ? "#1b5e20" : "#b71c1c");
            m_fixes = fixes;
            m_vFix->setEnabled(!fixes.isEmpty());
            if (!fixes.isEmpty()) {
                for (const auto &f : fixes)
                    m_vOut->append(QString("\nDiagnosis: %1 equals Redump track '%2' after shifting %3 samples "
                                           "(%4 bytes), zero-filling the end.\n"
                                           "Typical cause: write offset differs (drive cannot read the lead-out).")
                                       .arg(f.file, f.datRom).arg(f.samples).arg(f.samples * 4));
                if (m_afterDump && m_cfg.autoFix) { applyFixes(); return; }
                m_vOut->append("\nPress \"Apply offset fix\" to correct it (the original is kept in _original/).");
            }
            const bool wasDump = m_afterDump;
            if (m_afterDump && m_cfg.autoRename && r.overall == "PASS") renameLast();
            if (wasDump) {
                QJsonArray files;
                for (const auto &f : r.files)
                    files.append(QJsonObject{{"name", f.name}, {"verdict", f.verdict}, {"detail", f.detail}});
                writeStatus("done", {{"overall", r.overall}, {"game", r.game}, {"files", files},
                                     {"finalFolder", m_vFolder->text()}});
            }
            m_afterDump = false;
        });
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void MainWindow::applyFixes()
{
    const QString dir = QDir::fromNativeSeparators(m_vFolder->text());
    QStringList log;
    for (const auto &f : m_fixes) {
        QString err;
        if (applyShift(dir, f, &err)) log << QString("fixed %1 (shifted %2 samples)").arg(f.file).arg(f.samples);
        else log << QString("could not fix %1: %2").arg(f.file, err);
    }
    m_fixes.clear();
    m_vFix->setEnabled(false);
    m_dLog->appendPlainText(log.join("\n"));
    // Re-verify; a dump in progress keeps its afterDump flag so rename still follows.
    startVerify(dir, verifyPlatform());
    m_vOut->setPlainText(log.join("\n"));
}

void MainWindow::writeStatus(const QString &state, const QJsonObject &extra)
{
    for (auto it = extra.begin(); it != extra.end(); ++it) m_status[it.key()] = it.value();
    m_status["state"] = state;
    m_status["updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QFile f(dataDir() + "/status.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_status).toJson());
}

void MainWindow::autoDump(const QMap<QString, QString> &o)
{
    m_tabs->setCurrentIndex(0);
    Platform p = Platform::PSX;
    // Qt itself swallows "--platform <plugin>", so the GUI launch option is "--system".
    platformFromId(o.value("system", o.value("platform", "psx")), p);
    m_dPlatform->setCurrentIndex(m_dPlatform->findData(int(p)));
    const int di = m_dDrive->findData(o.value("drive", m_cfg.lastDrive).toUpper());
    if (di >= 0) m_dDrive->setCurrentIndex(di);
    m_dName->setText(o.value("name"));
    if (o.contains("out")) m_dOut->setText(o["out"]);
    if (o.contains("speed")) m_dSpeed->setText(o["speed"]);
    if (o.contains("retries")) m_dRetries->setValue(o["retries"].toInt());
    m_dOverread->setChecked(o.contains("overread"));
    m_dRename->setChecked(!o.contains("norename"));
    m_dOverwrite->setChecked(o.contains("overwrite"));
    startDump();
}

void MainWindow::renameLast()
{
    const Platform p = verifyPlatform();
    const RenameResult rr = renameToRedump(m_last, m_dat[int(p)]);
    if (!rr.ok) {
        QMessageBox::warning(this, "Rename", "Not renamed: " + rr.error);
        return;
    }
    m_vOut->append("\nRenamed:\n  " + rr.actions.join("\n  ") + "\nNow at: " + rr.folder);
    m_vFolder->setText(rr.folder);
    m_vRename->setEnabled(false);
    m_last = VerifyResult();
}

void MainWindow::showResult(const VerifyResult &r)
{
    m_last = r;
    m_vRename->setEnabled(r.overall == "PASS");
    const bool ok = r.overall == "PASS", bad = r.overall == "FAIL";
    m_vBanner->setText(r.overall + (r.game.isEmpty() ? "" : "  -  " + r.game));
    m_vBanner->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: white; background: %1; border-radius: 4px")
                                 .arg(ok ? "#2e7d32" : bad ? "#c62828" : "#ef6c00"));
    m_vOut->setPlainText(r.toText());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls()) {
        QString p = u.toLocalFile();
        if (QFileInfo(p).isFile()) p = QFileInfo(p).absolutePath();
        if (!p.isEmpty()) { openFolder(p); break; }
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_runner.running()) {
        if (QMessageBox::question(this, "Dump running", "A dump is in progress. Cancel it and quit?") != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_runner.cancel();
    }
    saveSettings();
    e->accept();
}
