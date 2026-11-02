#pragma once
#include <QWidget>

#include "Core.h"
#include "Library.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

// Lists every dump under the output folder with its Redump status, and works on
// the selection: verify, compress to CHD, delete, open.
#include <functional>

class LibraryTab : public QWidget {
    Q_OBJECT
public:
    explicit LibraryTab(core::Config *cfg, QWidget *parent = nullptr);
    void refresh();
    // Asked to fetch chdman when compression needs it; returns true if it is now installed.
    std::function<bool()> onNeedChdman;

private:
    void fillTable();
    QVector<int> selectedRows() const;
    void verifyRows(const QVector<int> &rows);
    void compressRows(const QVector<int> &rows);
    void deleteRows(const QVector<int> &rows);
    void setBusy(bool busy);
    void logLine(const QString &s);

    core::Config *m_cfg;
    QVector<lib::Entry> m_entries;
    lib::DatCache m_dats;
    bool m_busy = false;

    QLineEdit *m_root;
    QTableWidget *m_table;
    QPushButton *m_scan, *m_verify, *m_verifyNew, *m_compress, *m_open, *m_delete;
    QCheckBox *m_delOriginals, *m_delScratch;
    QProgressBar *m_bar;
    QPlainTextEdit *m_log;
};
