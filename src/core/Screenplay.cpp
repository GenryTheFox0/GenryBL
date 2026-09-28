// GenryBL V1 - «пиши как сценарий»: play lines -> ordinary story commands (see Screenplay.h).
// The words and the ES catalog come from ScreenplayTables.inc (tools/screenplay/gen_tables.py);
// tools/screenplay/scr_proto.py is the Python prototype the tables were tuned on.
#include "Screenplay.h"
#include "Compiler.h"
#include "Overlays.h"
#include "Py.h"
#include "Text.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace gb {
namespace {

struct ScrPair {
    const char* a;
    const char* b;
};
#include "ScreenplayTables.inc"

QString U8(const char* s) { return QString::fromUtf8(s); }

QString norm(const QString& s)
{
    QString t = s.toLower();
    t.replace(QChar(0x0451), QChar(0x0435));   // ё -> е
    return t.trimmed();
}

// "злая." -> "злая", "з.к." stays
QString tokNorm(QString t)
{
    if (t.size() > 1 && t.endsWith(QLatin1Char('.')) && t.indexOf(QLatin1Char('.')) == t.size() - 1) t.chop(1);
    return t;
}

QStringList tokens(const QString& s)
{
    static const QRegularExpression seps(QStringLiteral("[;/]+"));
    static const QRegularExpression tok(QStringLiteral("[a-z0-9\\x{0430}-\\x{044f}][a-z0-9\\x{0430}-\\x{044f}.'\\-]*|,"));
    QString t = norm(s);
    t.replace(seps, QStringLiteral(","));
    QStringList out;
    for (auto it = tok.globalMatch(t); it.hasNext();) out << tokNorm(it.next().captured());
    return out;
}

bool wmatch(const QString& pat, const QString& tok)
{
    return pat.endsWith(QLatin1Char('*')) ? tok.startsWith(pat.left(pat.size() - 1)) : tok == pat;
}

// «АЛИСА», «ОЛЬГА ДМИТРИЕВНА» - but not a lone «Я»
bool isUpperName(const QString& s)
{
    int letters = 0;
    for (const QChar c : s) {
        if (c.isLetter()) {
            ++letters;
            if (c.isLower()) return false;
        }
    }
    return letters >= 2;
}

QString titleCase(const QString& s)
{
    QStringList w = s.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString& x : w) if (!x.isEmpty()) x[0] = x[0].toUpper();
    return w.join(QLatin1Char(' '));
}

struct Entry {
    QStringList words;
    QString cat, val;
    int n = 0, len = 0, exact = 0;
    bool better(const Entry& o) const
    {
        if (n != o.n) return n > o.n;
        if (len != o.len) return len > o.len;
        return exact > o.exact;
    }
};

struct Place {
    QString kind, ext, in;
};

struct Tabs {
    QSet<QString> sprites, bgs, spriteTags, black, stop;
    QStringList blushStems;
    QHash<QString, QSet<QString>> tagAttrs;
    QHash<QString, QStringList> chains, outfitGroups;
    QHash<QString, QString> stronger, weaker, extraSpeakers, canon, timeRu;
    QHash<QString, QPair<QString, QString>> defaultLook;
    QVector<Entry> lex;
    QStringList outfitPriority;
    QVector<QPair<QString, QString>> directions, timeWords;
    QHash<QString, Place> places;
    QVector<QPair<QStringList, QString>> placeWords;
};

const Tabs& TT()
{
    static const Tabs tabs = [] {
        Tabs t;
        for (auto p = kScrSprites; *p; ++p) {
            const QString s = U8(*p);
            t.sprites.insert(s);
            const QStringList w = s.split(QLatin1Char(' '));
            t.spriteTags.insert(w[0]);
            for (int i = 1; i < w.size(); ++i) t.tagAttrs[w[0]].insert(w[i]);
        }
        for (auto p = kScrBgs; *p; ++p) t.bgs.insert(U8(*p));
        for (auto p = kScrChains; p->a; ++p) t.chains.insert(U8(p->a), U8(p->b).split(QLatin1Char(' ')));
        for (auto p = kScrStronger; p->a; ++p) t.stronger.insert(U8(p->a), U8(p->b));
        for (auto p = kScrWeaker; p->a; ++p) t.weaker.insert(U8(p->a), U8(p->b));
        for (auto p = kScrLex; p->a; ++p) {
            Entry e;
            const QString cv = U8(p->a);
            const int c = cv.indexOf(QLatin1Char(':'));
            e.cat = cv.left(c);
            e.val = cv.mid(c + 1);
            for (const QString& w : U8(p->b).split(QLatin1Char(' '), Qt::SkipEmptyParts)) e.words << tokNorm(w);
            e.n = int(e.words.size());
            for (const QString& w : e.words) {
                const bool stem = w.endsWith(QLatin1Char('*'));
                e.len += int(w.size()) - (stem ? 1 : 0);
                if (!stem) ++e.exact;
            }
            t.lex << e;
        }
        for (auto p = kScrOutfitGroups; p->a; ++p) t.outfitGroups.insert(U8(p->a), U8(p->b).split(QLatin1Char(' ')));
        for (auto p = kScrOutfitPriority; *p; ++p) t.outfitPriority << U8(*p);
        for (auto p = kScrDefaultLook; p->a; ++p) {
            const QStringList v = U8(p->b).split(QLatin1Char('|'));
            t.defaultLook.insert(U8(p->a), {v.value(0), v.value(1)});
        }
        for (auto p = kScrDirections; p->a; ++p) t.directions.push_back({U8(p->a), U8(p->b)});
        for (auto p = kScrPlaces; p->a; ++p) {
            const QStringList v = U8(p->b).split(QLatin1Char('|'));
            t.places.insert(U8(p->a), {v.value(0), v.value(1), v.value(2)});
        }
        for (auto p = kScrPlaceWords; p->a; ++p) t.placeWords.push_back({U8(p->a).split(QLatin1Char(' ')), U8(p->b)});
        for (auto p = kScrTimeWords; p->a; ++p) t.timeWords.push_back({U8(p->a), U8(p->b)});
        std::stable_sort(t.timeWords.begin(), t.timeWords.end(),
                         [](const QPair<QString, QString>& a, const QPair<QString, QString>& b) { return a.first.size() > b.first.size(); });
        for (auto p = kScrTimeRu; p->a; ++p) t.timeRu.insert(U8(p->a), U8(p->b));
        for (auto p = kScrBlack; *p; ++p) t.black.insert(U8(*p));
        for (auto p = kScrExtraSpeakers; p->a; ++p) t.extraSpeakers.insert(U8(p->a), U8(p->b));
        for (auto p = kScrCanonName; p->a; ++p) t.canon.insert(U8(p->a), U8(p->b));
        for (auto p = kScrStop; *p; ++p) t.stop.insert(U8(*p));
        for (auto p = kScrBlushStems; *p; ++p) t.blushStems << U8(*p);
        return t;
    }();
    return tabs;
}

