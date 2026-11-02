#pragma once
#include <QJsonObject>
#include <QMainWindow>
#include <QMap>

#include "Core.h"
#include "Runner.h"
#include "Setup.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextEdit;
class QCheckBox;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void openFolder(const QString &folder);
    // Fill in the Dump tab from command-line options and start immediately.
    // Keys: drive, name, platform, out, speed, retries, overread, norename.
    void autoDump(const QMap<QString, QString> &opts);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildDumpTab();
    QWidget *buildVerifyTab();
    QWidget *buildSettingsTab();
    void refreshDrives();
    bool downloadTool(setup::Tool t, bool confirm = true);
    void offerMissingTools();
    void startDump();
    void onDumpFinished(int code, bool crashed);
    void startVerify(const QString &folder, core::Platform p);
    void showResult(const core::VerifyResult &r);
    bool ensureDat(core::Platform p);
    void saveSettings();
    void refreshProfileLabel();
    core::Platform dumpPlatform() const;
    core::Platform verifyPlatform() const;

    core::Config m_cfg;
    core::DatIndex m_dat[core::kPlatformCount];
    Runner m_runner;
    core::DumpRequest m_req;
    bool m_verifying = false;
    bool m_afterDump = false;
    core::VerifyResult m_last;
    void renameLast();
    void applyFixes();
    int m_refinePass = 0;
    int m_eccPass = 0;      // forced 1x re-reads of sectors that fail the ECC check (max 3)
    QMap<QString, core::DiscProbe> m_discs;
    QString m_autoName;
    QVector<core::ShiftFix> m_fixes;
    // Machine-readable progress for scripts: <app dir>/status.json
    void writeStatus(const QString &state, const QJsonObject &extra = {});
    QJsonObject m_status;
    int m_lastStatusPct = -1;

    QTabWidget *m_tabs;
    // dump tab
    QComboBox *m_dPlatform, *m_dDrive;
    QLineEdit *m_dName, *m_dOut;
    QLineEdit *m_dSpeed;
    QSpinBox *m_dRetries;
    QCheckBox *m_dOverwrite, *m_dProfile, *m_dRename, *m_dOverread, *m_dAutoFix;
    QPushButton *m_dStart, *m_dCancel;
    QProgressBar *m_dBar;
    QLabel *m_dStatus, *m_dProfileLbl, *m_dStage, *m_dNote;
    QLineEdit *m_dExtra;
    void setStage(const QString &text, const QString &color = "#0b5cad");
    QPlainTextEdit *m_dLog;
    // verify tab
    QComboBox *m_vPlatform;
    QLineEdit *m_vFolder;
    QPushButton *m_vGo, *m_vRename, *m_vFix;
    QProgressBar *m_vBar;
    QLabel *m_vBanner;
    QTextEdit *m_vOut;
    // settings tab
    QLineEdit *m_sRedumper, *m_sChdman;
    QLabel *m_sDat;
};
