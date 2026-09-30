// GenryBL V1 - «История версий»: every project keeps its own past next to story.txt (projects/<id>/history).
// Nothing a maker typed is ever lost for good: a snapshot every few minutes of work, one when the project is
// opened, one BEFORE a big cut (the text as it was on disk, not after), one before a version is brought back.
// Old ones thin out by themselves: the newest are all kept, then one a day.
#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>

namespace gb::history {

struct Version {
    QString file;          // the file name inside history/ ("2026-09-30_12-04-55_open.txt")
    QDateTime when;
    QString tag;           // "" (work) | open | cut | restore | modid
    int lines = 0;
};

struct DiffLine {
    char kind = '=';       // '=' the same, '-' only in the old text, '+' only in the new one, '~' «…» (lines skipped)
    int oldLine = 0, newLine = 0;   // 1-based, 0 = not in that text
    QString text;
};

// the story is being saved as `next` while `onDisk` is what story.txt holds now: writes the snapshots that are due
// (a cut of many lines keeps `onDisk`; a few minutes of work keep `next`). Returns how many were written.
int onSave(const QString& dir, const QString& onDisk, const QString& next, const QDateTime& now = QDateTime::currentDateTime());
// one snapshot right now, with a reason (open / restore / modid); nothing is written if the newest one is the same text
bool snapshot(const QString& dir, const QString& text, const QString& tag, const QDateTime& now = QDateTime::currentDateTime());
QVector<Version> list(const QString& dir);                 // newest first
QString read(const QString& dir, const QString& file);
void prune(const QString& dir, const QDateTime& now = QDateTime::currentDateTime());
// what changed from `a` to `b`, line by line; runs of unchanged lines are folded to `context` lines around the changes
QVector<DiffLine> diff(const QString& a, const QString& b, int context = 2);
// «+12 −3»: lines added / removed
QPair<int, int> diffCount(const QString& a, const QString& b);

} // namespace gb::history