const Entry* lexAt(const QStringList& toks, int i)
{
    const Entry* best = nullptr;
    for (const Entry& e : TT().lex) {
        if (i + e.n > toks.size()) continue;
        bool ok = true;
        for (int k = 0; k < e.n && ok; ++k) ok = wmatch(e.words[k], toks[i + k]);
        if (ok && (!best || e.better(*best))) best = &e;
    }
    return best;
}

// ------------------------------------------------------------------ what the parentheses say
struct Mods {
    QString emo, outfit, dist, pos, dir;
    QSet<QString> accAdd, accDel;
    bool enter = false, exit = false, offscreen = false, thought = false, punch = false;
    QStringList unknown, raw, emos, positions;
    QStringList ovs;           // румянец / пот / слёзы / мокрая (Overlays.h kinds)
    bool changesLook() const
    {
        return !emo.isEmpty() || !outfit.isEmpty() || !accAdd.isEmpty() || !accDel.isEmpty() || !dist.isEmpty() ||
               !pos.isEmpty() || enter || !raw.isEmpty() || !ovs.isEmpty();
    }
};

bool allDigits(const QString& s)
{
    for (const QChar c : s) if (!c.isDigit()) return false;
    return !s.isEmpty();
}

Mods parseMods(const QString& text, const QString& tag)
{
    const Tabs& T = TT();
    Mods m;
    const QStringList toks = tokens(text);
    const QSet<QString> attrs = tag.isEmpty() ? QSet<QString>() : T.tagAttrs.value(tag);
    int strength = 0;
    bool neg = false;
    QStringList outfits;
    for (int i = 0; i < toks.size();) {
        const QString& t = toks[i];
        if (t == QLatin1String(",")) { strength = 0; neg = false; ++i; continue; }
        if (attrs.contains(t)) { m.raw << t; ++i; continue; }      // power user: raw ES attributes «(angry swim close)»
        const Entry* e = lexAt(toks, i);
        if (!e) {
            if (!T.stop.contains(t) && !allDigits(t)) m.unknown << t;
            ++i;
            continue;
        }
        int n = e->n;
        const QString& c = e->cat;
        if (c == QLatin1String("strong")) strength = 1;
        else if (c == QLatin1String("weak")) strength = -1;
        else if (c == QLatin1String("neg")) neg = true;
        else if (c == QLatin1String("emo")) {
            if (!neg) {
                QString v = e->val;
                if (strength > 0) v = T.stronger.value(v, v);
                if (strength < 0) v = T.weaker.value(v, v);
                m.emos << v;
                for (const QString& stem : T.blushStems)       // «краснеет»: shy and the cheeks go pink
                    if (toks[i].startsWith(stem) && !m.ovs.contains(QStringLiteral("blush"))) m.ovs << QStringLiteral("blush");
            }
            neg = false;
            strength = 0;
        } else if (c == QLatin1String("ov")) {
            if (!neg && !m.ovs.contains(e->val)) m.ovs << e->val;
            neg = false;
        } else if (c == QLatin1String("outfit")) {
            if (!neg) outfits << e->val;
            neg = false;
        } else if (c == QLatin1String("acc")) {
            (neg ? m.accDel : m.accAdd).insert(e->val);
            neg = false;
        } else if (c == QLatin1String("dist")) m.dist = e->val;
        else if (c == QLatin1String("pos")) m.positions << e->val;
        else if (c == QLatin1String("enter") || c == QLatin1String("exit")) {
            (c == QLatin1String("enter") ? m.enter : m.exit) = true;
            const int j = i + n;
            if (j < toks.size())
                for (const auto& d : T.directions)
                    if (toks[j] == d.first) { m.dir = d.second; ++n; break; }
        } else if (c == QLatin1String("offscreen")) m.offscreen = true;
        else if (c == QLatin1String("thought")) m.thought = true;
        else if (c == QLatin1String("punch")) m.punch = true;
        i += n;
    }
    if (!m.emos.isEmpty()) {
        const bool cry = m.emos.contains(QStringLiteral("cry"));
        const bool smile = m.emos.contains(QStringLiteral("smile")) || m.emos.contains(QStringLiteral("smile2")) ||
                           m.emos.contains(QStringLiteral("happy"));
        m.emo = cry && smile ? QStringLiteral("crysmile") : m.emos.last();
    }
    if (!outfits.isEmpty()) {
        std::stable_sort(outfits.begin(), outfits.end(),
                         [&](const QString& a, const QString& b) { return T.outfitPriority.indexOf(a) < T.outfitPriority.indexOf(b); });
        m.outfit = outfits.first();
    }
    if (!m.positions.isEmpty()) m.pos = m.positions.last();
    if (m.pos.isEmpty() && !m.dir.isEmpty() && m.enter) m.pos = m.dir;
    return m;
}

// ------------------------------------------------------------------ sprites
struct Look {
    QString emo = QStringLiteral("normal"), outfit;
    QSet<QString> acc;
    QString dist = QStringLiteral("normal");
};

bool isOutfit(const QString& a)
{
    static const QSet<QString> o{QStringLiteral("pioneer"), QStringLiteral("pioneer2"), QStringLiteral("swim"),
                                 QStringLiteral("body"),    QStringLiteral("dress"),    QStringLiteral("sport")};
    return o.contains(a);
}
bool isAccessory(const QString& a)
{
    return a == QLatin1String("panama") || a == QLatin1String("glasses") || a == QLatin1String("stethoscope");
}

QString compose(const QString& tag, const QString& emo, const QSet<QString>& acc, const QString& outfit, const QString& dist)
{
    QStringList p{tag, emo};
    if (acc.contains(QStringLiteral("panama")) && tag == QLatin1String("mt")) p << QStringLiteral("panama");
    if (acc.contains(QStringLiteral("glasses"))) p << QStringLiteral("glasses");
    if (acc.contains(QStringLiteral("stethoscope"))) p << QStringLiteral("stethoscope");
    if (!outfit.isEmpty()) p << outfit;
    if (!dist.isEmpty() && dist != QLatin1String("normal")) p << dist;
    return p.join(QLatin1Char(' '));
}
QString compose(const QString& tag, const Look& l) { return compose(tag, l.emo, l.acc, l.outfit, l.dist); }

QString nameOfTag(const QString& tag) { return TT().canon.value(tag, tag); }

