#include "History.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace gb::history {

namespace {

const char* const kStamp = "yyyy-MM-dd_HH-mm-ss";
constexpr int kKeepAll = 60;          // the newest snapshots, all of them
constexpr int kKeepDays = 120;        // older: the last one of each day, for this long
constexpr qint64 kWorkGap = 4 * 60;   // seconds of work between two ordinary snapshots

QStringList linesOf(const QString& s)
{
    QString t = s;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList l = t.split(QLatin1Char('\n'));
    if (!l.isEmpty() && l.last().isEmpty()) l.removeLast();
    return l;
}

bool writeText(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    const QByteArray b = text.toUtf8();
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(b) == b.size();
}

// the edit script of Myers' O(ND) algorithm over the lines between the common head and tail
QVector<DiffLine> script(const QStringList& a, const QStringList& b)
{
    int head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) ++head;
    int tail = 0;
    while (tail < a.size() - head && tail < b.size() - head && a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) ++tail;
    const int n = int(a.size()) - head - tail, m = int(b.size()) - head - tail;
    QVector<DiffLine> out;
    out.reserve(a.size() + b.size());
    for (int i = 0; i < head; ++i) out.push_back({'=', i + 1, i + 1, a[i]});
    // hashes: comparing lines by number is cheaper than by text
    QHash<QString, int> ids;
    QVector<int> ha(n), hb(m);
    for (int i = 0; i < n; ++i) ha[i] = ids.insert(a[head + i], ids.value(a[head + i], int(ids.size()))).value();
    for (int j = 0; j < m; ++j) hb[j] = ids.insert(b[head + j], ids.value(b[head + j], int(ids.size()))).value();
    const int max = n + m;
    QVector<QVector<int>> trace;
    QVector<int> v(2 * max + 2, 0);
    int dFound = -1;
    if (max == 0) dFound = 0;
    for (int d = 0; d <= max && dFound < 0; ++d) {
        trace.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && v[k - 1 + max] < v[k + 1 + max])) ? v[k + 1 + max] : v[k - 1 + max] + 1;
            int y = x - k;
            while (x < n && y < m && ha[x] == hb[y]) { ++x; ++y; }
            v[k + max] = x;
            if (x >= n && y >= m) { dFound = d; break; }
        }
        // a monster diff (two unrelated texts): give up on the minimal script, the whole middle is -/+
        if (dFound < 0 && qint64(trace.size()) * v.size() > 16000000) { dFound = -2; break; }
    }
    QVector<DiffLine> mid;
    if (dFound == -2) {
        for (int i = 0; i < n; ++i) mid.push_back({'-', head + i + 1, 0, a[head + i]});
        for (int j = 0; j < m; ++j) mid.push_back({'+', 0, head + j + 1, b[head + j]});
    } else if (max > 0) {
        trace.push_back(v);
        int x = n, y = m;
        for (int d = dFound; d > 0; --d) {
            const QVector<int>& pv = trace[d];
            const int k = x - y;
            const bool down = (k == -d || (k != d && pv[k - 1 + max] < pv[k + 1 + max]));
            const int pk = down ? k + 1 : k - 1;
            const int px = pv[pk + max], py = px - pk;
            while (x > px + (down ? 0 : 1) && y > py + (down ? 1 : 0)) { --x; --y; mid.push_back({'=', head + x + 1, head + y + 1, a[head + x]}); }
            if (down) { --y; mid.push_back({'+', 0, head + y + 1, b[head + y]}); }
            else { --x; mid.push_back({'-', head + x + 1, 0, a[head + x]}); }
        }
        while (x > 0 && y > 0) { --x; --y; mid.push_back({'=', head + x + 1, head + y + 1, a[head + x]}); }
        std::reverse(mid.begin(), mid.end());
    }
    out += mid;
    for (int i = 0; i < tail; ++i) {
        const int ai = int(a.size()) - tail + i, bi = int(b.size()) - tail + i;
        out.push_back({'=', ai + 1, bi + 1, a[ai]});
    }
    return out;
}

} // namespace

