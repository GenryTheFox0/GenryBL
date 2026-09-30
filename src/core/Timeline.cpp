#include "Timeline.h"
#include "Py.h"
#include "Text.h"
#include "Tr.h"

#include <QHash>
#include <QMap>

namespace gb {

namespace {

QString clip40(const QString& s)
{
    const QString t = s.simplified();
    return t.size() > 40 ? t.left(39) + QChar(0x2026) : t;
}

} // namespace

SceneTimeline sceneTimeline(const QString& storyText, int line, const CompileOptions& opt)
{
    Q_UNUSED(opt);
    SceneTimeline tl;
    const QStringList lines = pySplitLines(stripBom(storyText));
    // the scene around the cursor: from its «: name» to the next one
    int head = -1;
    for (int i = qMin(line, int(lines.size())) - 1; i >= 0; --i)
        if (pyStrip(lines[i]).startsWith(QLatin1Char(':'))) { head = i; break; }
    int end = int(lines.size());
    for (int i = (head < 0 ? 0 : head + 1); i < lines.size(); ++i)
        if (pyStrip(lines[i]).startsWith(QLatin1Char(':'))) { end = i; break; }
    if (head >= 0) {
        tl.scene = pyStrip(pyStrip(lines[head]).mid(1));
        tl.headLine = head + 1;
    }

    // the tracks, in the order a video editor stacks them
    QMap<QString, int> at;
    auto track = [&](const QString& id, const QString& title) -> TimelineTrack& {
        if (!at.contains(id)) {
            at.insert(id, int(tl.tracks.size()));
            tl.tracks.push_back({id, title, {}});
        }
        return tl.tracks[at.value(id)];
    };
    track(QStringLiteral("bg"), gbTr("Фон"));
    track(QStringLiteral("say"), gbTr("Реплики"));
    track(QStringLiteral("music"), gbTr("Музыка"));
    track(QStringLiteral("amb"), gbTr("Атмосфера и звуки"));
    track(QStringLiteral("fx"), gbTr("Эффекты"));
    track(QStringLiteral("flow"), gbTr("Выборы и переходы"));

    // open clips (they run until something ends them), by track id
    struct Open { int from = 0, line = 0; QString label, kind; };
    QHash<QString, Open> open;
    auto close = [&](const QString& id, int beat) {
        if (!open.contains(id)) return;
        const Open o = open.take(id);
        track(id, id).clips.push_back({o.from, qMax(o.from + 1, beat), o.line, o.label, o.kind, true});
    };
    auto start = [&](const QString& id, const QString& title, int beat, int ln, const QString& label, const QString& kind) {
        close(id, beat);
        track(id, title);
        open.insert(id, {beat, ln, label, kind});
    };
    auto one = [&](const QString& id, const QString& title, int beat, int ln, const QString& label, const QString& kind, bool movable = true) {
        track(id, title).clips.push_back({beat, beat + 1, ln, label, kind, movable});
    };
    auto charTitle = [](const QString& tag) {
        static const QHash<QString, const char*> names{{QStringLiteral("dv"), "Алиса"}, {QStringLiteral("sl"), "Славя"}, {QStringLiteral("un"), "Лена"},
                                                      {QStringLiteral("us"), "Ульяна"}, {QStringLiteral("mi"), "Мику"}, {QStringLiteral("mt"), "Ольга Дмитриевна"},
                                                      {QStringLiteral("el"), "Электроник"}, {QStringLiteral("sh"), "Шурик"}, {QStringLiteral("cs"), "Виола"},
                                                      {QStringLiteral("mz"), "Женя"}, {QStringLiteral("uv"), "Юля"}, {QStringLiteral("pi"), "Пионер"}};
        return names.contains(tag) ? QString::fromUtf8(names.value(tag)) : tag;
    };

    int beat = 0;
    for (int i = (head < 0 ? 0 : head + 1); i < end; ++i) {
        const QString s = pyStrip(lines[i]);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@'))) continue;
        const int ln = i + 1;
        tl.beatLines << ln;
        const QString first = firstWord(s);
        const QString cmd = normalizeCommand(first);
        const QString rest = pyStrip(s.mid(first.size()));
        QStringList w = pySplit(rest);
        if (!w.isEmpty() && isEffect(w.last())) w.removeLast();

        if (isChoiceItemLine(s) || cmd == QLatin1String("choice") || cmd == QLatin1String("endchoice") || cmd == QLatin1String("jump") ||
            cmd == QLatin1String("callscene") || cmd == QLatin1String("ifjump") || cmd == QLatin1String("ifpersistent") || cmd == QLatin1String("ifitem") ||
            cmd == QLatin1String("map") || cmd == QLatin1String("codelock") || cmd == QLatin1String("phonecall") || cmd == QLatin1String("terminal") ||
            cmd == QLatin1String("rps") || cmd == QLatin1String("endgame") || cmd == QLatin1String("return") || cmd == QLatin1String("screenmenu") ||
            cmd == QLatin1String("bestmeter")) {
            // the choice's own lines stay with it: moving one out would break the block
            one(QStringLiteral("flow"), gbTr("Выборы и переходы"), beat, ln, clip40(s), QStringLiteral("flow"),
                !(isChoiceItemLine(s) || cmd == QLatin1String("choice") || cmd == QLatin1String("endchoice")));
        } else if (cmd == QLatin1String("bg") || cmd == QLatin1String("showbg") || cmd == QLatin1String("cg")) {
            start(QStringLiteral("bg"), gbTr("Фон"), beat, ln, (cmd == QLatin1String("cg") ? QStringLiteral("CG ") : QString()) + w.join(QLatin1Char(' ')),
                  QStringLiteral("bg"));
            // a new place: ES clears the characters with it
            if (cmd != QLatin1String("showbg"))
                for (const QString& id : open.keys())
                    if (id.startsWith(QLatin1String("char:"))) close(id, beat);
        } else if (cmd == QLatin1String("show") || cmd == QLatin1String("mirror") || cmd == QLatin1String("mirrorbig") || cmd == QLatin1String("bigshow") ||
                   cmd == QLatin1String("fullheight") || cmd == QLatin1String("enterleft") || cmd == QLatin1String("enterright") ||
                   cmd == QLatin1String("walk") || cmd == QLatin1String("pulse") || cmd == QLatin1String("ghostmove") || cmd == QLatin1String("zoomshow")) {
            QStringList img = w;
            while (!img.isEmpty() && (isPosition(img.last()) || isWalkPosition(img.last()) || pyIsNumber(img.last()))) img.removeLast();
            const QString image = img.join(QLatin1Char(' ')).section(QLatin1Char('|'), 0, 0).trimmed();
            const QString tag = image.section(QLatin1Char(' '), 0, 0);
            // «… move»: the character glides there from where it stood - a keyframe of the scene
            const bool key = w.contains(QStringLiteral("move"));
            if (!tag.isEmpty()) start(QStringLiteral("char:") + tag, charTitle(tag), beat, ln, key ? QStringLiteral("◆ ") + image : image, QStringLiteral("char"));
        } else if (cmd == QLatin1String("hide") || cmd == QLatin1String("exitleft") || cmd == QLatin1String("exitright")) {
            const QString tag = w.value(0);
            if (!tag.isEmpty()) close(QStringLiteral("char:") + tag, beat + 1);
            one(QStringLiteral("fx"), gbTr("Эффекты"), beat, ln, clip40(s), QStringLiteral("moment"));
        } else if (cmd == QLatin1String("hideall")) {
            for (const QString& id : open.keys())
                if (id.startsWith(QLatin1String("char:"))) close(id, beat + 1);
            one(QStringLiteral("fx"), gbTr("Эффекты"), beat, ln, clip40(s), QStringLiteral("moment"));
        } else if (cmd == QLatin1String("music") || cmd == QLatin1String("musicfile") || cmd == QLatin1String("musicqueue")) {
            start(QStringLiteral("music"), gbTr("Музыка"), beat, ln, w.value(0), QStringLiteral("music"));
        } else if (cmd == QLatin1String("stopmusic")) {
            close(QStringLiteral("music"), beat + 1);
        } else if (cmd == QLatin1String("ambience")) {
            start(QStringLiteral("amb"), gbTr("Атмосфера и звуки"), beat, ln, w.value(0), QStringLiteral("amb"));
        } else if (cmd == QLatin1String("stopambience")) {
            close(QStringLiteral("amb"), beat + 1);
        } else if (cmd == QLatin1String("stopallaudio") || cmd == QLatin1String("stop")) {
            close(QStringLiteral("music"), beat + 1);
            close(QStringLiteral("amb"), beat + 1);
        } else if (cmd == QLatin1String("sound") || cmd == QLatin1String("soundfile") || cmd == QLatin1String("voice") || cmd == QLatin1String("voicefile")) {
            one(QStringLiteral("sfx"), gbTr("Звуки"), beat, ln, w.value(0), QStringLiteral("sfx"));
        } else if (cmd == QLatin1String("weather") || cmd == QLatin1String("colorfilter") || cmd == QLatin1String("flashlight") ||
                   cmd == QLatin1String("parallax") || cmd == QLatin1String("staticfx") || cmd == QLatin1String("vhsfx") ||
                   cmd == QLatin1String("glitchfx") || cmd == QLatin1String("dreamfx") || cmd == QLatin1String("memoryfx") || cmd == QLatin1String("dream")) {
            const QString v = w.value(0).toLower();
            const bool off = v == QString::fromUtf8("стоп") || v == QString::fromUtf8("нет") || v == QString::fromUtf8("выкл") || v == QLatin1String("off");
            const QString id = QStringLiteral("fx:") + cmd;
            if (off) close(id, beat + 1);
            else start(id, gbTr("Эффекты"), beat, ln, clip40(s), QStringLiteral("fx"));
        } else if (cmd == QLatin1String("clearfx") || cmd == QLatin1String("stopdream")) {
            for (const QString& id : open.keys())
                if (id.startsWith(QLatin1String("fx:"))) close(id, beat + 1);
            one(QStringLiteral("fx"), gbTr("Эффекты"), beat, ln, clip40(s), QStringLiteral("moment"));
        } else if (!isCommandName(cmd) && s.contains(QLatin1Char(':'))) {
            one(QStringLiteral("say"), gbTr("Реплики"), beat, ln, clip40(s), QStringLiteral("say"));
        } else if (cmd == QLatin1String("say") || cmd == QLatin1String("narration") || cmd == QLatin1String("note") ||
                   cmd == QLatin1String("monologue") || cmd == QLatin1String("punchedsay") || cmd == QLatin1String("voicedsay") ||
                   cmd == QLatin1String("floatingthought") || cmd == QLatin1String("diary") || cmd == QLatin1String("bigtext") ||
                   cmd == QLatin1String("memorynote") || cmd == QLatin1String("extend") || cmd == QLatin1String("sms")) {
            one(QStringLiteral("say"), gbTr("Реплики"), beat, ln, clip40(s), QStringLiteral("say"));
        } else {
            one(QStringLiteral("fx"), gbTr("Эффекты"), beat, ln, clip40(s), QStringLiteral("moment"));
        }
        ++beat;
    }
    for (const QString& id : open.keys()) close(id, beat);
    // characters under the background, in the order they came
    QVector<TimelineTrack> ordered;
    for (const TimelineTrack& t : tl.tracks) if (t.id == QLatin1String("bg")) ordered << t;
    for (const TimelineTrack& t : tl.tracks) if (t.id.startsWith(QLatin1String("char:"))) ordered << t;
    for (const TimelineTrack& t : tl.tracks)
        if (t.id != QLatin1String("bg") && !t.id.startsWith(QLatin1String("char:")) && !t.id.startsWith(QLatin1String("fx:"))) ordered << t;
    // the effects that run a while share the effects track
    for (TimelineTrack& t : ordered)
        if (t.id == QLatin1String("fx"))
            for (const TimelineTrack& f : tl.tracks) if (f.id.startsWith(QLatin1String("fx:"))) t.clips += f.clips;
    tl.tracks = ordered;
    return tl;
}

} // namespace gb