QString resolveSprite(const QString& tag, Mods mods, const Look& cur, Look* out, QStringList* info)
{
    const Tabs& T = TT();
    QStringList chain = !mods.emo.isEmpty() ? T.chains.value(mods.emo, {mods.emo}) : QStringList{cur.emo};
    for (const QString& a : mods.raw) {
        if (isOutfit(a)) mods.outfit = a;
        else if (a == QLatin1String("close") || a == QLatin1String("far")) mods.dist = a;
        else if (isAccessory(a)) mods.accAdd.insert(a);
        else chain = {a};
    }
    if (chain.last() != QLatin1String("normal")) chain << QStringLiteral("normal");
    const QSet<QString> acc = (cur.acc + mods.accAdd) - mods.accDel;
    const QString dist = !mods.dist.isEmpty() ? mods.dist : cur.dist;
    const QString want = !mods.outfit.isEmpty() ? mods.outfit : cur.outfit;
    auto group = [&](const QString& o) { return T.outfitGroups.value(o, {o}); };
    QVector<QStringList> groups;
    if (!want.isEmpty()) groups << group(want);
    if (!cur.outfit.isEmpty() && !groups.contains(group(cur.outfit))) groups << group(cur.outfit);
    const QString d0 = T.defaultLook.value(tag).first;
    if (!groups.contains(QStringList{d0}) && !groups.contains(group(d0))) groups << group(d0);
    groups << QStringList{QString()};
    QVector<QSet<QString>> accTries;
    if (acc.isEmpty()) accTries << QSet<QString>();
    else accTries << acc << (acc - QSet<QString>{QStringLiteral("panama")}) << QSet<QString>();
    const QStringList distTries = !dist.isEmpty() && dist != QLatin1String("normal") ? QStringList{dist, QStringLiteral("normal")}
                                                                                      : QStringList{QStringLiteral("normal")};
    for (int gi = 0; gi < groups.size(); ++gi) {
        for (const QString& e : chain)
            for (const QString& o : groups[gi])
                for (const QSet<QString>& a : accTries)
                    for (const QString& d : distTries) {
                        const QString name = compose(tag, e, a, o, d);
                        if (!T.sprites.contains(name)) continue;
                        if (gi > 0 && !mods.outfit.isEmpty())
                            *info << QString::fromUtf8("у %1 нет одежды «%2» — оставил «%3»")
                                         .arg(nameOfTag(tag), mods.outfit, o.isEmpty() ? QStringLiteral("-") : o);
                        else if (!mods.emo.isEmpty() && e != chain.first())
                            *info << QString::fromUtf8("%1: нет «%2» — взял «%3»").arg(nameOfTag(tag), chain.first(), e);
                        out->emo = e;
                        out->outfit = o;
                        out->acc = a;
                        out->dist = d;
                        return name;
                    }
    }
    return {};
}

// ------------------------------------------------------------------ scene headings
struct Heading {
    QString kind, place, placeText, time;
};

bool parseHeading(const QString& line, Heading* h)
{
    const auto ci = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
    static const QRegularExpression scene(QString::fromUtf8("^(?:СЦЕНА|SCENE)\\s*\\d+\\s*[.:)]?\\s*"), ci);
    static const QRegularExpression num(QStringLiteral("^\\d+\\s*[.)]\\s+"));
    static const QRegularExpression kindRe(
        QString::fromUtf8("^(ИНТ\\.?\\s*/\\s*НАТ|НАТ\\.?\\s*/\\s*ИНТ|ИНТ|НАТ|INT\\.?\\s*/\\s*EXT|EXT\\.?\\s*/\\s*INT|INT|EXT|I/E|ЭКСТ)\\.?\\s+(.+)$"), ci);
    static const QRegularExpression dash(QString::fromUtf8("\\s[—–]{1,2}\\s|\\s-{1,2}\\s"));
    static const QRegularExpression splitter(QString::fromUtf8("\\s*[—–]\\s*|\\s+-{1,2}\\s+|\\s*,\\s*|\\.\\s+"));
    QString s = pyStrip(pyStripChars(pyStrip(line), QStringLiteral("*#")));
    s.remove(scene);
    s.remove(num);
    auto m = kindRe.match(s);
    // «Инт.» / «НАТ ПЛЯЖ»: a lower-case kind needs its dot, so «Нат пошёл домой» stays prose
    if (m.hasMatch() && !isUpperName(m.captured(1)) && !s.mid(m.capturedEnd(1)).startsWith(QLatin1Char('.'))) m = QRegularExpressionMatch();
    QString rest;
    if (m.hasMatch()) {
        const QString k = norm(m.captured(1));
        h->kind = k.startsWith(QString::fromUtf8("инт")) || k.startsWith(QLatin1String("int")) || k.startsWith(QLatin1String("i/e"))
                      ? QStringLiteral("int")
                      : QStringLiteral("ext");
        rest = m.captured(2);
    } else {
        if (!isUpperName(s) || !s.contains(dash)) return false;
        h->kind.clear();
        rest = s;
    }
    QStringList parts;
    for (const QString& p : rest.split(splitter)) {
        const QString x = pyStripChars(p, QStringLiteral(" ."));
        if (!x.isEmpty()) parts << x;
    }
    if (parts.isEmpty()) return false;
    QStringList placeParts;
    h->time.clear();
    for (const QString& p : parts) {
        const QString tn = norm(p);
        QString tv;
        for (const auto& w : TT().timeWords)
            if (tn == w.first || tn.startsWith(w.first + QLatin1Char(' '))) { tv = w.second; break; }
        if (!tv.isEmpty() && !placeParts.isEmpty()) h->time = tv;
        else placeParts << p;
    }
    if (!m.hasMatch() && h->time.isEmpty()) return false;
    h->placeText = placeParts.join(QLatin1Char(' '));
    const QStringList toks = tokens(h->placeText);
    int bestN = -1, bestLen = -1;
    h->place.clear();
    for (const auto& pw : TT().placeWords) {
        const QStringList& words = pw.first;
        for (int i = 0; i + words.size() <= toks.size(); ++i) {
            bool ok = true;
            for (int k = 0; k < words.size() && ok; ++k) ok = wmatch(words[k], toks[i + k]);
            if (!ok) continue;
            int len = 0;
            for (const QString& w : words) len += int(w.size()) - (w.endsWith(QLatin1Char('*')) ? 1 : 0);
            if (words.size() > bestN || (words.size() == bestN && len > bestLen)) {
                bestN = int(words.size());
                bestLen = len;
                h->place = pw.second;
            }
        }
    }
    return true;
}