QVector<Version> list(const QString& dir)
{
    QVector<Version> out;
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo& fi : files) {
        const QString base = fi.completeBaseName();
        const QDateTime when = QDateTime::fromString(base.left(19), QLatin1String(kStamp));
        if (!when.isValid()) continue;
        Version v;
        v.file = fi.fileName();
        v.when = when;
        v.tag = base.size() > 20 ? base.mid(20) : QString();
        QFile f(fi.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) v.lines = int(linesOf(QString::fromUtf8(f.readAll())).size());
        out.push_back(v);
    }
    return out;
}

QString read(const QString& dir, const QString& file)
{
    if (file.contains(QLatin1Char('/')) || file.contains(QLatin1Char('\\')) || file.contains(QLatin1String(".."))) return {};
    QFile f(dir + QLatin1Char('/') + file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool snapshot(const QString& dir, const QString& text, const QString& tag, const QDateTime& now)
{
    const QVector<Version> have = list(dir);
    if (!have.isEmpty() && read(dir, have.first().file) == text) return false;
    QString name = now.toString(QLatin1String(kStamp));
    if (!tag.isEmpty()) name += QLatin1Char('_') + tag;
    // two in the same second (open + cut): the second one waits for its own name
    QString path = dir + QLatin1Char('/') + name + QStringLiteral(".txt");
    for (int n = 1; QFileInfo::exists(path) && n < 60; ++n)
        path = dir + QLatin1Char('/') + now.addSecs(n).toString(QLatin1String(kStamp)) + (tag.isEmpty() ? QString() : QLatin1Char('_') + tag) + QStringLiteral(".txt");
    const bool ok = writeText(path, text);
    if (ok) prune(dir, now);
    return ok;
}

int onSave(const QString& dir, const QString& onDisk, const QString& next, const QDateTime& now)
{
    if (onDisk == next) return 0;
    int written = 0;
    // a cut: many lines gone at once (a slip of Ctrl+A, a paste over the story, a broken tool) - the text BEFORE it
    if (!onDisk.trimmed().isEmpty()) {
        const QPair<int, int> c = diffCount(onDisk, next);
        const int before = int(linesOf(onDisk).size());
        if (c.second >= 12 || (before >= 8 && c.second * 4 >= before)) written += snapshot(dir, onDisk, QStringLiteral("cut"), now);
    }
    // ordinary work: the newest text, once every few minutes
    const QVector<Version> have = list(dir);
    const bool due = have.isEmpty() || have.first().when.secsTo(now) >= kWorkGap;
    if (due && !next.trimmed().isEmpty()) written += snapshot(dir, next, QString(), now);
    return written;
}

void prune(const QString& dir, const QDateTime& now)
{
    const QVector<Version> have = list(dir);
    QSet<QString> days;
    for (int i = 0; i < have.size(); ++i) {
        if (i < kKeepAll) { days.insert(have[i].when.date().toString(Qt::ISODate)); continue; }
        const QString day = have[i].when.date().toString(Qt::ISODate);
        const bool firstOfDay = !days.contains(day);          // newest first: the first seen is the day's last
        if (firstOfDay && have[i].when.daysTo(now) <= kKeepDays) { days.insert(day); continue; }
        QFile::remove(dir + QLatin1Char('/') + have[i].file);
    }
}

QVector<DiffLine> diff(const QString& a, const QString& b, int context)
{
    const QVector<DiffLine> all = script(linesOf(a), linesOf(b));
    QVector<bool> keep(all.size(), false);
    for (int i = 0; i < all.size(); ++i)
        if (all[i].kind != '=')
            for (int j = qMax(0, i - context); j <= qMin(int(all.size()) - 1, i + context); ++j) keep[j] = true;
    QVector<DiffLine> out;
    bool folded = false;
    for (int i = 0; i < all.size(); ++i) {
        if (keep[i]) { out.push_back(all[i]); folded = false; continue; }
        if (!folded) { out.push_back({'~', 0, 0, QString()}); folded = true; }
    }
    return out;
}

QPair<int, int> diffCount(const QString& a, const QString& b)
{
    int add = 0, del = 0;
    for (const DiffLine& d : script(linesOf(a), linesOf(b))) {
        if (d.kind == '+') ++add;
        else if (d.kind == '-') ++del;
    }
    return {add, del};
}

} // namespace gb::history
