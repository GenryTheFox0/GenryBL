// GenryBL V1 - «кино-режим» (see Cinema.h).
#include "Cinema.h"
#include "Compiler.h"
#include "EsAssets.h"
#include "Py.h"
#include "Screenplay.h"
#include "Text.h"

#include <QRegularExpression>

namespace gb {

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

QStringList fieldsOf(const QString& r)
{
    QStringList f;
    for (const QString& x : r.split(QLatin1Char('|'))) f << pyStrip(x);
    return f;
}

bool isSayLine(const QString& s, const QString& cmd)
{
    return s.contains(QLatin1Char(':')) && !isCommandName(cmd) && !s.toLower().startsWith(QLatin1String("http"));
}

// a tiny Python-ish condition evaluator for «если любовь >= 2 and not ссора -> сцена»
struct Expr {
    QStringList t;
    int i = 0;
    const QHash<QString, double>* vars = nullptr;
    QString prefix;
    QString peek() const { return i < t.size() ? t[i] : QString(); }
    QString take() { return i < t.size() ? t[i++] : QString(); }
    double orE()
    {
        double v = andE();
        while (peek() == QLatin1String("or")) { take(); const double r = andE(); v = (v != 0 || r != 0) ? 1 : 0; }
        return v;
    }
    double andE()
    {
        double v = notE();
        while (peek() == QLatin1String("and")) { take(); const double r = notE(); v = (v != 0 && r != 0) ? 1 : 0; }
        return v;
    }
    double notE()
    {
        if (peek() == QLatin1String("not")) { take(); return notE() != 0 ? 0 : 1; }
        return cmpE();
    }
    double cmpE()
    {
        const double a = sumE();
        const QString op = peek();
        static const QStringList ops{QStringLiteral("=="), QStringLiteral("!="), QStringLiteral(">="), QStringLiteral("<="), QStringLiteral(">"),
                                     QStringLiteral("<"), QStringLiteral("=")};
        if (!ops.contains(op)) return a;
        take();
        const double b = sumE();
        if (op == QLatin1String("==") || op == QLatin1String("=")) return a == b;
        if (op == QLatin1String("!=")) return a != b;
        if (op == QLatin1String(">=")) return a >= b;
        if (op == QLatin1String("<=")) return a <= b;
        if (op == QLatin1String(">")) return a > b;
        return a < b;
    }
    double sumE()
    {
        double v = unE();
        for (;;) {
            if (peek() == QLatin1String("+")) { take(); v += unE(); }
            else if (peek() == QLatin1String("-")) { take(); v -= unE(); }
            else return v;
        }
    }
    double unE()
    {
        if (peek() == QLatin1String("-")) { take(); return -unE(); }
        return atom();
    }
    double atom()
    {
        const QString x = take();
        if (x == QLatin1String("(")) { const double v = orE(); if (peek() == QLatin1String(")")) take(); return v; }
        if (x == QLatin1String("True")) return 1;
        if (x == QLatin1String("False") || x == QLatin1String("None") || x.isEmpty()) return 0;
        if (x.startsWith(QLatin1Char('"')) || x.startsWith(QLatin1Char('\''))) return x.size() > 2 ? 1 : 0;
        bool ok = false;
        const double n = x.toDouble(&ok);
        if (ok) return n;
        return vars ? vars->value(prefix + slug(x, QStringLiteral("value"), false), 0) : 0;
    }
};

QStringList tokens(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("\\s*(>=|<=|==|!=|[()<>=+\\-]|\"[^\"]*\"|'[^']*'|[^\\s()<>=!+\\-]+)"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    QStringList out;
    for (auto it = re.globalMatch(s); it.hasNext();) out << it.next().captured(1);
    return out;
}

}   // namespace

QString Cinema::label(const QString& scene) const { return sceneLabel(m_modId, pyStrip(scene)); }

double Cinema::eval(const QString& expr) const
{
    Expr e;
    e.t = tokens(expr);
    e.vars = &m_vars;
    return e.orE();
}

void Cinema::load(const QString& storyText, const EsAssets* es)
{
    m_es = es;
    const QStringList src = pySplitLines(stripBom(storyText));
    m_modId = parseMeta(src, nullptr).modId;
    m_srcOf.clear();
    m_lines = expandScreenplay(src, &m_srcOf);
    m_labels.clear();
    m_prefix.clear();
    m_vars.clear();
    for (int i = 0; i < m_lines.size(); ++i) {
        const QString s = pyStrip(m_lines[i]);
        const QString first = firstWord(s), cmd = normalizeCommand(first), rest = pyStrip(s.mid(first.size()));
        if (s.startsWith(QLatin1Char(':'))) m_labels.insert(label(s.mid(1)), i);
        else if (cmd == QLatin1String("label")) m_labels.insert(label(rest), i);
        else if (cmd == QLatin1String("character") || cmd == QLatin1String("meter")) m_prefix << m_lines[i];
        // «переменная x 5» / «шкала x | … | мин» are the mod's defaults
        if (cmd == QLatin1String("variable")) {
            const QStringList w = pySplit(rest, 1);
            if (!w.isEmpty()) m_vars.insert(slug(w[0], QStringLiteral("value"), false), eval(w.value(1)));
        } else if (cmd == QLatin1String("meter")) {
            const QStringList f = fieldsOf(rest);
            if (!f.value(0).isEmpty()) m_vars.insert(slug(f[0], QStringLiteral("value"), false), qMax(0.0, f.value(3).toDouble()));
        }
    }
}

CinemaStop Cinema::stop(CinemaStop::Kind k, int idx)
{
    CinemaStop st;
    st.kind = k;
    st.line = idx >= 0 && idx < m_srcOf.size() ? m_srcOf[idx] + 1 : 0;
    st.scene = sceneAt((m_prefix + m_path).join(QLatin1Char('\n')), -1, m_es);
    st.music = m_music;
    st.ambience = m_ambience;
    st.sounds = m_sounds;
    st.popups = m_popups;
    st.moment = m_moment;
    m_sounds.clear();
    m_popups.clear();
    m_moment.clear();
    ++m_steps;
    return st;
}

bool Cinema::jumpTo(const QString& scene, QString* note)
{
    const auto it = m_labels.constFind(label(scene));
    if (it == m_labels.constEnd()) {
        if (note) *note = U("Переход в сцену «%1», а её в истории нет — в игре тут мод закончится").arg(pyStrip(scene));
        return false;
    }
    m_pc = *it + 1;
    m_scene = pyStrip(scene);
    return true;
}

CinemaStop Cinema::start(int line)
{
    m_path.clear();
    m_calls.clear();
    m_items.clear();
    m_music.clear();
    m_ambience.clear();
    m_sounds.clear();
    m_popups.clear();
    m_moment.clear();
    m_scene.clear();
    m_ended = false;
    m_steps = 0;
    m_targets.clear();
    m_resume = -1;
    m_fromMenu = false;
    // before the first scene: the mod starts from the top (its own main menu first, like in the game)
    int firstScene = -1;
    for (int i = 0; i < m_lines.size() && firstScene < 0; ++i)
        if (pyStrip(m_lines[i]).startsWith(QLatin1Char(':')) || normalizeCommand(firstWord(pyStrip(m_lines[i]))) == QLatin1String("label")) firstScene = i;
    int at = 0;
    while (at < m_lines.size() && m_srcOf[at] < line - 1) ++at;
    if (firstScene < 0 || at <= firstScene) {
        for (int i = 0; i < m_lines.size(); ++i) {
            if (normalizeCommand(firstWord(pyStrip(m_lines[i]))) != QLatin1String("modmenu")) continue;
            // «менюмода»: the frame of the menu, its buttons that lead to scenes
            QStringList block;
            QStringList options;
            m_targets.clear();
            for (int k = i; k < m_lines.size(); ++k) {
                const QString t = pyStrip(m_lines[k]);
                const QString c = normalizeCommand(firstWord(t));
                if (k > i && (c == QLatin1String("endmodmenu") || c == QLatin1String("endchoice"))) break;
                block << m_lines[k];
                const QString w = firstWord(t).toLower(), rest = pyStrip(t.mid(firstWord(t).size()));
                if (w != U("кнопка") && w != QLatin1String("button")) continue;
                const QString cap = pyStrip(rest.section(QStringLiteral("->"), 0, 0));
                const QString target = rest.contains(QLatin1String("->")) ? pyStrip(rest.section(QStringLiteral("->"), 1)) : QString();
                const QString key = (target.isEmpty() ? cap : target).toLower();
                static const QStringList special{U("галерея"), QStringLiteral("gallery"), U("достижения"), QStringLiteral("achievements"), U("шкалы"),
                                                 U("отношения"), QStringLiteral("meters"), U("главы"), QStringLiteral("chapters"), U("настройки"),
                                                 QStringLiteral("settings"), U("загрузить"), U("продолжить"), QStringLiteral("load"), U("выход"),
                                                 U("выйти"), QStringLiteral("exit")};
                if (special.contains(key)) continue;
                options << cap;
                m_targets << (target.isEmpty() ? QStringLiteral("start") : target);
            }
            if (options.isEmpty()) {
                options << U("Начать");
                m_targets << QStringLiteral("start");
            }
            m_path = block;
            for (const QString& b : block) absorb(pyStrip(b));      // the menu's music
            CinemaStop st = stop(CinemaStop::Choice, i);
            m_path.clear();
            st.options = options;
            for (const QString& t : m_targets) st.optionOk << m_labels.contains(label(t));
            m_timeoutSet = false;
            m_resume = firstScene < 0 ? int(m_lines.size()) : firstScene;
            m_fromMenu = true;
            return st;
        }
        m_pc = 0;
        return run();
    }
    // from the cursor: everything above it happened (like the preview), then the real route
    for (int i = 0; i < at; ++i) {
        const QString s = pyStrip(m_lines[i]);
        const QString cmd = normalizeCommand(firstWord(s));
        if (s.startsWith(QLatin1Char(':'))) m_scene = pyStrip(s.mid(1));
        else if (cmd == QLatin1String("label")) m_scene = pyStrip(s.mid(firstWord(s).size()));
        absorb(s);
        m_path << m_lines[i];
    }
    // a choice block the cursor sits in: start at the block
    for (int i = at - 1; i >= 0; --i) {
        const QString c = normalizeCommand(firstWord(pyStrip(m_lines[i])));
        if (c == QLatin1String("endchoice") || c == QLatin1String("endmodmenu")) break;
        if (c == QLatin1String("choice")) { at = i; m_path = m_path.mid(0, i); break; }
    }
    m_popups.clear();
    m_sounds.clear();
    m_moment.clear();
    m_pc = at;
    return run();
}

CinemaStop Cinema::next(int option)
{
    if (m_ended) {
        CinemaStop st = stop(CinemaStop::End, m_pc - 1);
        st.note = U("Конец");
        return st;
    }
    if (m_resume >= 0) {          // a choice is waiting for its answer
        const int resume = m_resume;
        m_resume = -1;
        QString target;
        bool go = false;
        if (option >= 0 && option < m_targets.size()) {
            target = m_targets[option];
            go = !target.isEmpty();
        } else if (option < 0 && m_timeoutSet) {
            target = m_timeoutTarget;
            go = !target.isEmpty();
        }
        m_targets.clear();
        m_timeoutSet = false;
        if (m_fromMenu) {         // «stop music fadeout 1.5»: the route plays its own
            m_fromMenu = false;
            m_music.clear();
        }
        if (go) {
            QString note;
            if (!jumpTo(target, &note)) {
                m_ended = true;
                CinemaStop st = stop(CinemaStop::End, resume - 1);
                st.note = note;
                return st;
            }
        } else {
            m_pc = resume;
        }
    }
    return run();
}

void Cinema::absorb(const QString& s)
{
    if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@')) || s.startsWith(QLatin1Char(':'))) return;
    const QString first = firstWord(s);
    const QString cmd = normalizeCommand(first);
    const QString rest = pyStrip(s.mid(first.size()));
    const QStringList w = pySplit(rest);
    auto audio = [&](const QString& word, const QMap<QString, QString>& table) -> QString {
        if (word.isEmpty()) return {};
        if (word.startsWith(QLatin1String("es:"))) return word.mid(3);
        if (word.contains(QLatin1Char('/')) || word.contains(QLatin1Char('.'))) return word;     // the project's audio/x.ogg or a game path
        return table.value(word);
    };
    static const QMap<QString, QString> none;
    if (cmd == QLatin1String("setvar") && w.size() >= 2) {
        m_vars.insert(slug(w[0], QStringLiteral("value"), false), eval(pyStrip(pySplit(rest, 1).value(1))));
    } else if (cmd == QLatin1String("addvar") && w.size() >= 2) {
        const QString v = slug(w[0], QStringLiteral("value"), false);
        m_vars.insert(v, m_vars.value(v) + eval(w[1]));
        m_popups << U("Очки|%1 %2%3").arg(w[0], w[1].startsWith(QLatin1Char('-')) ? QString() : QStringLiteral("+"), w[1]);
    } else if ((cmd == QLatin1String("persistentvar") || cmd == QLatin1String("persistentadd")) && w.size() >= 2) {
        const QString v = QStringLiteral("p:") + slug(w[0], QStringLiteral("value"), false);
        const double d = eval(pyStrip(pySplit(rest, 1).value(1)));
        m_vars.insert(v, cmd == QLatin1String("persistentadd") ? m_vars.value(v) + d : d);
    } else if (cmd == QLatin1String("remember")) {
        const QStringList f = fieldsOf(rest);
        m_popups << U("Запомнят|") + (f.size() > 1 && !f[1].isEmpty() ? f[1] : (f.value(0).isEmpty() ? U("Это запомнят.") : f[0] + U(" это запомнит.")));
        for (const QString& x : f) {
            const QString k = x.section(QLatin1Char('='), 0, 0).trimmed().toLower();
            if (x.contains(QLatin1Char('=')) && (k == U("флаг") || k == QLatin1String("flag")))
                m_vars.insert(slug(x.section(QLatin1Char('='), 1).trimmed(), QStringLiteral("value"), false), 1);
        }
    } else if (cmd == QLatin1String("itemget")) {
        const QStringList f = fieldsOf(rest);
        if (!f.value(0).isEmpty()) {
            m_items.insert(slug(f[0], QStringLiteral("item"), false));
            m_popups << U("Инвентарь|Предмет: ") + (f.value(1).isEmpty() ? f[0] : f[1]);
        }
    } else if (cmd == QLatin1String("itemremove")) {
        m_items.remove(slug(fieldsOf(rest).value(0), QStringLiteral("item"), false));
    } else if (cmd == QLatin1String("notify")) {
        const QStringList f = fieldsOf(rest);
        m_popups << f.value(1, QStringLiteral("Genry")) + QLatin1Char('|') + f.value(0);
    } else if (cmd == QLatin1String("unlockachievement")) {
        const QStringList f = fieldsOf(rest);
        m_popups << U("Достижение|") + (f.value(1).isEmpty() ? f.value(0) : f[1]);
    } else if (cmd == QLatin1String("replayunlock")) {
        const QStringList f = fieldsOf(rest);
        m_popups << U("Реплей|Сцена открыта: ") + (f.value(1).isEmpty() ? f.value(0) : f[1]);
    } else if (cmd == QLatin1String("music")) {
        QString id;
        for (int k = 0; k < w.size() && id.isEmpty(); ++k) {
            const QString x = w[k].toLower();
            if (x == QLatin1String("fadein") || x == QLatin1String("fadeout")) { ++k; continue; }
            if (x == QLatin1String("loop") || x == QLatin1String("noloop")) continue;
            id = w[k];
        }
        m_music = audio(id, m_es ? m_es->music : none);
    } else if (cmd == QLatin1String("musicfile")) {
        m_music = audio(w.value(0), none);
    } else if (cmd == QLatin1String("stopmusic")) {
        m_music.clear();
    } else if (cmd == QLatin1String("stopallaudio")) {
        m_music.clear();
        m_ambience.clear();
    } else if (cmd == QLatin1String("ambience")) {
        m_ambience = audio(w.value(0), m_es ? m_es->ambience : none);
    } else if (cmd == QLatin1String("stopambience")) {
        m_ambience.clear();
    } else if (cmd == QLatin1String("sound") || cmd == QLatin1String("soundfile") || cmd == QLatin1String("voice") || cmd == QLatin1String("voicefile")) {
        const QString a = audio(w.value(0), m_es ? m_es->sounds : none);
        if (!a.isEmpty()) m_sounds << a;
    } else if (cmd == QLatin1String("shake")) {
        m_moment = QStringLiteral("shake");
    } else if (cmd == QLatin1String("flash")) {
        m_moment = QStringLiteral("flash");
    } else if (cmd == QLatin1String("pixelfx")) {
        m_moment = QStringLiteral("pixels");
    } else if (cmd == QLatin1String("eyesblink")) {
        m_moment = QStringLiteral("blink");
    }
}

CinemaStop Cinema::run()
{
    auto finish = [&](int idx, const QString& note) {
        m_ended = true;
        CinemaStop st = stop(CinemaStop::End, idx);
        st.note = note;
        return st;
    };
    int guard = 0;
    while (m_pc < m_lines.size()) {
        if (++guard > 200000) return finish(m_pc - 1, U("История ходит по кругу без единой реплики — проверь переходы"));
        const int i = m_pc++;
        const QString raw = m_lines[i];
        const QString s = pyStrip(raw);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@'))) continue;
        const QString first = firstWord(s);
        const QString cmd = normalizeCommand(first);
        const QString rest = pyStrip(s.mid(first.size()));

        // walking into the next scene: the one we were in is over (the game returns, unless it is <scene>_next)
        if (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) {
            const QString name = s.startsWith(QLatin1Char(':')) ? pyStrip(s.mid(1)) : rest;
            if (m_scene.isEmpty() || label(name) == label(m_scene) + QStringLiteral("_next")) {
                m_scene = name;
                m_path << raw;
                continue;
            }
            if (!m_calls.isEmpty()) {
                const auto c = m_calls.takeLast();
                m_pc = c.first;
                m_scene = c.second;
                continue;
            }
            return finish(i - 1, U("Сцена «%1» закончилась без перехода — в игре мод тут завершается").arg(m_scene));
        }
        if (cmd == QLatin1String("modmenu")) {           // the mod's own menu is not a part of the route
            while (m_pc < m_lines.size()) {
                const QString c = normalizeCommand(firstWord(pyStrip(m_lines[m_pc++])));
                if (c == QLatin1String("endmodmenu") || c == QLatin1String("endchoice")) break;
            }
            continue;
        }
        if (cmd == QLatin1String("choice")) {
            QStringList options, block{raw};
            m_targets.clear();
            m_timeoutTarget.clear();
            m_timeoutSet = false;
            int k = i + 1;
            for (; k < m_lines.size(); ++k) {
                const QString t = pyStrip(m_lines[k]);
                block << m_lines[k];
                if (normalizeCommand(firstWord(t)) == QLatin1String("endchoice")) break;
                if (t.startsWith(QLatin1Char('-')) && t.contains(QLatin1String("->"))) {
                    options << pyStrip(t.mid(1).section(QStringLiteral("->"), 0, 0));
                    m_targets << pyStrip(t.section(QStringLiteral("->"), 1).section(QLatin1Char('|'), 0, 0));
                } else if (t.contains(QLatin1String("->")) && isTimeoutWord(t.section(QStringLiteral("->"), 0, 0))) {
                    m_timeoutTarget = pyStrip(t.section(QStringLiteral("->"), 1));
                    m_timeoutSet = true;
                }
            }
            m_path << block;
            m_resume = qMin(k + 1, int(m_lines.size()));
            CinemaStop st = stop(CinemaStop::Choice, i);
            st.options = options;
            for (const QString& t : m_targets) st.optionOk << m_labels.contains(label(t));
            if (choiceStyleOf(rest) == QLatin1String("timed")) {
                st.seconds = choiceSeconds(rest).toDouble();
                m_timeoutSet = true;                      // no «время вышло»: the story just goes on
            }
            return st;
        }
        if (isSayLine(s, cmd) || cmd == QLatin1String("say") || cmd == QLatin1String("voicedsay") || cmd == QLatin1String("punchedsay")) {
            m_path << raw;
            if (cmd == QLatin1String("voicedsay")) {
                const QStringList p = fieldsOf(rest);
                const QString v = p.size() >= 3 ? p[1] : pySplit(rest).value(1);
                if (!v.isEmpty()) m_sounds << (v.contains(QLatin1Char('/')) || v.contains(QLatin1Char('.')) ? v : (m_es ? m_es->sounds.value(v) : QString()));
                m_sounds.removeAll(QString());
            }
            if (cmd == QLatin1String("punchedsay")) m_moment = QStringLiteral("shake");
            return stop(CinemaStop::Say, i);
        }
        if (cmd == QLatin1String("note") || cmd == QLatin1String("monologue") || cmd == QLatin1String("diary") || cmd == QLatin1String("memorynote") ||
            cmd == QLatin1String("bigtext")) {
            m_path << raw;
            return stop(CinemaStop::Note, i);
        }
        // screens the game calls and waits on until the player closes them
        {
            const QString a = pySplit(rest).value(0).toLower();
            const bool buttonOrHide = a == U("кнопка") || a == QLatin1String("button") || a == U("скрыть") || a == U("убрать") || a == QLatin1String("hide");
            if (cmd == QLatin1String("achievements") || cmd == QLatin1String("gallery") || cmd == QLatin1String("memories") ||
                cmd == QLatin1String("chapters") || cmd == QLatin1String("musicplayer") || cmd == QLatin1String("phonefeed") ||
                cmd == QLatin1String("phonehome") || ((cmd == QLatin1String("meters") || cmd == QLatin1String("inventory")) && !buttonOrHide)) {
                m_path << raw;
                return stop(CinemaStop::Note, i);
            }
        }
        if (cmd == QLatin1String("timeskip") || cmd == QLatin1String("chapter") || cmd == QLatin1String("chapterpng") || cmd == QLatin1String("titlecard") ||
            cmd == QLatin1String("splitflap") || cmd == QLatin1String("creditsroll") || cmd == QLatin1String("newchapter")) {
            m_path << raw;
            CinemaStop st = stop(CinemaStop::Card, i);
            if (cmd == QLatin1String("newchapter")) {
                st.scene.cardKind = QStringLiteral("title");
                st.scene.cardText = pyStrip(rest.section(QLatin1Char('|'), 0, 0));
            }
            st.seconds = cmd == QLatin1String("creditsroll") ? 7.0 : 2.6;
            return st;
        }
        if (cmd == QLatin1String("video")) {
            m_path << raw;
            CinemaStop st = stop(CinemaStop::Video, i);
            st.video = pyStrip(rest.section(QLatin1Char('|'), 0, 0));
            return st;
        }
        if (cmd == QLatin1String("pause")) {
            m_path << raw;
            CinemaStop st = stop(CinemaStop::Timed, i);
            double sec = 1.0;
            pyFloat(rest, &sec);
            st.seconds = qBound(0.1, sec, 30.0);
            return st;
        }
        if (cmd == QLatin1String("sms") || cmd == QLatin1String("phonephoto") || cmd == QLatin1String("phonevoice") || cmd == QLatin1String("phonepost") ||
            cmd == QLatin1String("phonepush") || cmd == QLatin1String("phonecomment")) {
            m_path << raw;
            CinemaStop st = stop(CinemaStop::Timed, i);
            st.seconds = 1.4;
            return st;
        }
        if (cmd == QLatin1String("phonecall")) {
            // «звонок Славя | принять -> a | сбросить -> b | нет ответа -> c | ждать=10»
            m_path << raw;
            QString accept, decline;
            double wait = 0;
            m_timeoutTarget.clear();
            for (const QString& x : fieldsOf(rest).mid(1)) {
                if (x.contains(QLatin1String("->"))) {
                    const QString k = pyStrip(x.section(QStringLiteral("->"), 0, 0)).toLower(), t = pyStrip(x.section(QStringLiteral("->"), 1));
                    if (k.startsWith(U("прин")) || k.startsWith(U("ответ")) || k == QLatin1String("accept")) accept = t;
                    else if (k.startsWith(U("сброс")) || k.startsWith(U("откл")) || k == QLatin1String("decline")) decline = t;
                    else m_timeoutTarget = t;
                } else if (x.contains(QLatin1Char('='))) {
                    const QString k = pyStrip(x.section(QLatin1Char('='), 0, 0)).toLower();
                    if (k == U("ждать") || k == U("время") || k == QLatin1String("wait")) pyFloat(pyStrip(x.section(QLatin1Char('='), 1)), &wait);
                }
            }
            m_targets = {accept, decline};
            m_timeoutSet = true;
            m_resume = i + 1;
            if (m_es && m_es->sounds.contains(QStringLiteral("sfx_home_phone_ring"))) m_sounds << m_es->sounds.value(QStringLiteral("sfx_home_phone_ring"));
            CinemaStop st = stop(CinemaStop::Choice, i);
            st.options = {U("Принять"), U("Сбросить")};
            st.optionOk = {accept.isEmpty() || m_labels.contains(label(accept)), decline.isEmpty() || m_labels.contains(label(decline))};
            st.seconds = wait;
            return st;
        }
        if (cmd == QLatin1String("map") || cmd == QLatin1String("screenmenu")) {
            m_path << raw;
            QStringList options;
            m_targets.clear();
            if (cmd == QLatin1String("map")) {
                for (QString e : rest.split(QLatin1Char(','))) {
                    e = e.trimmed();
                    const int atSign = int(e.lastIndexOf(QLatin1Char('@')));
                    if (atSign >= 0) e = e.left(atSign).trimmed();
                    const int sep = e.contains(QLatin1Char(':')) ? int(e.indexOf(QLatin1Char(':'))) : int(e.indexOf(QLatin1String("->")));
                    if (sep < 0) continue;
                    const QString zone = e.left(sep).trimmed(), target = e.mid(sep + (e.at(sep) == QLatin1Char(':') ? 1 : 2)).trimmed();
                    QString title = zone;
                    for (const EsMapZone& z : esMapZones()) if (z.id == zone) title = z.title;
                    options << title;
                    m_targets << target;
                }
            } else {
                for (const QString& it : fieldsOf(rest).mid(1))
                    if (it.contains(QLatin1String("->"))) {
                        options << pyStrip(it.section(QStringLiteral("->"), 0, 0));
                        m_targets << pyStrip(it.section(QStringLiteral("->"), 1));
                    }
                if (options.isEmpty()) { options << U("Продолжить"); m_targets << QString(); }
            }
            m_timeoutSet = false;
            m_resume = i + 1;
            CinemaStop st = stop(CinemaStop::Choice, i);
            st.options = options;
            for (const QString& t : m_targets) st.optionOk << (t.isEmpty() || m_labels.contains(label(t)));
            return st;
        }
        if (cmd == QLatin1String("jump") || cmd == QLatin1String("callscene")) {
            m_path << raw;
            if (cmd == QLatin1String("callscene")) m_calls.push_back({m_pc, m_scene});
            QString note;
            if (!jumpTo(rest, &note)) {
                if (cmd == QLatin1String("callscene")) m_calls.removeLast();
                return finish(i, note);
            }
            continue;
        }
        if (cmd == QLatin1String("return")) {
            m_path << raw;
            if (!m_calls.isEmpty()) {
                const auto c = m_calls.takeLast();
                m_pc = c.first;
                m_scene = c.second;
                continue;
            }
            return finish(i, U("«конецсцены»: в игре тут мод завершается"));
        }
        if (cmd == QLatin1String("endgame")) {
            m_path << raw;
            m_music.clear();
            m_ambience.clear();
            return finish(i, U("Конец мода"));
        }
        if ((cmd == QLatin1String("ifjump") || cmd == QLatin1String("ifitem") || cmd == QLatin1String("ifpersistent")) && rest.contains(QLatin1String("->"))) {
            m_path << raw;
            const QString cond = pyStrip(rest.section(QStringLiteral("->"), 0, 0)), target = pyStrip(rest.section(QStringLiteral("->"), 1));
            bool yes = false;
            if (cmd == QLatin1String("ifitem")) {
                yes = m_items.contains(slug(cond, QStringLiteral("item"), false));
            } else if (cmd == QLatin1String("ifpersistent")) {
                Expr e;
                e.t = tokens(cond);
                e.vars = &m_vars;
                e.prefix = QStringLiteral("p:");
                yes = e.orE() != 0;
            } else {
                yes = eval(cond) != 0;
            }
            if (yes) {
                QString note;
                if (!jumpTo(target, &note)) return finish(i, note);
            }
            continue;
        }
        if (cmd == QLatin1String("bestmeter")) {
            // лучшаяшкала a -> x | b -> y | иначе -> z   (genry_best_of: a tie or nothing above 0 = «иначе»)
            m_path << raw;
            QString best, otherwise;
            double top = 0;
            bool any = false, tie = false;
            for (const QString& part : rest.split(QLatin1Char('|'))) {
                if (!part.contains(QLatin1String("->"))) continue;
                const QString left = pyStrip(part.section(QStringLiteral("->"), 0, 0)), target = pyStrip(part.section(QStringLiteral("->"), 1));
                const QString l = left.toLower();
                if (l == U("иначе") || l == QLatin1String("else") || l == U("ничья")) { otherwise = target; continue; }
                const double v = m_vars.value(slug(left, QStringLiteral("value"), false));
                if (!any || v > top) { best = target; top = v; tie = false; any = true; }
                else if (v == top) tie = true;
            }
            const QString go = (!any || tie || top <= 0) ? otherwise : best;
            if (!go.isEmpty()) {
                QString note;
                if (!jumpTo(go, &note)) return finish(i, note);
            }
            continue;
        }
        absorb(s);
        m_path << raw;
    }
    return finish(m_lines.size() - 1, U("История кончилась"));
}

} // namespace gb