QString bgFor(const QString& base, const QString& time, bool* fallback)
{
    const Tabs& T = TT();
    *fallback = false;
    if (T.bgs.contains(base)) return base;          // timeless: int_mine, semen_room
    QStringList order;
    if (time == QLatin1String("sunset")) order = {QStringLiteral("sunset"), QStringLiteral("day"), QStringLiteral("night")};
    else if (time == QLatin1String("night"))
        order = {QStringLiteral("night"), QStringLiteral("night2"), QStringLiteral("night_without_light"), QStringLiteral("sunset"), QStringLiteral("day")};
    else if (time == QLatin1String("prolog")) order = {QStringLiteral("night"), QStringLiteral("sunset"), QStringLiteral("day")};
    else order = {QStringLiteral("day"), QStringLiteral("sunset"), QStringLiteral("night")};
    for (const QString& t : order) {
        if (T.bgs.contains(base + QLatin1Char('_') + t)) {
            *fallback = t != order.first();
            return base + QLatin1Char('_') + t;
        }
    }
    return {};
}

// ------------------------------------------------------------------ the stage
struct Slot {
    QString pos;
    bool autoPlaced = true;
};

struct Stage {
    QHash<QString, Look> look;
    QHash<QString, Slot> pos;
    QStringList onscreen;
    QString time = QStringLiteral("day");
    bool timeKnown = false;
    QHash<QString, QPair<QStringList, QHash<QString, Slot>>> snap;
    QHash<QString, QString> custom;     // the story's own characters: normalized id / name -> name as declared
};

Look& lookOf(Stage& st, const QString& tag)
{
    auto it = st.look.find(tag);
    if (it == st.look.end()) {
        Look l;
        const auto d = TT().defaultLook.value(tag);
        l.outfit = d.first;
        if (!d.second.isEmpty()) l.acc.insert(d.second);
        it = st.look.insert(tag, l);
    }
    return *it;
}

bool isPos(const QString& w)
{
    static const QSet<QString> p{QStringLiteral("left"),  QStringLiteral("right"), QStringLiteral("center"), QStringLiteral("fleft"),
                                 QStringLiteral("fright"), QStringLiteral("cleft"), QStringLiteral("cright"), QStringLiteral("truecenter")};
    return p.contains(w);
}

QString timeKey(const QString& word)
{
    const QString w = norm(word);
    if (w == QString::fromUtf8("день") || w == QLatin1String("day") || w == QString::fromUtf8("утро") || w == QLatin1String("morning"))
        return QStringLiteral("day");
    if (w == QString::fromUtf8("вечер") || w == QString::fromUtf8("закат") || w == QLatin1String("sunset") || w == QLatin1String("evening"))
        return QStringLiteral("sunset");
    if (w == QString::fromUtf8("ночь") || w == QLatin1String("night")) return QStringLiteral("night");
    if (w == QString::fromUtf8("пролог") || w == QLatin1String("prolog") || w == QLatin1String("prologue")) return QStringLiteral("prolog");
    return {};
}

void snapshot(Stage& st, const QString& label)
{
    const QString l = pyStrip(label);
    if (!l.isEmpty() && !st.snap.contains(l)) st.snap.insert(l, {st.onscreen, st.pos});
}

// an ordinary command line: follow what it does to the stage
void observe(const QString& raw, Stage& st)
{
    const QString s = pyStrip(raw);
    if (s.isEmpty() || s.startsWith(QLatin1Char('#'))) return;
    if (s.startsWith(QLatin1Char(':'))) {
        const QString name = pyStrip(s.mid(1));
        if (st.snap.contains(name)) {
            st.onscreen = st.snap.value(name).first;
            st.pos = st.snap.value(name).second;
        }
        return;
    }
    if (s.startsWith(QLatin1Char('-')) && s.contains(QLatin1String("->"))) {
        snapshot(st, s.section(QLatin1String("->"), 1).section(QLatin1Char('|'), 0, 0));
        return;
    }
    const QStringList w = splitWords(s);
    const QString c = normalizeCommand(w[0]);
    static const QSet<QString> clears{QStringLiteral("bg"),      QStringLiteral("cg"),      QStringLiteral("showbg"),
                                      QStringLiteral("timeskip"), QStringLiteral("hideall"), QStringLiteral("chapter"),
                                      QStringLiteral("videobg")};
    if (clears.contains(c)) {
        st.onscreen.clear();
        st.pos.clear();
        return;
    }
    if (c == QLatin1String("timeofday") && w.size() > 1) {
        const QString t = timeKey(w[1]);
        if (!t.isEmpty()) {
            st.time = t;
            st.timeKnown = true;
        }
        return;
    }
    if (c == QLatin1String("show") && w.size() > 1 && TT().spriteTags.contains(w[1])) {
        const QString tag = w[1];
        QStringList attrs, p;
        for (const QString& x : w.mid(2)) {
            if (isPos(x)) p << x;
            else if (!isEffect(x) && x != QLatin1String("at") && x != QLatin1String("none")) attrs << x;
        }
        Look& lk = lookOf(st, tag);
        if (!attrs.isEmpty()) {
            lk.emo = attrs[0];
            lk.acc.clear();
            lk.outfit.clear();
            lk.dist = QStringLiteral("normal");
            for (const QString& a : attrs.mid(1)) {
                if (isAccessory(a)) lk.acc.insert(a);
                else if (isOutfit(a) && lk.outfit.isEmpty()) lk.outfit = a;
                else if (a == QLatin1String("close") || a == QLatin1String("far")) lk.dist = a;
            }
        }
        if (!p.isEmpty()) st.pos.insert(tag, {p[0], false});
        if (!st.onscreen.contains(tag)) st.onscreen << tag;
        return;
    }
    if ((c == QLatin1String("hide") || c == QLatin1String("exitleft") || c == QLatin1String("exitright")) && w.size() > 1) {
        st.onscreen.removeAll(w[1]);
        st.pos.remove(w[1]);
        return;
    }
    if (c == QLatin1String("jump") && w.size() > 1) snapshot(st, w[1]);
    else if (c == QLatin1String("if") && s.contains(QLatin1String("->"))) snapshot(st, s.section(QLatin1String("->"), 1).section(QLatin1Char('|'), 0, 0));
}

