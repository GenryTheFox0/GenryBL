#include "ModHub.h"
#include "Library.h"
#include "Rpa.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace gb {

namespace {

QString readSmall(const QString& path, qint64 max = 8 * 1024 * 1024)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > max) return {};
    return QString::fromUtf8(f.readAll());
}

QString cleanName(QString t)
{
    static const QRegularExpression tags(QStringLiteral("\\{[^}]*\\}"));
    t.remove(tags);
    return t.simplified().left(80);
}

HubMod scanOne(const QString& dir, const QString& source, const QString& id)
{
    HubMod m;
    m.source = source;
    m.id = id;
    m.dir = dir;
    m.genrybl = QFileInfo::exists(dir + QStringLiteral("/.genrybl_owner"));
    QStringList rpy, rpyc;
    QString preview;
    int previewScore = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QFileInfo fi = it.fileInfo();
        m.size += fi.size();
        ++m.files;
        const QString low = fi.fileName().toLower();
        if (low.endsWith(QLatin1String(".rpy"))) rpy << f;
        else if (low.endsWith(QLatin1String(".rpyc"))) rpyc << f;
        else if (low.endsWith(QLatin1String(".png")) || low.endsWith(QLatin1String(".jpg")) || low.endsWith(QLatin1String(".jpeg"))) {
            // the cover: preview.* at the top first, then anything that says cover / preview / icon / logo near the top
            const int depth = int(QDir(dir).relativeFilePath(f).count(QLatin1Char('/')));
            if (depth > 2) continue;
            int score = 0;
            if (low.startsWith(QLatin1String("preview"))) score = 10 - depth;
            else if (low.contains(QLatin1String("cover")) || low.contains(QLatin1String("preview"))) score = 7 - depth;
            else if (low.contains(QLatin1String("logo")) || low.contains(QLatin1String("icon")) || low.contains(QLatin1String("title"))) score = 4 - depth;
            if (score > previewScore) { previewScore = score; preview = f; }
        }
    }
    m.preview = preview;
    // the game's own Mods menu: mods["label"] = u"Name" (in the .rpy, or read out of the .rpyc)
    static const QRegularExpression reg(QStringLiteral("mods\\s*\\[\\s*u?[\"']([^\"']+)[\"']\\s*\\]\\s*=\\s*(u?[\"'](.+?)[\"'])?"));
    QSet<QString> seen;
    auto collect = [&](const QString& text) {
        for (auto mt = reg.globalMatch(text); mt.hasNext();) {
            const auto x = mt.next();
            const QString label = x.captured(1);
            if (seen.contains(label)) continue;
            seen.insert(label);
            const QString title = cleanName(x.captured(3));
            m.entries.push_back({label, title.isEmpty() ? label : title});
        }
    };
    QSet<QString> hasSource;
    for (const QString& f : rpy) {
        hasSource.insert(f + QLatin1Char('c'));
        collect(readSmall(f));
    }
    std::sort(rpyc.begin(), rpyc.end(), [](const QString& a, const QString& b) { return QFileInfo(a).size() < QFileInfo(b).size(); });
    int read = 0;
    for (const QString& f : rpyc) {
        if (hasSource.contains(f)) continue;
        if (++read > 40) break;
        collect(compiledScriptText(f));
    }
    m.title = !m.entries.isEmpty() ? m.entries.first().title : source == QLatin1String("workshop") ? workshopKnownTitle(id) : QString();
    if (m.title.isEmpty()) m.title = source == QLatin1String("workshop") ? QStringLiteral("Workshop ") + id : id;
    return m;
}

} // namespace

QVector<HubMod> scanGameMods(const QString& esRoot)
{
    QVector<HubMod> out;
    if (esRoot.isEmpty()) return out;
    const QString ws = Vfs::workshopDirFor(esRoot);
    if (!ws.isEmpty())
        for (const QFileInfo& fi : QDir(ws).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            out << scanOne(fi.absoluteFilePath(), QStringLiteral("workshop"), fi.fileName());
    static const QSet<QString> service{QStringLiteral("cache"), QStringLiteral("editor"), QStringLiteral("genry_presence"), QStringLiteral("__pycache__")};
    for (const QFileInfo& fi : QDir(esRoot + QStringLiteral("/game/mods")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = fi.fileName();
        if (name.startsWith(QLatin1String("_genry")) || name.startsWith(QLatin1String("gbgate_")) || service.contains(name)) continue;
        out << scanOne(fi.absoluteFilePath(), QStringLiteral("local"), name);
    }
    // the same entry from two places: the game keeps one of them, the other never starts
    QHash<QString, QStringList> where;
    for (const HubMod& m : out)
        for (const HubEntry& e : m.entries) where[e.label] << m.source + QLatin1Char(':') + m.id;
    for (HubMod& m : out) {
        const QString self = m.source + QLatin1Char(':') + m.id;
        for (const HubEntry& e : m.entries)
            for (const QString& w : where.value(e.label))
                if (w != self && !m.twins.contains(w)) m.twins << w;
    }
    std::sort(out.begin(), out.end(), [](const HubMod& a, const HubMod& b) { return a.title.compare(b.title, Qt::CaseInsensitive) < 0; });
    return out;
}

bool writePlayHook(const QString& esRoot, const QString& label, QString* err)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_\\.]*$"));
    if (!safe.match(label).hasMatch()) {
        if (err) *err = QStringLiteral("bad label");
        return false;
    }
    const QString dir = esRoot + QStringLiteral("/game/mods/_genry_play");
    QDir().mkpath(dir);
    const QString hook = QStringLiteral(
        "# -*- coding: utf-8 -*-\n"
        "# GenryBL: «Играть» in the Center - starts one mod the way the game's own Mods menu does, then deletes itself.\n\n"
        "init 999 python:\n"
        "    config.label_overrides[\"main_menu\"] = \"_genry_play\"\n\n"
        "label _genry_play:\n"
        "    python:\n"
        "        config.label_overrides.pop(\"main_menu\", None)\n"
        "        import os\n"
        "        _genry_play_dir = os.path.join(config.gamedir, \"mods\", \"_genry_play\")\n"
        "        for _genry_play_f in (\"_genry_play.rpy\", \"_genry_play.rpyc\"):\n"
        "            try:\n"
        "                os.remove(os.path.join(_genry_play_dir, _genry_play_f))\n"
        "            except Exception:\n"
        "                pass\n"
        "        try:\n"
        "            os.rmdir(_genry_play_dir)\n"
        "        except Exception:\n"
        "            pass\n"
        "        persistent.jump_to = \"%1\"\n"
        "    $ renpy.jump_out_of_context(\"start\")\n").arg(label);
    QFile f(dir + QStringLiteral("/_genry_play.rpy"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(hook.toUtf8()) < 0) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

void removePlayHook(const QString& esRoot)
{
    QDir(esRoot + QStringLiteral("/game/mods/_genry_play")).removeRecursively();
}

} // namespace gb