// ------------------------------------------------------------------ the expander
const QVector<QStringList>& slotsFor()
{
    static const QVector<QStringList> s{
        {},
        {QStringLiteral("center")},
        {QStringLiteral("cleft"), QStringLiteral("cright")},
        {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")},
        {QStringLiteral("fleft"), QStringLiteral("cleft"), QStringLiteral("cright"), QStringLiteral("fright")},
    };
    return s;
}

QString exitFx(const QString& dir)
{
    if (dir == QLatin1String("left")) return QStringLiteral("moveoutleft");
    if (dir == QLatin1String("right")) return QStringLiteral("moveoutright");
    return QStringLiteral("dissolve");
}

bool isDash(QChar c) { return c == QChar(0x2014) || c == QChar(0x2013); }

class Expander {
public:
    Stage st;
    QVector<ScreenplayNote>* notes = nullptr;
    int line = -1;

    void learnCharacters(const QStringList& lines)
    {
        for (const QString& raw : lines) {
            const QString s = pyStrip(raw);
            const QString w = firstWord(s);
            if (normalizeCommand(w) != QLatin1String("character")) continue;
            QStringList v = splitWords(pyStrip(s.mid(w.size())));
            if (v.isEmpty()) continue;
            const QString id = v.takeFirst();
            if (!v.isEmpty() && pyIsHexColor(v.last())) v.removeLast();
            const QString name = v.isEmpty() ? id : v.join(QLatin1Char(' '));
            st.custom.insert(norm(id), name);
            st.custom.insert(norm(name), name);
        }
    }

    void note(bool warn, const QString& text)
    {
        if (notes) notes->push_back({line, warn, text});
    }

    // ES tag ("dv"), "@Name" for the story's own character, "" = nobody known
    QString speakerTag(const QString& name) const
    {
        const QString n = norm(name);
        if (n.isEmpty()) return {};
        if (st.custom.contains(n)) return QLatin1Char('@') + st.custom.value(n);
        const auto& sp = speakers();
        const QString low = pyStrip(name).toLower();
        if (sp.contains(low)) return sp.value(low);
        if (sp.contains(n)) return sp.value(n);
        const Tabs& T = TT();
        if (T.extraSpeakers.contains(n)) return T.extraSpeakers.value(n);
        static const QSet<QString> esIds{QStringLiteral("dv"), QStringLiteral("sl"), QStringLiteral("un"), QStringLiteral("us"),
                                         QStringLiteral("mi"), QStringLiteral("mt"), QStringLiteral("el"), QStringLiteral("sh"),
                                         QStringLiteral("mz"), QStringLiteral("uv"), QStringLiteral("cs"), QStringLiteral("me"),
                                         QStringLiteral("pi"), QStringLiteral("th"), QStringLiteral("genry"), QStringLiteral("scar")};
        if (esIds.contains(n)) return n;
        return {};
    }

    // the name written into «Имя: текст»
    QString whoOf(const QString& written, const QString& tag) const
    {
        if (tag.startsWith(QLatin1Char('@'))) return tag.mid(1);
        if (tag.isEmpty()) return isUpperName(written) ? titleCase(written) : written;
        const QString low = pyStrip(written).toLower();
        if (!isUpperName(written) && (speakers().contains(low) || speakers().contains(norm(written)))) return written;
        return TT().canon.value(tag, written);
    }

    // «— Привет, — улыбнулась Славя. — Ты новенький?» -> "Славя (улыбнулась): Привет. Ты новенький?"
    QString dashToHead(const QString& s) const
    {
        static const QRegularExpression sep(QString::fromUtf8("\\s+[—–]\\s*"));
        static const QRegularExpression wordRe(QStringLiteral("[\\w\\-]+"), QRegularExpression::UseUnicodePropertiesOption);
        if (s.isEmpty() || !isDash(s[0])) return {};
        const QStringList segs = pyStrip(s.mid(1)).split(sep);
        if (segs.size() < 2) return {};
        const QString attr = pyStrip(segs[1]);
        QStringList words;
        for (auto it = wordRe.globalMatch(attr); it.hasNext();) words << it.next().captured();
        int at = -1, len = 0;
        QString tag;
        for (int i = 0; i < words.size() && at < 0; ++i) {
            for (int k = 2; k >= 1; --k) {
                if (i + k > words.size()) continue;
                const QString cand = QStringList(words.mid(i, k)).join(QLatin1Char(' '));
                if (!cand[0].isUpper() && norm(cand) != QString::fromUtf8("я")) continue;
                const QString t = speakerTag(cand);
                if (!t.isEmpty()) { at = i; len = k; tag = t; break; }
            }
        }
        if (at < 0) return {};
        QString name = QStringList(words.mid(at, len)).join(QLatin1Char(' '));
        if (name == QString::fromUtf8("я")) name = QString::fromUtf8("Я");
        const QStringList rawMods = words.mid(0, at) + words.mid(at + len);
        // only the words the lexicon knows: «хмыкнула Алиса, не поднимая головы» -> (хмыкнула)
        QStringList low, modWords;
        for (const QString& w : rawMods) low << tokNorm(norm(w));
        for (int i = 0; i < low.size();) {
            const Entry* e = lexAt(low, i);
            if (!e) { ++i; continue; }
            if (e->cat == QLatin1String("neg") || e->cat == QLatin1String("strong") || e->cat == QLatin1String("weak")) {
                const Entry* next = i + e->n < low.size() ? lexAt(low, i + e->n) : nullptr;
                if (!next || next->cat == QLatin1String("ignore") || next->cat == QLatin1String("neg")) { i += e->n; continue; }
            }
            for (int k = 0; k < e->n; ++k) modWords << rawMods[i + k];
            i += e->n;
        }
        QString text = pyStrip(segs[0]);
        if (text.endsWith(QLatin1Char(','))) text = text.left(text.size() - 1) + QLatin1Char('.');
        for (int i = 2; i < segs.size(); ++i) {
            const QString more = pyStrip(segs[i]);
            if (!more.isEmpty()) text += QLatin1Char(' ') + more;
        }
        const QString mods = modWords.join(QLatin1Char(' '));
        return mods.isEmpty() ? name + QStringLiteral(": ") + text : name + QStringLiteral(" (") + mods + QStringLiteral("): ") + text;
    }

    static bool looksLikeProse(const QString& s)
    {
        if (s.isEmpty()) return false;
        const QChar c = s[0];
        if (!(c.isUpper() || c == QChar(0x00AB) || c == QLatin1Char('"') || c == QChar(0x2026) || c == QChar(0x201E) ||
              c == QChar(0x201C)))
            return false;
        if (isCommandName(normalizeCommand(firstWord(s)))) return false;
        const int colon = int(s.indexOf(QLatin1Char(':')));
        if (colon >= 0 && splitWords(s.left(colon)).size() <= 4) return false;   // «Имя: текст»
        return true;
    }

    bool inBlock = false;     // «менюмода … конецменюмода», «выбор … конецвыбора»: their lines are not the play

    // one line -> commands; false = not a play line (the caller keeps it as it is)
    bool expand(const QString& raw, QStringList* out)
    {
        const Tabs& T = TT();
        const QString s = pyStrip(raw);
        out->clear();
        const QString cmd = normalizeCommand(firstWord(s));
        if (inBlock || cmd == QLatin1String("modmenu") || cmd == QLatin1String("choice")) {
            inBlock = cmd != QLatin1String("endmodmenu") && cmd != QLatin1String("endchoice");
            observe(s, st);
            return false;
        }
        if (s.isEmpty() || s.startsWith(QLatin1Char(':')) || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@')) ||
            s.startsWith(QLatin1Char('-'))) {
            observe(s, st);
            return false;
        }
        if (T.black.contains(pyStripChars(norm(s), QStringLiteral(" .!")))) {
            *out << QStringLiteral("фон black fade");
            observe(out->first(), st);
            return true;
        }
        Heading h;
        if (parseHeading(s, &h)) {
            if (h.place.isEmpty()) {
                note(true, QString::fromUtf8("место «%1» не знаю — фон не сменится").arg(h.placeText));
                *out << QStringLiteral("# ") + s;
                return true;
            }
            const Place pl = T.places.value(h.place);
            const QString kind = !h.kind.isEmpty() ? h.kind : pl.kind;
            const QString base = kind == QLatin1String("int") ? (!pl.in.isEmpty() ? pl.in : pl.ext) : (!pl.ext.isEmpty() ? pl.ext : pl.in);
            const QString t = h.time.isEmpty() || h.time == QLatin1String("same") ? st.time : h.time;
            bool fb = false;
            const QString bg = bgFor(base, t, &fb);
            if (bg.isEmpty()) {
                note(true, QString::fromUtf8("для «%1» нет фона в БЛ").arg(h.placeText));
                *out << QStringLiteral("# ") + s;
                return true;
            }
            if (fb) note(false, QString::fromUtf8("у «%1» нет времени «%2» — взят %3").arg(h.placeText, T.timeRu.value(t), bg));
            if (!st.timeKnown || t != st.time) *out << QStringLiteral("время ") + T.timeRu.value(t);
            *out << QStringLiteral("фон %1 fade").arg(bg);
            for (const QString& l : *out) observe(l, st);
            return true;
        }
        if (isCommandName(normalizeCommand(firstWord(s)))) {
            observe(s, st);
            return false;
        }
        if (isDash(s[0])) {
            const QString head = dashToHead(s);
            if (!head.isEmpty()) {
                if (expandHead(head, out)) return true;
                *out << head;
                return true;
            }
            *out << QStringLiteral("текст ") + s;
            return true;
        }
        if (s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')'))) return direction(s, out);
        if (expandHead(s, out)) return true;
        if (looksLikeProse(s)) {
            *out << QStringLiteral("текст ") + s;
            return true;
        }
        observe(s, st);
        return false;
    }

    // «(Алиса уходит)», «(Пауза)»
    bool direction(const QString& s, QStringList* out)
    {
        const QString inner = pyStrip(s.mid(1, s.size() - 2));
        const QStringList w = splitWords(inner);
        for (int k = qMin(2, int(w.size())); k >= 1; --k) {
            QString cand = QStringList(w.mid(0, k)).join(QLatin1Char(' '));
            cand = pyStripChars(cand, QStringLiteral(",.:;!?"));
            if (cand.isEmpty() || speakerTag(cand).isEmpty()) continue;
            const QString rest = pyStrip(inner.mid(inner.indexOf(cand) + cand.size()));
            if (expandHead(cand + QStringLiteral(" (") + pyStripChars(rest, QStringLiteral(",.;")) + QLatin1Char(')'), out)) return true;
        }
        const QString n = pyStripChars(norm(inner), QStringLiteral(" .!"));
        if (n == QString::fromUtf8("пауза") || n == QString::fromUtf8("молчание") || n == QString::fromUtf8("тишина")) {
            *out << QString::fromUtf8("пауза 1");
            return true;
        }
        *out << QStringLiteral("# ") + s;
        return true;
    }

    bool expandHead(const QString& s, QStringList* out)
    {
        static const QRegularExpression headRe(
            QString::fromUtf8("^(?<name>[^():|#@\\-—–«\"\\s][^():|]{0,60}?)\\s*\\((?<mods>[^()]*)\\)\\s*(?:(?<colon>:)\\s*(?<text>.*?)|\\.)?\\s*$"),
            QRegularExpression::UseUnicodePropertiesOption);
        static const QRegularExpression andRe(QString::fromUtf8("\\s+и\\s+|\\s*,\\s*"));
        const Tabs& T = TT();
        const auto m = headRe.match(s);
        if (!m.hasMatch()) return false;
        const QString name = pyStrip(m.captured(QStringLiteral("name")));
        const QString modText = m.captured(QStringLiteral("mods"));
        const bool colon = !m.captured(QStringLiteral("colon")).isEmpty();
        const QString text = pyStrip(m.captured(QStringLiteral("text")));
        const QStringList names = name.split(andRe, Qt::SkipEmptyParts);
        QStringList tags;
        bool allKnown = true;
        for (const QString& x : names) {
            tags << speakerTag(x);
            if (tags.last().isEmpty()) allKnown = false;
        }
        if (!allKnown) {
            // «Незнакомка (злая): Кто здесь?» - a stranger: the line, without the parentheses
            if (!colon || splitWords(name).size() > 3 || !name[0].isUpper()) return false;
            if (!text.isEmpty()) *out << whoOf(name, QString()) + QStringLiteral(": ") + text;
            note(false, QString::fromUtf8("«%1» — не персонаж БЛ, спрайта не будет").arg(name));
            return true;
        }
        QStringList after, info;
        QStringList unknown;
        for (int k = 0; k < names.size(); ++k) {
            const QString& tag = tags[k];
            const bool sprite = T.spriteTags.contains(tag);
            Mods mods = parseMods(modText, sprite ? tag : QString());
            for (const QString& u : mods.unknown) if (!unknown.contains(u)) unknown << u;
            if (!sprite || mods.offscreen || mods.thought) {
                if (!sprite && (!mods.emo.isEmpty() || !mods.outfit.isEmpty() || !mods.pos.isEmpty()))
                    info << QString::fromUtf8("%1: спрайта в БЛ нет — только реплика").arg(names[k]);
                continue;
            }
            const bool shown = st.onscreen.contains(tag);
            if (mods.exit && mods.emo.isEmpty() && mods.outfit.isEmpty() && mods.dist.isEmpty()) {
                after << QStringLiteral("убрать %1 %2").arg(tag, exitFx(mods.dir));
                if (!shown) info << QString::fromUtf8("%1 уходит, но на экране %1 не было").arg(names[k]);
                continue;
            }
            if (!mods.changesLook() && shown) continue;
            Look newLook;
            QString img = resolveSprite(tag, mods, lookOf(st, tag), &newLook, &info);
            if (img.isEmpty()) {
                note(true, QString::fromUtf8("нет подходящего спрайта для %1").arg(names[k]));
                continue;
            }
            // she cries but the game has no crying face for her: the tears are drawn on
            QStringList ovs = mods.ovs;
            if (mods.emo == QLatin1String("cry") && !newLook.emo.startsWith(QLatin1String("cry")) && !ovs.contains(QStringLiteral("tears")))
                ovs << QStringLiteral("tears");
            for (const QString& kind : overlayKinds())
                if (ovs.contains(kind)) img += QLatin1Char(' ') + overlayWord(kind);
            QString pos = mods.pos;
            QString fx = shown ? QStringLiteral("dspr") : QStringLiteral("dissolve");
            if (mods.enter && !shown) {
                if (mods.dir == QLatin1String("left")) fx = QStringLiteral("moveinleft");
                else if (mods.dir == QLatin1String("right")) fx = QStringLiteral("moveinright");
            }
            if (!shown && pos.isEmpty()) {        // auto layout: re-slot the ones placed automatically
                QStringList autos, fixed;
                for (const QString& t : st.onscreen) {
                    const Slot sl = st.pos.value(t, Slot{});
                    if (sl.autoPlaced) autos << t;
                    else fixed << sl.pos;
                }
                const int n = qMin(int(autos.size()) + 1, 4);
                QStringList slotList;
                for (const QString& x : slotsFor()[n]) if (!fixed.contains(x)) slotList << x;
                if (slotList.isEmpty()) slotList << QStringLiteral("center");
                for (int i = 0; i < autos.size() && i < slotList.size(); ++i) {
                    if (st.pos.value(autos[i], Slot{}).pos != slotList[i]) {
                        *out << QStringLiteral("показать %1 %2").arg(compose(autos[i], lookOf(st, autos[i])), slotList[i]);
                        st.pos.insert(autos[i], {slotList[i], true});
                    }
                }
                pos = autos.size() < slotList.size() ? slotList[autos.size()] : slotList.last();
                *out << QStringLiteral("показать %1 %2 %3").arg(img, pos, fx);
                st.pos.insert(tag, {pos, true});
            } else {
                *out << QStringLiteral("показать %1%2 %3").arg(img, pos.isEmpty() ? QString() : QLatin1Char(' ') + pos, fx);
                if (!pos.isEmpty()) st.pos.insert(tag, {pos, false});
            }
            st.look.insert(tag, newLook);
            if (!st.onscreen.contains(tag)) st.onscreen << tag;
            if (mods.exit) after << QStringLiteral("убрать %1 %2").arg(tag, exitFx(mods.dir));
        }
        if (!unknown.isEmpty()) note(true, QString::fromUtf8("в скобках не понял: %1").arg(unknown.join(QStringLiteral(", "))));
        for (const QString& i : info) note(false, i);
        if (colon && !text.isEmpty()) {
            const QString tag0 = tags.size() == 1 ? tags[0] : QString();
            const QString who = tags.size() == 1 ? whoOf(name, tag0) : (isUpperName(name) ? titleCase(name) : name);
            const Mods all = parseMods(modText, QString());
            if (all.thought && tag0 == QLatin1String("me")) *out << QStringLiteral("th: ") + text;
            else if (all.thought) *out << who + QStringLiteral(": {i}") + text + QStringLiteral("{/i}");
            else if (all.punch) *out << QString::fromUtf8("крик %1 | hpunch | %2").arg(who, text);
            else *out << who + QStringLiteral(": ") + text;
        }
        for (const QString& l : after) {
            const QString tag = l.section(QLatin1Char(' '), 1, 1);
            st.onscreen.removeAll(tag);
            st.pos.remove(tag);
        }
        *out += after;
        return !out->isEmpty() || colon;
    }
};

// a paragraph of prose -> narration boxes of up to ~260 characters, cut between sentences
QStringList splitSentences(const QString& text, int maxLen = 260)
{
    if (text.size() <= maxLen) return {text};
    static const QRegularExpression cut(QString::fromUtf8("(?<=[.!?…])\\s+(?=[\\p{Lu}«\"—(])"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    const QStringList sentences = text.split(cut, Qt::SkipEmptyParts);
    QStringList out;
    QString cur;
    for (const QString& s : sentences) {
        if (!cur.isEmpty() && cur.size() + 1 + s.size() > maxLen) {
            out << cur;
            cur.clear();
        }
        cur += (cur.isEmpty() ? QString() : QStringLiteral(" ")) + s;
    }
    if (!cur.isEmpty()) out << cur;
    return out;
}

} // namespace

QString bgTimeOf(const QString& bg)
{
    const QString b = bg.trimmed().toLower();
    if (b.endsWith(QLatin1String("_night_without_light")) || b.endsWith(QLatin1String("_night2")) || b.endsWith(QLatin1String("_night")))
        return QStringLiteral("night");
    if (b.endsWith(QLatin1String("_sunset"))) return QStringLiteral("sunset");
    if (b.endsWith(QLatin1String("_day"))) return QStringLiteral("day");
    return {};
}

QString bgAtTime(const QString& bg, const QString& time)
{
    static const QRegularExpression suffix(QStringLiteral("_(day|sunset|night|night2|night_without_light)$"));
    QString base = bg.trimmed();
    base.remove(suffix);
    bool fallback = false;
    const QString r = bgFor(base, time, &fallback);
    return fallback ? QString() : r;
}

QStringList expandScreenplay(const QStringList& lines, QVector<int>* srcOf, QVector<ScreenplayNote>* notes)
{
    Expander ex;
    ex.notes = notes;
    ex.learnCharacters(lines);
    QStringList out, one;
    if (srcOf) srcOf->clear();
    for (int i = 0; i < lines.size(); ++i) {
        ex.line = i;
        if (ex.expand(lines[i], &one)) {
            for (const QString& l : one) {
                out << l;
                if (srcOf) *srcOf << i;
            }
        } else {
            out << lines[i];
            if (srcOf) *srcOf << i;
        }
    }
    return out;
}

QStringList expandScreenplayLine(const QStringList& lines, int index, QVector<ScreenplayNote>* notes)
{
    Expander ex;
    ex.learnCharacters(lines);
    QStringList one;
    for (int i = 0; i < index && i < lines.size(); ++i) {
        ex.line = i;
        ex.expand(lines[i], &one);
    }
    if (index < 0 || index >= lines.size()) return {};
    ex.notes = notes;
    ex.line = index;
    return ex.expand(lines[index], &one) ? one : QStringList{};
}

QString screenplayKind(const QString& line)
{
    const QString s = pyStrip(line);
    if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char(':')) || s.startsWith(QLatin1Char('@')) ||
        s.startsWith(QLatin1Char('-')))
        return {};
    Heading h;
    if (TT().black.contains(pyStripChars(norm(s), QStringLiteral(" .!"))) || parseHeading(s, &h)) return QStringLiteral("heading");
    if (isCommandName(normalizeCommand(firstWord(s)))) return {};
    if (isDash(s[0])) return QStringLiteral("say");
    if (s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')'))) return QStringLiteral("direction");
    static const QRegularExpression head(QString::fromUtf8("^[^():|#@\\-—–«\"\\s][^():|]{0,60}?\\s*\\([^()]*\\)\\s*(?::.*|\\.)?\\s*$"));
    if (head.match(s).hasMatch()) return s.contains(QLatin1Char(':')) ? QStringLiteral("say") : QStringLiteral("direction");
    if (Expander::looksLikeProse(s)) return QStringLiteral("prose");
    return {};
}

QStringList convertToStory(const QString& text, bool expand, QVector<ScreenplayNote>* notes)
{
    static const QRegularExpression stamp(QStringLiteral("^\\[?\\d{1,2}:\\d{2}(?::\\d{2})?\\]?\\s+"));
    static const QRegularExpression cueRe(QString::fromUtf8("^([A-ZА-ЯЁ][A-ZА-ЯЁ .'\\-]{0,40}?)\\s*(?:\\(([^()]*)\\))?\\s*$"));
    static const QRegularExpression nameDash(QString::fromUtf8("^([^:—–\\-]{1,40}?)\\s+[—–\\-]\\s+(.+)$"));
    const QStringList src = pySplitLines(stripBom(text));
    Expander ex;
    ex.notes = notes;
    QStringList out;
    QString cueName, cueMods, para;
    QStringList cueText;
    QString last, prev;              // the two who spoke last: unattributed «— реплика» alternates between them
    bool firstPerson = false;        // the prose says «я»: the one who answers an unattributed line is the hero
    static const QRegularExpression iWord(QString::fromUtf8("(^|[^\\w])[Яя]([^\\w]|$)"), QRegularExpression::UseUnicodePropertiesOption);
    auto spoke = [&](const QString& who) {
        if (who == last) return;
        prev = last;
        last = who;
    };
    auto flushCue = [&] {
        if (cueName.isEmpty()) return;
        if (!cueText.isEmpty())
            out << cueName + (cueMods.isEmpty() ? QString() : QStringLiteral(" (") + cueMods + QLatin1Char(')')) + QStringLiteral(": ") +
                       cueText.join(QLatin1Char(' '));
        else if (!cueMods.isEmpty()) out << cueName + QStringLiteral(" (") + cueMods + QLatin1Char(')');
        spoke(cueName);
        cueName.clear();
        cueMods.clear();
        cueText.clear();
    };
    auto flushPara = [&] {
        if (para.isEmpty()) return;
        for (const QString& chunk : splitSentences(para))
            out << (Expander::looksLikeProse(chunk) ? chunk : QStringLiteral("текст ") + chunk);
        para.clear();
    };
    auto isCue = [&](const QString& s, int i) -> bool {
        const auto m = cueRe.match(s);
        if (!m.hasMatch() || !isUpperName(m.captured(1)) || splitWords(m.captured(1)).size() > 3) return false;
        Heading h;
        if (parseHeading(s, &h) || TT().black.contains(pyStripChars(norm(s), QStringLiteral(" .!")))) return false;
        if (!ex.speakerTag(pyStrip(m.captured(1))).isEmpty()) return true;
        const QString next = i + 1 < src.size() ? pyStrip(src[i + 1]) : QString();
        return !next.isEmpty() && !cueRe.match(next).hasMatch();
    };
    for (int i = 0; i < src.size(); ++i) {
        ex.line = int(out.size());
        QString s = pyStrip(src[i]);
        s.remove(stamp);
        if (s.isEmpty()) {
            flushCue();
            flushPara();
            continue;
        }
        if (!cueName.isEmpty()) {
            if (s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')')) && cueText.isEmpty()) {
                cueMods += (cueMods.isEmpty() ? QString() : QStringLiteral(", ")) + pyStrip(s.mid(1, s.size() - 2));
                continue;
            }
            Heading h;
            if (!isCue(s, i) && !parseHeading(s, &h)) {
                cueText << s;
                continue;
            }
            flushCue();
        }
        Heading h;
        if (parseHeading(s, &h) || TT().black.contains(pyStripChars(norm(s), QStringLiteral(" .!")))) {
            flushPara();
            out << s;
            continue;
        }
        if (isCue(s, i)) {
            flushPara();
            const auto m = cueRe.match(s);
            const QString nm = pyStrip(m.captured(1));
            cueName = ex.whoOf(nm, ex.speakerTag(nm));
            cueMods = pyStrip(m.captured(2));
            continue;
        }
        if (isDash(s[0])) {
            flushPara();
            const QString head = ex.dashToHead(s);
            if (!head.isEmpty()) {
                out << head;
                spoke(head.section(QLatin1Char(':'), 0, 0).section(QStringLiteral(" ("), 0, 0));
            } else if (!prev.isEmpty() || (firstPerson && !last.isEmpty() && last != QString::fromUtf8("Я"))) {
                const QString who = !prev.isEmpty() ? prev : QString::fromUtf8("Я");
                out << who + QStringLiteral(": ") + pyStrip(s.mid(1));
                ex.note(false, QString::fromUtf8("реплика без автора — отдал %1 (по очереди)").arg(who));
                spoke(who);
            } else {
                out << QStringLiteral("текст ") + s;
            }
            continue;
        }
        if (s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')'))) {
            flushPara();
            out << s;
            continue;
        }
        if (isCommandName(normalizeCommand(firstWord(s)))) {   // a half-made story pasted back: keep its commands
            flushPara();
            out << s;
            continue;
        }
        const int colon = int(s.indexOf(QLatin1Char(':')));
        if (colon > 0 && splitWords(s.left(colon)).size() <= 4 && !pyStrip(s.mid(colon + 1)).isEmpty()) {
            QString nm = pyStrip(s.left(colon));
            const QString bare = pyStrip(nm.section(QLatin1Char('('), 0, 0));
            const QString tag = ex.speakerTag(bare);
            if (!tag.isEmpty() || bare[0].isUpper()) {
                flushPara();
                const QString who = ex.whoOf(bare, tag);
                nm = who + nm.mid(bare.size());
                out << nm + QStringLiteral(": ") + pyStrip(s.mid(colon + 1));
                spoke(who);
                continue;
            }
        }
        const auto nd = nameDash.match(s);
        if (nd.hasMatch() && !ex.speakerTag(pyStrip(nd.captured(1))).isEmpty()) {
            flushPara();
            const QString who = ex.whoOf(pyStrip(nd.captured(1)), ex.speakerTag(pyStrip(nd.captured(1))));
            out << who + QStringLiteral(": ") + pyStrip(nd.captured(2));
            spoke(who);
            continue;
        }
        if (s.contains(iWord)) firstPerson = true;
        para += (para.isEmpty() ? QString() : QStringLiteral(" ")) + s;
        static const QRegularExpression ends(QString::fromUtf8("[.!?…»\"):]$"));
        if (ends.match(s).hasMatch()) flushPara();
    }
    flushCue();
    flushPara();
    if (!expand) return out;
    QVector<ScreenplayNote> more;
    const QStringList full = expandScreenplay(out, nullptr, notes ? &more : nullptr);
    if (notes) *notes += more;
    return full;
}

} // namespace gb
