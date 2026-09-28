#include "Compiler.h"
#include "EsAssets.h"
#include "Py.h"
#include "PhoneAssets.h"
#include "Overlays.h"
#include "Screenplay.h"
#include "Text.h"
#include "Wardrobe.h"
#include "Weather.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace gb {

namespace {

#include "LegacyHeader.inc"
#include "Tables.inc"
#include "V1Screens.inc"
#include "V1Phone.inc"

using SL = QStringList;
const QString I4 = QStringLiteral("    ");
const QString I8 = QStringLiteral("        ");

QString U(const char* s) { return QString::fromUtf8(s); }

struct Tbl {
    QHash<QString, QString> renames, speakers, weatherAliases, filterAliases;
    QSet<QString> effects, positions, commandNames, spritePrefixes, weatherStop, filterStop;
    QHash<QString, double> walk;
    QVector<std::pair<QString, std::pair<QString, QString>>> weather;   // key -> (dot, blossom) in order
    QVector<std::pair<QString, QString>> filters;                       // key -> matrix in order

    Tbl()
    {
        for (int i = 0; kRenames[i].k; ++i) renames.insert(U(kRenames[i].k), U(kRenames[i].v));
        for (int i = 0; kSpeakers[i].k; ++i) speakers.insert(U(kSpeakers[i].k), U(kSpeakers[i].v));
        for (int i = 0; kEffects[i]; ++i) effects.insert(U(kEffects[i]));
        for (int i = 0; kPositions[i]; ++i) positions.insert(U(kPositions[i]));
        for (int i = 0; kCommandNames[i]; ++i) commandNames.insert(U(kCommandNames[i]));
        for (int i = 0; kSpritePrefixes[i]; ++i) spritePrefixes.insert(U(kSpritePrefixes[i]));
        for (int i = 0; kWalkXaligns[i].k; ++i) walk.insert(U(kWalkXaligns[i].k), kWalkXaligns[i].v);
        for (int i = 0; kWeather[i].k; ++i) weather.push_back({U(kWeather[i].k), {U(kWeather[i].dot), U(kWeather[i].blossom)}});
        for (int i = 0; kWeatherAliases[i].k; ++i) weatherAliases.insert(U(kWeatherAliases[i].k), U(kWeatherAliases[i].v));
        for (int i = 0; kWeatherStop[i]; ++i) weatherStop.insert(U(kWeatherStop[i]));
        for (int i = 0; kFilters[i].k; ++i) filters.push_back({U(kFilters[i].k), U(kFilters[i].v)});
        for (int i = 0; kFilterAliases[i].k; ++i) filterAliases.insert(U(kFilterAliases[i].k), U(kFilterAliases[i].v));
        for (int i = 0; kFilterStop[i]; ++i) filterStop.insert(U(kFilterStop[i]));
    }
};

const Tbl& T()
{
    static const Tbl t;
    return t;
}

// ------------------------------------------------------------------ small helpers

struct Ctx {
    const CompileOptions& opt;
    QString modId;
    const QSet<QString>* modVars = nullptr;
    bool modMenu = false;
    QString sl(const QString& s, const char* fb = "label") const { return slugOf(s, U(fb), opt); }
    // V1: every scene label is <mod>__<scene>, so two mods with ": finale" can live side by side
    // (Ren'Py refuses duplicate labels and would take the whole game down). ": start" = the mod itself,
    // or <mod>__start when the mod opens with its own main menu.
    QString lab(const QString& name) const
    {
        const QString x = sl(name);
        if (opt.legacy || modId.isEmpty()) return x;
        if (x == QLatin1String("start")) return modMenu ? modId + QStringLiteral("__start") : slugOf(modId, QStringLiteral("genry_mod"), opt);
        return modId + QStringLiteral("__") + x;
    }
    // V1: the mod's own system names (meter list, inventory, gallery): <prefix>__<name>
    QString sys(const QString& name) const
    {
        return (modId.startsWith(QLatin1String("genry")) ? modId : QStringLiteral("genry_") + modId) + QStringLiteral("__") + name;
    }
    // V1: the mod's own variables (установить/прибавить/переменная) are <mod>_<name> + default,
    // so "прибавить trust 1" never hits a NameError and mods don't share "trust"
    QString var(const QString& name) const
    {
        const QString x = sl(name, "value");
        if (opt.legacy || modId.isEmpty()) return x;
        return (modId.startsWith(QLatin1String("genry")) ? modId : QStringLiteral("genry_") + modId) + QLatin1Char('_') + x;
    }
    bool isModVar(const QString& slugged) const { return modVars && modVars->contains(slugged); }
    // legacy: the old split that drops empty items (shifts every later field);
    // V1: positions stay where the writer put them ("Текст | Заголовок | | #143247 | справа")
    SL pipes(const QString& rest) const;
};

QString arg(const QString& f, const QString& a) { return f.arg(a); }

QString F2(double v) { return pyFixed(v, 2); }

SL words(const QString& s) { return pySplit(s); }

QString joinW(const SL& w) { return w.join(QLatin1Char(' ')); }

QString repairStoryLine(const QString& line, const CompileOptions& opt)
{
    if (!opt.legacy) return line;      // V1: "pioneer2" is a real ES outfit (dv_*_pioneer2.png), keep it
    static const QRegularExpression re(QStringLiteral("\\bpioneer2\\b"), QRegularExpression::UseUnicodePropertiesOption);
    QString s = line;
    s.replace(re, QStringLiteral("pioneer"));
    return s;
}

QString cleanNumber(const QString& value, const QString& fallback)
{
    const QString v = pyStrip(value);
    return pyIsNumber(v) ? v : fallback;
}

QString cleanMinNumber(const QString& value, const QString& fallback, double minimum)
{
    const QString c = cleanNumber(value, fallback);
    double d;
    if (!pyFloat(c, &d)) return fallback;
    return d < minimum ? fallback : c;
}

QString cleanAlpha(const QString& value, const QString& fallback = QStringLiteral("0.60"))
{
    const QString c = cleanNumber(value.isEmpty() ? fallback : value, fallback);
    double a;
    if (!pyFloat(c, &a)) pyFloat(fallback, &a);
    a = qBound(0.0, a, 1.0);
    return F2(a);
}

QString cleanInteger(const QString& value, const QString& fallback)
{
    static const QRegularExpression re(QStringLiteral("^-?[0-9]+$"));
    QString v = pyStrip(value);
    QString t = v;
    if (t.endsWith(QLatin1Char('\n'))) t.chop(1);
    return re.match(t).hasMatch() ? v : fallback;
}

QString cleanFloatRange(const QString& value, const QString& fallback, double minimum, double maximum)
{
    const QString v = cleanNumber(value, fallback);
    double n;
    if (!pyFloat(v, &n)) return fallback;
    if (n < minimum) return pyRepr(minimum);
    if (n > maximum) return pyRepr(maximum);
    return v;
}

QString imageAlias(const QString& image, const QString& prefix, const Ctx& c)
{
    QString base = image;
    base.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_')).replace(QLatin1Char('.'), QLatin1Char('_'));
    return prefix + QLatin1Char('_') + c.sl(base, "sprite").left(64);
}

SL splitPipeItems(const QString& rest);

SL Ctx::pipes(const QString& rest) const
{
    if (opt.legacy) return splitPipeItems(rest);
    SL out;
    for (const QString& item : pySplitSep(rest, QStringLiteral("|"))) out << pyStrip(item);
    while (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out;
}

SL splitPipeItems(const QString& rest)
{
    SL out;
    for (const QString& item : pySplitSep(rest, QStringLiteral("|")))
        if (!pyStrip(item).isEmpty()) out << pyStrip(item);
    return out;
}

QString imageTagName(const QString& image)
{
    const SL w = words(image);
    return w.isEmpty() ? QString() : w.first();
}

bool shouldSpriteTimeTint(const QString& image)
{
    const QString tag = imageTagName(image).toLower();
    if (tag.isEmpty()) return false;
    static const QSet<QString> no{QStringLiteral("black"), QStringLiteral("white"), QStringLiteral("blink"),
                                  QStringLiteral("unblink"), QStringLiteral("prologue_dream")};
    if (no.contains(tag)) return false;
    if (tag.startsWith(QLatin1String("genry_"))) return false;
    return true;
}

QString spriteShowAtClause(const QString& image, const QString& pos)
{
    if (shouldSpriteTimeTint(image))
        return pos.isEmpty() ? QStringLiteral(" at genry_sprite_time_tint") : QStringLiteral(" at %1, genry_sprite_time_tint").arg(pos);
    return pos.isEmpty() ? QString() : QStringLiteral(" at ") + pos;
}

SL tintAtl(const QString& image)
{
    if (shouldSpriteTimeTint(image)) return {QStringLiteral("        matrixcolor genry_sprite_time_matrix()")};
    return {};
}

QString effectZorder() { return QStringLiteral(" zorder 100"); }

bool isNum(const QString& s) { return pyIsNumber(s); }

QString walkF2(const QString& pos, double fallback)
{
    const auto& w = T().walk;
    return F2(w.contains(pos) ? w.value(pos) : fallback);
}

QString stripWrappedQuotes(const QString& text)
{
    const QString t = pyStrip(text);
    if (t.size() >= 2 && t.front() == t.back() && (t.front() == QLatin1Char('"') || t.front() == QLatin1Char('\'')))
        return pyStrip(t.mid(1, t.size() - 2));
    return t;
}

// speaker id for a say line; V1 records unknown names so compileStory can declare them
QString speakerFor(const QString& name, CompileState& st, const CompileOptions& opt)
{
    const QString id = speakerId(name, st, opt);
    if (id.startsWith(QLatin1String("genry_sp_")) && !st.unknownSpeakers.contains(id)) st.unknownSpeakers.insert(id, pyStrip(name));
    return id;
}

// V1: identifiers written in Cyrillic inside a condition get the same transliteration
// as установить/прибавить, otherwise Ren'Py 7 (Python 2) dies on a SyntaxError
QString asciiCondition(const QString& cond, const Ctx& c)
{
    const CompileOptions& opt = c.opt;
    if (opt.legacy) return cond;
    static const QRegularExpression ident(QStringLiteral("[^\\W\\d]\\w*"), QRegularExpression::UseUnicodePropertiesOption);
    QString out;
    int last = 0;
    for (auto it = ident.globalMatch(cond); it.hasNext();) {
        const auto m = it.next();
        out += cond.mid(last, m.capturedStart() - last);
        const QString tok = m.captured();
        bool ascii = true;
        for (const QChar ch : tok) if (ch.unicode() > 127) { ascii = false; break; }
        const QString id = ascii ? tok : slugOf(tok, QStringLiteral("value"), opt);
        out += c.isModVar(id) ? c.var(id) : id;
        last = m.capturedEnd();
    }
    return out + cond.mid(last);
}

void rememberSpriteTag(CompileState& st, const QString& image)
{
    const QString tag = imageTagName(image);
    if (!tag.isEmpty() && T().spritePrefixes.contains(tag) && !st.activeSpriteTags.contains(tag)) st.activeSpriteTags << tag;
}

// ------------------------------------------------------------------ sprite commands

SL compileWalk(const QString& rest)
{
    const SL w = words(rest);
    if (w.size() < 4) return {U("    # walk command needs: image from_position to_position seconds")};
    const QString seconds = cleanNumber(w.last(), QStringLiteral("1.0"));
    QString toPos = w.at(w.size() - 2), fromPos = w.at(w.size() - 3);
    const QString image = pyStrip(joinW(w.mid(0, w.size() - 3)));
    if (image.isEmpty()) return {U("    # walk command needs an image name")};
    if (!isWalkPosition(fromPos)) fromPos = QStringLiteral("left");
    if (!isWalkPosition(toPos)) toPos = QStringLiteral("center");
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign ") + F2(walkXalign(fromPos)), QStringLiteral("        yalign 1.0")};
    r << tintAtl(image);
    r << QStringLiteral("        linear %1 xalign %2").arg(seconds, F2(walkXalign(toPos)));
    return r;
}

SL compileBigShow(const QString& rest)
{
    SL w = words(rest);
    if (w.isEmpty()) return {U("    # bigshow command needs: image [position] [zoom] [effect]")};
    QString effect = QStringLiteral("dissolve");
    if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
    if (w.size() >= 6 && isWalkPosition(w.at(w.size() - 5)) && isWalkPosition(w.at(w.size() - 4)) && isNum(w.at(w.size() - 3)) &&
        isNum(w.at(w.size() - 2)) && isNum(w.last())) {
        const QString zoom = cleanNumber(w.takeLast(), QStringLiteral("1.25"));
        const QString alpha = cleanNumber(w.takeLast(), QStringLiteral("1.0"));
        const QString seconds = cleanNumber(w.takeLast(), QStringLiteral("0.0"));
        const QString toPos = w.takeLast();
        const QString fromPos = w.takeLast();
        const QString image = pyStrip(joinW(w));
        if (image.isEmpty()) return {U("    # bigshow legacy form needs an image name")};
        SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign ") + walkF2(fromPos, 0.50),
             QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom, QStringLiteral("        alpha ") + alpha};
        r << tintAtl(image);
        if (seconds != QLatin1String("0") && seconds != QLatin1String("0.0"))
            r << QStringLiteral("        linear %1 xalign %2").arg(seconds, walkF2(toPos, 0.50));
        r << QStringLiteral("    with ") + effect;
        return r;
    }
    QString zoom = QStringLiteral("1.25");
    if (!w.isEmpty() && isNum(w.last())) zoom = cleanNumber(w.takeLast(), QStringLiteral("1.25"));
    QString pos = QStringLiteral("center");
    if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
    const QString image = pyStrip(joinW(w));
    if (image.isEmpty()) return {U("    # bigshow command needs an image name")};
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign ") + walkF2(pos, 0.50),
         QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom};
    r << tintAtl(image);
    r << QStringLiteral("    with ") + effect;
    return r;
}

SL compileMirrorShow(const QString& rest, const QString& defaultZoom, const Ctx& c)
{
    SL w = words(rest);
    if (w.isEmpty()) return {U("    # mirror command needs: image [position] [zoom] [effect]")};
    QString effect;
    if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
    QString zoom = defaultZoom;
    if (!w.isEmpty() && isNum(w.last())) zoom = cleanNumber(w.takeLast(), defaultZoom);
    QString pos = QStringLiteral("right");
    if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
    const QString image = pyStrip(joinW(w));
    if (image.isEmpty()) return {U("    # mirror command needs an image name")};
    const QString alias = imageAlias(image + QLatin1Char('_') + pos, QStringLiteral("genry_mirror"), c);
    SL r{QStringLiteral("    show %1 as %2:").arg(image, alias), QStringLiteral("        xalign ") + walkF2(pos, 0.82),
         QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom, QStringLiteral("        xzoom -1.0")};
    const SL tint = tintAtl(image);
    for (int i = 0; i < tint.size(); ++i) r.insert(5 + i, tint.at(i));
    if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
    return r;
}

SL compileConflictFocus(const QString& rest, const Ctx& c)
{
    const SL p = c.pipes(rest);
    if (p.size() < 2)
        return {U("    # conflictfocus needs: active image | passive image | [active_pos] | [passive_pos] | [active_zoom] | [passive_zoom] | [effect]")};
    const QString active = p[0], passive = p[1];
    const QString activePos = p.size() > 2 && isWalkPosition(p[2]) ? p[2] : QStringLiteral("left");
    const QString passivePos = p.size() > 3 && isWalkPosition(p[3]) ? p[3] : QStringLiteral("right");
    const QString activeZoom = p.size() > 4 ? cleanNumber(p[4], QStringLiteral("1.12")) : QStringLiteral("1.12");
    const QString passiveZoom = p.size() > 5 ? cleanNumber(p[5], QStringLiteral("0.96")) : QStringLiteral("0.96");
    const QString effect = p.size() > 6 && isEffect(p[6]) ? p[6] : QStringLiteral("dissolve");
    const QString aa = imageAlias(active + QStringLiteral("_active"), QStringLiteral("genry_focus"), c);
    const QString pa = imageAlias(passive + QStringLiteral("_passive"), QStringLiteral("genry_focus"), c);
    static const QSet<QString> rightish{QStringLiteral("right"), QStringLiteral("fright"), QStringLiteral("cright")};
    return {QStringLiteral("    show %1 as %2:").arg(passive, pa),
            QStringLiteral("        xalign ") + walkF2(passivePos, 0.82),
            QStringLiteral("        yalign 1.0"),
            QStringLiteral("        zoom ") + passiveZoom,
            QStringLiteral("        alpha 0.72"),
            QStringLiteral("        xzoom ") + (rightish.contains(passivePos) ? QStringLiteral("-1.0") : QStringLiteral("1.0")),
            QStringLiteral("    show %1 as %2:").arg(active, aa),
            QStringLiteral("        xalign ") + walkF2(activePos, 0.18),
            QStringLiteral("        yalign 1.0"),
            QStringLiteral("        zoom ") + activeZoom,
            QStringLiteral("        alpha 1.0"),
            QStringLiteral("        xzoom ") + (rightish.contains(activePos) ? QStringLiteral("-1.0") : QStringLiteral("1.0")),
            QStringLiteral("    with ") + effect};
}

SL compileFullheight(const QString& rest, const Ctx& c)
{
    const SL p = c.pipes(rest);
    QString image, pos, zoom, mirrorMode, alpha, effect;
    if (!p.isEmpty()) {
        image = p[0];
        pos = p.size() > 1 && isWalkPosition(p[1]) ? p[1] : QStringLiteral("center");
        zoom = p.size() > 2 ? cleanNumber(p[2], QStringLiteral("1.28")) : QStringLiteral("1.28");
        mirrorMode = p.size() > 3 ? p[3].toLower() : QStringLiteral("auto");
        alpha = p.size() > 4 ? cleanNumber(p[4], QStringLiteral("1.0")) : QStringLiteral("1.0");
        effect = p.size() > 5 && isEffect(p[5]) ? p[5] : QStringLiteral("dissolve");
    } else {
        SL w = words(rest);
        if (w.isEmpty()) return {U("    # fullheight needs: image | [position] | [zoom] | [mirror/normal/auto] | [alpha] | [effect]")};
        effect = QStringLiteral("dissolve");
        if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
        alpha = QStringLiteral("1.0");
        if (!w.isEmpty() && isNum(w.last())) alpha = cleanNumber(w.takeLast(), QStringLiteral("1.0"));
        mirrorMode = QStringLiteral("auto");
        static const QSet<QString> modes{QStringLiteral("mirror"), QStringLiteral("flip"), U("зеркало"), QStringLiteral("normal"),
                                         QStringLiteral("no"), QStringLiteral("auto")};
        if (!w.isEmpty() && modes.contains(w.last().toLower())) mirrorMode = w.takeLast().toLower();
        zoom = QStringLiteral("1.28");
        if (!w.isEmpty() && isNum(w.last())) zoom = cleanNumber(w.takeLast(), QStringLiteral("1.28"));
        pos = QStringLiteral("center");
        if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
        image = pyStrip(joinW(w));
    }
    image = pyStrip(image);
    if (image.isEmpty()) return {U("    # fullheight needs an image name")};
    bool mirror = false;
    static const QSet<QString> yes{QStringLiteral("mirror"), QStringLiteral("flip"), U("зеркало"), QStringLiteral("yes"),
                                   QStringLiteral("true"), QStringLiteral("1")};
    if (yes.contains(mirrorMode)) mirror = true;
    else if (mirrorMode == QLatin1String("auto"))
        mirror = pos == QLatin1String("right") || pos == QLatin1String("fright") || pos == QLatin1String("cright");
    const QString alias = imageAlias(image + QStringLiteral("_fullheight_") + pos, QStringLiteral("genry_fullheight"), c);
    SL r{QStringLiteral("    show %1 as %2:").arg(image, alias), QStringLiteral("        xalign ") + walkF2(pos, 0.50),
         QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom,
         QStringLiteral("        alpha ") + cleanNumber(alpha, QStringLiteral("1.0"))};
    r << tintAtl(image);
    r << QStringLiteral("        xzoom ") + (mirror ? QStringLiteral("-1.0") : QStringLiteral("1.0"));
    r << QStringLiteral("    with ") + (effect.isEmpty() ? QStringLiteral("dissolve") : effect);
    return r;
}

SL compileCrowdShow(const QString& rest)
{
    const SL items = splitPipeItems(rest).mid(0, 10);
    if (items.isEmpty()) return {U("    # crowd command needs: image | image | image")};
    const int count = int(items.size());
    const QString zoom = count <= 3 ? QStringLiteral("0.98") : (count <= 5 ? QStringLiteral("0.86") : QStringLiteral("0.72"));
    SL r;
    for (int i = 0; i < count; ++i) {
        const double xalign = double(i + 1) / double(count + 1);
        r << QStringLiteral("    show %1:").arg(items[i]) << QStringLiteral("        xalign ") + F2(xalign)
          << QStringLiteral("        yalign 1.0") << QStringLiteral("        zoom ") + zoom;
        r << tintAtl(items[i]);
    }
    r << QStringLiteral("    with dissolve");
    return r;
}

SL compileEnterShow(const QString& rest, bool left)
{
    SL w = words(rest);
    if (w.size() < 3) return {U("    # enter command needs: image target_position seconds [zoom]")};
    QString zoom = QStringLiteral("1.0");
    if (w.size() >= 4 && isNum(w.last()) && isNum(w.at(w.size() - 2))) zoom = cleanNumber(w.takeLast(), QStringLiteral("1.0"));
    const QString seconds = cleanNumber(w.takeLast(), QStringLiteral("1.0"));
    QString target = w.takeLast();
    if (!isWalkPosition(target)) target = QStringLiteral("center");
    const QString image = pyStrip(joinW(w));
    if (image.isEmpty()) return {U("    # enter command needs an image name")};
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign ") + F2(left ? -0.18 : 1.18),
         QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom};
    r << tintAtl(image);
    r << QStringLiteral("        linear %1 xalign %2").arg(seconds, walkF2(target, 0.50));
    return r;
}

SL compileZoomShow(const QString& rest)
{
    SL w = words(rest);
    if (w.size() < 5) return {U("    # zoom command needs: image seconds zoom xalign yalign")};
    const QString yalign = cleanNumber(w.takeLast(), QStringLiteral("0.50"));
    const QString xalign = cleanNumber(w.takeLast(), QStringLiteral("0.50"));
    const QString zoom = cleanNumber(w.takeLast(), QStringLiteral("1.12"));
    const QString seconds = cleanNumber(w.takeLast(), QStringLiteral("2.0"));
    const QString image = pyStrip(joinW(w));
    if (image.isEmpty()) return {U("    # zoom command needs an image name")};
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign 0.50"), QStringLiteral("        yalign 0.50"),
         QStringLiteral("        zoom 1.0")};
    r << tintAtl(image);
    r << QStringLiteral("        linear %1 zoom %2 xalign %3 yalign %4").arg(seconds, zoom, xalign, yalign);
    return r;
}

SL compileExitShow(const QString& rest, bool left)
{
    SL w = words(rest);
    if (w.size() < 2) return {U("    # exit command needs: image seconds [zoom]")};
    QString zoom = QStringLiteral("1.0");
    if (w.size() >= 3 && isNum(w.last()) && isNum(w.at(w.size() - 2))) zoom = cleanNumber(w.takeLast(), QStringLiteral("1.0"));
    const QString seconds = cleanNumber(w.takeLast(), QStringLiteral("1.0"));
    const QString image = pyStrip(joinW(w));
    if (image.isEmpty()) return {U("    # exit command needs an image name")};
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom};
    r << tintAtl(image);
    r << QStringLiteral("        linear %1 xalign %2").arg(seconds, F2(left ? -0.25 : 1.25));
    r << QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(seconds);
    return r;
}

SL compilePulse(const QString& rest)
{
    SL w = words(rest);
    if (w.isEmpty()) return {U("    # pulse command needs: image [position] [seconds] [zoom]")};
    QString zoom = QStringLiteral("1.08"), seconds = QStringLiteral("0.14");
    if (!w.isEmpty() && isNum(w.last())) zoom = cleanNumber(w.takeLast(), QStringLiteral("1.08"));
    if (!w.isEmpty() && isNum(w.last())) seconds = cleanNumber(w.takeLast(), QStringLiteral("0.14"));
    QString pos = QStringLiteral("center");
    if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
    const QString image = pyStrip(joinW(w));
    if (image.isEmpty()) return {U("    # pulse command needs an image name")};
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign ") + walkF2(pos, 0.50),
         QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom 1.0")};
    r << tintAtl(image);
    r << QStringLiteral("        linear %1 zoom %2").arg(seconds, zoom) << QStringLiteral("        linear %1 zoom 1.0").arg(seconds);
    return r;
}

SL compileGhostMove(const QString& rest)
{
    const SL w = words(rest);
    if (w.size() < 6) return {U("    # ghost command needs: image from_position to_position seconds alpha zoom")};
    const QString zoom = cleanNumber(w.last(), QStringLiteral("1.0"));
    const QString alpha = cleanNumber(w.at(w.size() - 2), QStringLiteral("0.45"));
    const QString seconds = cleanNumber(w.at(w.size() - 3), QStringLiteral("3.0"));
    QString toPos = w.at(w.size() - 4), fromPos = w.at(w.size() - 5);
    const QString image = pyStrip(joinW(w.mid(0, w.size() - 5)));
    if (!isWalkPosition(fromPos)) fromPos = QStringLiteral("left");
    if (!isWalkPosition(toPos)) toPos = QStringLiteral("right");
    if (image.isEmpty()) return {U("    # ghost command needs an image name")};
    SL r{QStringLiteral("    show %1:").arg(image), QStringLiteral("        xalign ") + F2(walkXalign(fromPos)),
         QStringLiteral("        yalign 1.0"), QStringLiteral("        zoom ") + zoom, QStringLiteral("        alpha ") + alpha};
    r << tintAtl(image);
    r << QStringLiteral("        linear %1 xalign %2").arg(seconds, F2(walkXalign(toPos)));
    return r;
}

// ------------------------------------------------------------------ eyes, fx, cards

SL compileEyesClose(const QString& rest)
{
    const QString s = cleanMinNumber(rest.isEmpty() ? QStringLiteral("2.00") : rest, QStringLiteral("2.00"), 0.9);
    return {QStringLiteral("    window hide"), QStringLiteral("    show blink onlayer overlay"), QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(s)};
}

SL compileEyesOpen(const QString& rest)
{
    const QString s = cleanMinNumber(rest.isEmpty() ? QStringLiteral("2.00") : rest, QStringLiteral("2.00"), 0.9);
    return {QStringLiteral("    window hide"), QStringLiteral("    show unblink onlayer overlay"), QStringLiteral("    hide blink onlayer overlay"),
            QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(s), QStringLiteral("    hide unblink onlayer overlay")};
}

SL compileEyesBlink(const QString& rest)
{
    const SL w = words(rest);
    const QString c = !w.isEmpty() ? cleanMinNumber(w[0], QStringLiteral("2.00"), 0.35) : QStringLiteral("2.00");
    const QString o = w.size() > 1 ? cleanMinNumber(w[1], QStringLiteral("2.00"), 0.65) : QStringLiteral("2.00");
    return {QStringLiteral("    window hide"), QStringLiteral("    show blink onlayer overlay"), QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(c),
            QStringLiteral("    show unblink onlayer overlay"), QStringLiteral("    hide blink onlayer overlay"),
            QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(o), QStringLiteral("    hide unblink onlayer overlay")};
}

SL compileFlash(const QString& rest)
{
    const QString s = cleanNumber(rest.isEmpty() ? QStringLiteral("0.18") : rest, QStringLiteral("0.18"));
    return {QStringLiteral("    show genry_flash"), QStringLiteral("    with dissolve"), QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(s),
            QStringLiteral("    hide genry_flash"), QStringLiteral("    with dissolve")};
}

SL compileShake(const QString& rest)
{
    SL w = words(rest.toLower());
    if (!w.isEmpty() && (w[0] == QLatin1String("hpunch") || w[0] == QLatin1String("vpunch"))) return {QStringLiteral("    with ") + w[0]};
    double duration = 0.45;
    if (!w.isEmpty() && isNum(w[0])) {
        double d;
        if (pyFloat(w.takeFirst(), &d)) duration = std::max(0.15, std::min(1.8, d));
    }
    const QString strength = !w.isEmpty() ? w[0] : QStringLiteral("normal");
    SL pattern;
    static const QSet<QString> light{U("слабо"), U("лёгко"), U("легко"), QStringLiteral("light"), QStringLiteral("soft"), QStringLiteral("weak")};
    static const QSet<QString> strong{U("сильно"), U("жёстко"), U("жестко"), QStringLiteral("strong"), QStringLiteral("hard"), QStringLiteral("heavy")};
    static const QSet<QString> vert{U("вертикально"), QStringLiteral("vertical"), QStringLiteral("v"), QStringLiteral("vpunch")};
    if (light.contains(strength)) pattern = {QStringLiteral("hpunch")};
    else if (strong.contains(strength)) pattern = {QStringLiteral("hpunch"), QStringLiteral("vpunch"), QStringLiteral("hpunch")};
    else if (vert.contains(strength)) pattern = {QStringLiteral("vpunch")};
    else pattern = {QStringLiteral("hpunch"), QStringLiteral("vpunch")};
    const int cycles = int(std::max<long long>(1, std::min<long long>(6, pyRound(duration / 0.25))));
    SL r;
    for (int i = 0; i < cycles; ++i)
        for (int e = 0; e < pattern.size(); ++e) {
            r << QStringLiteral("    with ") + pattern[e];
            if (i != cycles - 1 || e != pattern.size() - 1) r << QStringLiteral("    $ renpy.pause(0.035, hard=True)");
        }
    return r;
}

// V1, the eyes shut: closing them IS the transition. The words come over the closed lids themselves (ES's blink
// lives on the overlay layer) - no fade to black, no black screen; the place changes under the lids unseen.
SL overLidsText(const QString& tag, const QString& text, int size, const QString& hold)
{
    return {QStringLiteral("    window hide"),
            QStringLiteral("    show expression Text(%1, size=%2, color=\"#f1ece0\", font=\"fonts/corbel.ttf\", text_align=0.5, "
                           "outlines=[(2, \"#00000099\", 0, 0)]) as %3 onlayer overlay zorder 10:").arg(pyUQ(text)).arg(size).arg(tag),
            QStringLiteral("        xcenter 0.5"), QStringLiteral("        ycenter 0.5"), QStringLiteral("        alpha 0.0"),
            QStringLiteral("        linear 0.8 alpha 1.0"), QStringLiteral("        pause ") + hold, QStringLiteral("        linear 0.7 alpha 0.0"),
            QStringLiteral("    $ renpy.pause(%1)").arg(0.8 + hold.toDouble() + 0.7), QStringLiteral("    hide %1 onlayer overlay").arg(tag)};
}

SL compileTitlecard(const QString& rest, bool v1 = false, bool eyesClosed = false)
{
    QString text = pyStrip(rest);
    if (text.isEmpty()) text = U("Титр");
    if (v1 && eyesClosed) return overLidsText(QStringLiteral("genry_title"), text, 66, QStringLiteral("2.2"));
    if (v1) {
        // V1: a real title card - black, the name alone in the middle in the menu's Corbel; no dialogue box.
        // A click skips it, like any ES pause.
        return {QStringLiteral("    window hide"), QStringLiteral("    show genry_black"), QStringLiteral("    with Dissolve(0.8)"),
                QStringLiteral("    show expression Text(%1, size=66, color=\"#f1ece0\", font=\"fonts/corbel.ttf\", text_align=0.5, "
                               "outlines=[(2, \"#00000099\", 0, 0)]) as genry_title:").arg(pyUQ(text)),
                QStringLiteral("        xcenter 0.5"), QStringLiteral("        ycenter 0.47"), QStringLiteral("        alpha 0.0"),
                QStringLiteral("        linear 1.0 alpha 1.0"), QStringLiteral("    $ renpy.pause(3.0)"), QStringLiteral("    hide genry_title"),
                QStringLiteral("    with Dissolve(0.7)"), QStringLiteral("    hide genry_black"), QStringLiteral("    with Dissolve(0.8)")};
    }
    return {QStringLiteral("    show genry_black"), QStringLiteral("    with dissolve"), QStringLiteral("    window hide"),
            QStringLiteral("    $ renpy.pause(0.2, hard=True)"), I4 + pyQ(QStringLiteral("{size=38}{b}%1{/b}{/size}").arg(text)),
            QStringLiteral("    window show"), QStringLiteral("    hide genry_black"), QStringLiteral("    with dissolve")};
}

SL compileCreditsRoll(const QString& rest, bool eyesClosed = false)
{
    const SL p = splitPipeItems(rest);
    QString text = stripWrappedQuotes(!p.isEmpty() ? p[0] : rest);
    if (text.isEmpty()) text = U("КОНЕЦ");
    text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    QString lit = pyQ(text);
    lit.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    const QString seconds = p.size() > 1 ? cleanNumber(p[1], QStringLiteral("18.0")) : QStringLiteral("18.0");
    const QString size = p.size() > 2 ? cleanNumber(p[2], QStringLiteral("34")) : QStringLiteral("34");
    const QString effect = p.size() > 3 && isEffect(p[3]) ? p[3] : QStringLiteral("dissolve2");
    return {QStringLiteral("    window hide"),
            eyesClosed ? QStringLiteral("    scene black\n    hide blink onlayer overlay") : QStringLiteral("    scene black with ") + effect,
            QStringLiteral("    show expression Text(%1, text_align=0.5, size=%2, color=\"#f5f5f5\") as genry_credits_roll:").arg(lit, size),
            QStringLiteral("        xalign 0.5"), QStringLiteral("        ypos 1.05"), QStringLiteral("        linear %1 ypos -1.20").arg(seconds),
            QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(seconds), QStringLiteral("    hide genry_credits_roll"),
            QStringLiteral("    with dissolve"), QStringLiteral("    window show")};
}

QString noteText(const QString& kind, const QString& rest)
{
    QString text = stripWrappedQuotes(rest);
    if (text.isEmpty()) text = QStringLiteral("...");
    if (kind == QLatin1String("monologue") || kind == QLatin1String("memorynote")) return QStringLiteral("{i}%1{/i}").arg(text);
    if (kind == QLatin1String("diary")) return U("{b}Дневник{/b}\n") + text;
    if (kind == QLatin1String("bigtext")) return QStringLiteral("{size=34}%1{/size}").arg(text);
    return text;
}

SL compileLargeText(const QString& kind, const QString& rest, bool legacy)
{
    const QString text = noteText(kind, rest);
    // V1: a line break stays a line break («дневник» title, «\n» typed by the writer); the old
    // builder wrote a raw newline into the string and Ren'Py folded it into a space
    QString said = pyQ(text);
    if (!legacy) said = pyQ(QString(text).replace(QStringLiteral("\\n"), QStringLiteral("\n"))).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    SL r{QStringLiteral("    window hide"), QStringLiteral("    show genry_note_dim onlayer overlay:"), QStringLiteral("        alpha 0.60"),
         QStringLiteral("    with dissolve")};
    const bool mem = kind == QLatin1String("memorynote");
    if (mem) r << QStringLiteral("    show prologue_dream") + effectZorder() << QStringLiteral("    with dissolve");
    r << QStringLiteral("    $ set_mode_nvl()") << QStringLiteral("    nvl show") << I4 + said << QStringLiteral("    nvl clear")
      << QStringLiteral("    nvl hide") << QStringLiteral("    $ set_mode_adv()");
    if (mem) r << QStringLiteral("    hide prologue_dream") << QStringLiteral("    with dissolve");
    r << QStringLiteral("    hide genry_note_dim onlayer overlay") << QStringLiteral("    with dissolve") << QStringLiteral("    window show");
    return r;
}

SL compileNoteBg(const QString& rest)
{
    SL w = words(rest);
    if (w.isEmpty()) return {U("    # notebg needs: bg/cg/image name")};
    const QString kind = w.takeFirst().toLower();
    QString effect;
    if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
    QString image = pyStrip(joinW(w));
    QString line;
    if (kind == QLatin1String("cg")) line = QStringLiteral("    scene cg ") + image;
    else if (kind == QLatin1String("black") || kind == QLatin1String("white")) line = QStringLiteral("    scene ") + kind;
    else if (kind == QLatin1String("bg")) {
        const QString low = image.toLower();
        line = (low == QLatin1String("black") || low == QLatin1String("white")) ? QStringLiteral("    scene ") + image : QStringLiteral("    scene bg ") + image;
    } else {
        image = pyStrip(joinW(SL{kind} + w));
        line = QStringLiteral("    scene ") + image;
    }
    if (!effect.isEmpty() && effect != QLatin1String("none")) line += QStringLiteral(" with ") + effect;
    return {line};
}

SL compileNoteNvl()
{
    return {QStringLiteral("    window hide"), QStringLiteral("    show genry_note_dim onlayer overlay:"), QStringLiteral("        alpha 0.60"),
            QStringLiteral("    with dissolve"), QStringLiteral("    $ set_mode_nvl()"), QStringLiteral("    nvl show")};
}

SL compileNoteAdv()
{
    return {QStringLiteral("    nvl clear"), QStringLiteral("    nvl hide"), QStringLiteral("    $ set_mode_adv()"),
            QStringLiteral("    hide genry_note_dim onlayer overlay"), QStringLiteral("    with dissolve"), QStringLiteral("    window show")};
}

SL compileClearOverlays()
{
    return {QStringLiteral("    hide genry_note_dim onlayer overlay"), QStringLiteral("    hide prologue_dream"),
            QStringLiteral("    hide blinking onlayer overlay"), QStringLiteral("    hide blink onlayer overlay"),
            QStringLiteral("    hide unblink onlayer overlay"), QStringLiteral("    with dissolve")};
}

void parsePrologueFx(const QString& rest, const QString& defAlpha, QString* alpha, QString* effect)
{
    *alpha = defAlpha;
    static const QRegularExpression num(QStringLiteral("^-?\\d+(\\.\\d+)?$"), QRegularExpression::UseUnicodePropertiesOption);
    for (const QString& word : words(rest)) {
        const QString low = word.toLower();
        if (isEffect(low)) { *effect = low; continue; }
        if (num.match(low).hasMatch()) *alpha = cleanAlpha(low, defAlpha);
    }
}

SL prologueDreamShow(const QString& alphaIn)
{
    const QString a = cleanAlpha(alphaIn, QStringLiteral("0.70"));
    if (a == QLatin1String("1") || a == QLatin1String("1.0") || a == QLatin1String("1.00")) return {QStringLiteral("    show prologue_dream") + effectZorder()};
    return {QStringLiteral("    show prologue_dream%1:").arg(effectZorder()), QStringLiteral("        alpha ") + a};
}

SL compileMemoryEffect(const QString& kind, const QString& rest)
{
    QString alpha, effect;
    parsePrologueFx(rest, QStringLiteral("0.70"), &alpha, &effect);
    SL r;
    if (kind == QLatin1String("staticfx") || kind == QLatin1String("noisefx") || kind == QLatin1String("vhsfx")) {
        r = prologueDreamShow(alpha);
        if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
        return r;
    }
    if (kind == QLatin1String("glitchfx")) return prologueDreamShow(alpha) << QStringLiteral("    with hpunch");
    if (kind == QLatin1String("memoryfx")) {
        r << QStringLiteral("    window hide") << QStringLiteral("    show blink onlayer overlay") << QStringLiteral("    $ renpy.pause(0.25, hard=True)");
        r << prologueDreamShow(alpha);
        r << QStringLiteral("    hide blink onlayer overlay") << QStringLiteral("    show unblink onlayer overlay")
          << QStringLiteral("    $ renpy.pause(1.0, hard=True)") << QStringLiteral("    hide unblink onlayer overlay") << QStringLiteral("    window show");
        return r;
    }
    if (kind == QLatin1String("dreamfx")) {
        r = prologueDreamShow(alpha);
        r << QStringLiteral("    show blinking onlayer overlay");
        if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
        return r;
    }
    r = prologueDreamShow(alpha);
    if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
    return r;
}

// ------------------------------------------------------------------ audio

// parse_audio_options: pulls fadein/fadeout/loop/noloop out of the words
QString parseAudioOptions(SL& w, const QString& defaultFadein)
{
    SL options;
    int idx = 0;
    while (idx < w.size()) {
        const QString word = w[idx].toLower();
        if (word == QLatin1String("fadein") || word == QLatin1String("fadeout")) {
            if (idx + 1 < w.size()) {
                options << word + QLatin1Char(' ') + cleanNumber(w[idx + 1], QStringLiteral("2"));
                w.removeAt(idx);
                w.removeAt(idx);
                continue;
            }
            w.removeAt(idx);
            continue;
        }
        if (word == QLatin1String("loop") || word == QLatin1String("noloop")) {
            options << word;
            w.removeAt(idx);
            continue;
        }
        ++idx;
    }
    if (!defaultFadein.isNull()) {
        bool has = false;
        for (const QString& o : options) if (o.startsWith(QLatin1String("fadein "))) has = true;
        if (!has) options.prepend(QStringLiteral("fadein ") + defaultFadein);
    }
    return options.isEmpty() ? QString(QStringLiteral("")) : QStringLiteral(" ") + options.join(QLatin1Char(' '));
}

// V1: ES lint evaluates the file of every play/queue statement. A temp variable
// ("play music _genry_music_file") can't be evaluated there and is reported; the
// resolver call itself can, and lint then also checks the track really exists.
QString playFile(bool legacy, const QString& tempVar, const QString& expr)
{
    return legacy ? tempVar : expr;
}

SL compileMusicPlay(const QString& rest, const Ctx& c)
{
    SL w = words(rest);
    const QString opts = parseAudioOptions(w, QStringLiteral("2"));
    const QString name = pyStrip(joinW(w));
    if (name.isEmpty()) return {U("    # music command needs a music id")};
    const QString resolve = QStringLiteral("genry_resolve_music(%1)").arg(pyQ(name));
    return {QStringLiteral("    $ _genry_music_file = ") + resolve, QStringLiteral("    if _genry_music_file:"),
            QStringLiteral("        play music ") + playFile(c.opt.legacy, QStringLiteral("_genry_music_file"), resolve) + opts,
            QStringLiteral("    else:"), QStringLiteral("        stop music fadeout 2"),
            QStringLiteral("        $ genry_warn_missing_audio(u\"music\", %1)").arg(pyQ(name))};
}

SL compileMusicFile(const QString& rest, const QString& modId)
{
    SL w = words(rest);
    const QString opts = parseAudioOptions(w, QStringLiteral("2"));
    return {QStringLiteral("    play music ") + pyQ(relModPath(modId, pyStrip(joinW(w)))) + opts};
}

SL compileAmbience(const QString& rest, const QString& modId, const Ctx& c)
{
    SL w = words(rest);
    const QString opts = parseAudioOptions(w, QStringLiteral("2"));
    QString path = pyStrip(joinW(w));
    if (path.isEmpty()) return {U("    # ambience command needs a path")};
    // V1: "атмосфера ambience_camp_center_day" is an ES variable, not a file inside the mod
    if (!c.opt.legacy && path.startsWith(QLatin1String("ambience_")) && !path.contains(QLatin1Char('/')) && !path.contains(QLatin1Char('.')))
        return {QStringLiteral("    play ambience ") + path + opts};
    if (!(path.startsWith(QLatin1String("sound/")) || path.startsWith(QLatin1String("mods/")))) path = relModPath(modId, path);
    return {QStringLiteral("    play ambience ") + pyQ(path) + opts};
}

SL compileAudioQueue(const QString& channel, const QString& rest, const QString& modId, bool builtinMusic, const Ctx& c)
{
    SL p = splitPipeItems(rest);
    if (p.isEmpty()) return {U("    # queue command needs: item | item | item [fadein 1 loop]")};
    SL last = words(p.last());
    QString opts;
    if (!last.isEmpty()) {
        opts = parseAudioOptions(last, QString());
        p.last() = pyStrip(joinW(last));
    }
    SL items;
    for (const QString& i : p) if (!i.isEmpty()) items << i;
    if (items.isEmpty()) return {U("    # queue command needs at least one item")};
    if (builtinMusic) {
        SL q, r;
        for (const QString& i : items) {
            q << pyQ(i);
            r << QStringLiteral("genry_resolve_music(%1)").arg(pyQ(i));
        }
        const QString v1Queue = QStringLiteral("[_genry_f for _genry_f in [") + r.join(QStringLiteral(", ")) + QStringLiteral("] if _genry_f]");
        return {QStringLiteral("    $ _genry_music_queue_names = [%1]").arg(q.join(QStringLiteral(", "))),
                QStringLiteral("    $ _genry_music_queue = [genry_resolve_music(_genry_music_name) for _genry_music_name in _genry_music_queue_names if genry_resolve_music(_genry_music_name)]"),
                QStringLiteral("    if _genry_music_queue:"),
                QStringLiteral("        queue ") + channel + QLatin1Char(' ') + playFile(c.opt.legacy, QStringLiteral("_genry_music_queue"), v1Queue) + opts,
                QStringLiteral("    else:"), QStringLiteral("        $ genry_warn_missing_audio(u\"musicqueue\", u\", \".join(_genry_music_queue_names))")};
    }
    SL q;
    for (QString path : items) {
        if (!(path.startsWith(QLatin1String("sound/")) || path.startsWith(QLatin1String("mods/")))) path = relModPath(modId, path);
        q << pyQ(path);
    }
    return {QStringLiteral("    queue %1 [%2]%3").arg(channel, q.join(QStringLiteral(", ")), opts)};
}

SL compileSoundExpr(const QString& rest, const Ctx& c)
{
    const QString expr = pyStrip(rest);
    if (expr.isEmpty()) return {U("    # sound command needs an id")};
    const QString low = expr.toLower();
    if (low.startsWith(QLatin1Char('"')) || low.startsWith(QLatin1String("u\"")) || low.startsWith(QLatin1Char('\'')) || low.startsWith(QLatin1String("u'")))
        return {QStringLiteral("    play sound ") + expr};
    const QString resolve = QStringLiteral("genry_resolve_audio_expr(%1)").arg(pyQ(expr));
    return {QStringLiteral("    $ _genry_sound_file = ") + resolve, QStringLiteral("    if _genry_sound_file:"),
            QStringLiteral("        play sound ") + playFile(c.opt.legacy, QStringLiteral("_genry_sound_file"), resolve), QStringLiteral("    else:"),
            QStringLiteral("        $ genry_warn_missing_audio(u\"sound\", %1)").arg(pyQ(expr))};
}

// ------------------------------------------------------------------ cards, popups, logic

SL compileChapterPng(const QString& rest, const Ctx& c)
{
    const SL p = c.pipes(rest);
    if (p.size() < 3) return {U("    # chapterpng needs: image | day | title | [backdrop] | [seconds] | [alpha] | [zoom]")};
    const QString image = p[0];
    const QString day = cleanInteger(p[1], QStringLiteral("1"));
    const QString title = !p[2].isEmpty() ? p[2] : U("День ") + day;
    const QString seconds = p.size() > 4 ? cleanNumber(p[4], QStringLiteral("4.0")) : QStringLiteral("4.0");
    const QString alpha = p.size() > 5 ? cleanMinNumber(p[5], QStringLiteral("0.92"), 0.75) : QStringLiteral("0.92");
    const QString zoom = p.size() > 6 ? cleanMinNumber(p[6], QStringLiteral("1.18"), 1.05) : QStringLiteral("1.18");
    const QString startX = p.size() > 7 ? cleanNumber(p[7], QStringLiteral("-0.08")) : QStringLiteral("-0.08");
    const QString endX = p.size() > 8 ? cleanNumber(p[8], QStringLiteral("0.18")) : QStringLiteral("0.18");
    const QString ycenter = p.size() > 9 ? cleanNumber(p[9], QStringLiteral("0.58")) : QStringLiteral("0.58");
    const QString dayText = U("День ") + day;
    return {QStringLiteral("    window hide"), QStringLiteral("    scene genry_chapter_back"), QStringLiteral("    show genry_chapter_noise"),
            QStringLiteral("    show expression Text(%1, size=96, color=\"#d8d8d8\", text_align=0.5) as genry_chapter_day:").arg(pyUQ(dayText)),
            QStringLiteral("        xcenter 0.455"), QStringLiteral("        ycenter 0.405"), QStringLiteral("        alpha 0.72"),
            QStringLiteral("    show expression Text(%1, size=28, color=\"#cfd4dc\", text_align=0.5) as genry_chapter_title:").arg(pyUQ(title)),
            QStringLiteral("        xcenter 0.455"), QStringLiteral("        ycenter 0.515"), QStringLiteral("        alpha 0.82"),
            QStringLiteral("    show %1 as genry_chapter_png:").arg(image), QStringLiteral("        xcenter ") + startX,
            QStringLiteral("        ycenter ") + ycenter, QStringLiteral("        alpha 0.0"), QStringLiteral("        zoom ") + zoom,
            QStringLiteral("        parallel:"), QStringLiteral("            linear 0.45 alpha ") + alpha, QStringLiteral("        parallel:"),
            QStringLiteral("            linear %1 xcenter %2").arg(seconds, endX), QStringLiteral("    with dissolve"),
            QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(seconds), QStringLiteral("    hide genry_chapter_png"),
            QStringLiteral("    hide genry_chapter_day"), QStringLiteral("    hide genry_chapter_title"), QStringLiteral("    hide genry_chapter_noise"),
            QStringLiteral("    with dissolve"), QStringLiteral("    scene black"), QStringLiteral("    with dissolve")};
}

SL compileTimeskip(const QString& restIn, bool v1 = false, bool eyesClosed = false)
{
    const QString rest = pyStrip(restIn);
    QString text;
    static const QRegularExpression digit(QStringLiteral("^[0-9]"));
    if (rest.isEmpty()) text = U("Прошло немного времени…");
    else if (digit.match(rest).hasMatch()) text = U("Прошло ") + rest;
    else text = rest;
    if (v1) {
        // V1: the eyes ARE the transition - shut (unless already), the words over the lids, no darkness;
        // compileLine opens them again before the next thing the player sees
        SL r = eyesClosed ? SL{} : compileEyesClose(QStringLiteral("1.6"));
        return r << overLidsText(QStringLiteral("genry_timeskip"), text, 62, QStringLiteral("1.9"));
    }
    return {QStringLiteral("    window hide"), QStringLiteral("    scene black"), QStringLiteral("    with dissolve"),
            QStringLiteral("    show expression Text(%1, size=64, color=\"#e8eef7\", text_align=0.5) as genry_timeskip:").arg(pyUQ(text)),
            QStringLiteral("        xcenter 0.5"), QStringLiteral("        ycenter 0.5"), QStringLiteral("        alpha 0.0"),
            QStringLiteral("        linear 0.6 alpha 1.0"), QStringLiteral("    with dissolve"), QStringLiteral("    $ renpy.pause(2.0, hard=True)"),
            QStringLiteral("    hide genry_timeskip"), QStringLiteral("    with dissolve")};
}

SL compileAchievementPopup(const QString& path, const QString& secondsIn, const Ctx& c)
{
    const QString s = cleanNumber(secondsIn.isEmpty() ? QStringLiteral("3") : secondsIn, QStringLiteral("3"));
    const QString resolve = QStringLiteral("genry_resolve_audio_expr(\"sfx_achievement\") or genry_resolve_audio_expr(\"aunl\")");
    return {QStringLiteral("    $ _genry_achievement_path = ") + pyQ(path),
            QStringLiteral("    $ _genry_achievement_sound = ") + resolve,
            QStringLiteral("    if _genry_achievement_sound:"),
            QStringLiteral("        play sound ") + playFile(c.opt.legacy, QStringLiteral("_genry_achievement_sound"), QLatin1Char('(') + resolve + QLatin1Char(')')),
            QStringLiteral("    show expression Image(_genry_achievement_path) as genry_achievement_popup onlayer overlay:"),
            QStringLiteral("        xalign 1.18"), QStringLiteral("        yalign 0.08"), QStringLiteral("        alpha 0.0"),
            QStringLiteral("        linear 0.28 xalign 0.98 alpha 1.0"), QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(s),
            QStringLiteral("    show expression Image(_genry_achievement_path) as genry_achievement_popup onlayer overlay:"),
            QStringLiteral("        xalign 0.98"), QStringLiteral("        yalign 0.08"), QStringLiteral("        alpha 1.0"),
            QStringLiteral("        linear 0.25 xalign 1.18 alpha 0.0"), QStringLiteral("    $ renpy.pause(0.25, hard=True)"),
            QStringLiteral("    hide genry_achievement_popup onlayer overlay")};
}

SL compileNotifyPopup(const QString& rest, const QString& modId, const Ctx& c)
{
    const SL p = c.pipes(rest);
    QString text = !p.isEmpty() ? pyStrip(p[0]) : pyStrip(rest);
    if (text.isEmpty()) text = QStringLiteral("...");
    const QString title = p.size() > 1 && !pyStrip(p[1]).isEmpty() ? pyStrip(p[1]) : QStringLiteral("Genry");
    const QString icon = p.size() > 2 && !pyStrip(p[2]).isEmpty() ? pyStrip(p[2]) : QString();
    const QString panel = p.size() > 3 && !pyStrip(p[3]).isEmpty() ? pyStrip(p[3]) : QStringLiteral("#111827");
    QString side = p.size() > 4 && !pyStrip(p[4]).isEmpty() ? pyStrip(p[4]).toLower() : QStringLiteral("right");
    const QString seconds = p.size() > 5 ? cleanNumber(p[5], QStringLiteral("3.0")) : QStringLiteral("3.0");
    static const QHash<QString, QString> sides{{U("справа"), QStringLiteral("right")}, {QStringLiteral("right"), QStringLiteral("right")},
                                               {U("слева"), QStringLiteral("left")}, {QStringLiteral("left"), QStringLiteral("left")},
                                               {U("сверху"), QStringLiteral("top")}, {QStringLiteral("top"), QStringLiteral("top")},
                                               {U("снизу"), QStringLiteral("bottom")}, {QStringLiteral("bottom"), QStringLiteral("bottom")}};
    side = sides.value(side, QStringLiteral("right"));
    const QString iconArg = icon.isEmpty() ? QStringLiteral("None") : pyQ(relModPath(modId, icon));
    return {QStringLiteral("    show screen genry_notify_popup(%1, %2, icon=%3, panel_color=%4, side=%5)")
                .arg(pyUQ(title), pyUQ(text), iconArg, pyQ(panel), pyQ(side)),
            QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(seconds), QStringLiteral("    hide screen genry_notify_popup"),
            QStringLiteral("    $ renpy.pause(0.25, hard=True)")};
}

SL compileTimeOfDay(const QString& rest)
{
    const QString m = pyStrip(rest).toLower();
    if (m == U("день") || m == QLatin1String("day") || m == U("утро") || m == QLatin1String("morning"))
        return {QStringLiteral("    $ persistent.sprite_time = \"day\""), QStringLiteral("    $ day_time()")};
    if (m == U("вечер") || m == U("закат") || m == QLatin1String("sunset") || m == QLatin1String("evening"))
        return {QStringLiteral("    $ persistent.sprite_time = \"sunset\""), QStringLiteral("    $ sunset_time()")};
    if (m == U("ночь") || m == QLatin1String("night")) return {QStringLiteral("    $ persistent.sprite_time = \"night\""), QStringLiteral("    $ night_time()")};
    if (m == U("пролог") || m == QLatin1String("prolog") || m == QLatin1String("prologue")) return {QStringLiteral("    $ prolog_time()")};
    return {QStringLiteral("    # timeofday command needs: day / sunset / night / prolog")};
}

SL compileVoicedSay(const QString& rest, const QString& modId, CompileState& st, const Ctx& c)
{
    const SL p = splitPipeItems(rest);
    QString speaker, relPath, text;
    if (p.size() >= 3) {
        speaker = p[0];
        relPath = p[1];
        text = p.mid(2).join(QStringLiteral(" | "));
    } else {
        const SL w = words(rest);
        if (w.size() < 3) return {U("    # voiced line needs: speaker audio/file.ogg | text")};
        speaker = w[0];
        relPath = w[1];
        text = pyStrip(pySplit(rest, 2).value(2));
    }
    return {QStringLiteral("    play sound ") + pyQ(relModPath(modId, relPath)), I4 + speakerFor(speaker, st, c.opt) + QLatin1Char(' ') + pyQ(pyStrip(text))};
}

SL compilePunchedSay(const QString& rest, CompileState& st, const Ctx& c)
{
    const SL p = splitPipeItems(rest);
    QString speaker, effect, text;
    if (p.size() >= 3) {
        speaker = p[0];
        effect = p[1];
        text = p.mid(2).join(QStringLiteral(" | "));
    } else {
        const SL w = words(rest);
        if (w.size() < 3) return {U("    # punched line needs: speaker hpunch/vpunch | text")};
        speaker = w[0];
        effect = w[1];
        text = pyStrip(pySplit(rest, 2).value(2));
    }
    effect = pyStrip(effect);
    if (effect != QLatin1String("hpunch") && effect != QLatin1String("vpunch")) effect = QStringLiteral("hpunch");
    return {I4 + speakerFor(speaker, st, c.opt) + QLatin1Char(' ') + pyQ(pyStrip(text)) + QStringLiteral(" with ") + effect};
}

SL screenMenuTargets(const QString& rest, const Ctx& c)
{
    SL t;
    for (const QString& item : splitPipeItems(rest)) {
        if (!item.contains(QLatin1String("->"))) continue;
        const QString target = c.lab(pyStrip(pySplitSep(item, QStringLiteral("->"), 1).value(1)));
        if (!target.isEmpty() && !t.contains(target)) t << target;
    }
    return t;
}

// value after the first word when command_payload gives nothing (item.split(None, 1)[1].strip())
QString tailOf(const QString& item)
{
    return words(item).size() > 1 ? pyStrip(pySplit(item, 1).value(1)) : QString();
}

QPair<QString, QString> commandPayload(const QString& line)
{
    const SL p = words(line);
    if (p.isEmpty()) return {QString(), QString()};
    return {normalizeCommand(p[0]), pyStrip(line.mid(p[0].size()))};
}

SL compileScreenMenu(const QString& rest, const QString& modId, const Ctx& c)
{
    const SL parts = splitPipeItems(rest);
    if (parts.isEmpty()) return {U("    # screenmenu needs: title | bg ext_square_day | music my_daily_life | Button -> label")};
    const QString title = parts[0];
    QString subtitle, hint, footer, bgImage, music, logoImage, charImage;
    QString charX = QStringLiteral("0.82"), charY = QStringLiteral("1.0"), panelX = QStringLiteral("0.78");
    QString panelColor = QStringLiteral("#111827"), panelAlpha = QStringLiteral("0.88"), accent = QStringLiteral("#7dd3fc");
    QString buttonColor = QStringLiteral("#182131"), buttonHover = QStringLiteral("#2b5c8f"), textColor = QStringLiteral("#eef6ff");
    QString titleSize = QStringLiteral("38"), menuWidth = QStringLiteral("470"), dimAlpha = QStringLiteral("0.45");
    QVector<QPair<QString, QString>> items;

    struct Style { const char* pc; const char* ac; const char* bc; const char* bh; const char* tc; const char* pa; const char* mw; const char* ts; };
    static const QHash<QString, Style> styles{
        {QStringLiteral("neon"), {"#07111f", "#38bdf8", "#10233a", "#2563eb", "#eef6ff", "0.88", "500", "40"}},
        {QStringLiteral("night"), {"#05070b", "#93c5fd", "#111827", "#1d4ed8", "#f8fafc", "0.86", "470", "38"}},
        {QStringLiteral("terminal"), {"#06110a", "#79ff9f", "#102014", "#245b35", "#eaffef", "0.90", "500", "36"}},
        {QStringLiteral("blood"), {"#170608", "#fb7185", "#2b1114", "#7f1d1d", "#fff1f2", "0.90", "500", "38"}},
        {QStringLiteral("warm"), {"#17100b", "#fbbf24", "#2a1b10", "#92400e", "#fff7ed", "0.88", "470", "38"}}};
    auto applyStyle = [&](const QString& name) {
        auto it = styles.constFind(pyStrip(name).toLower());
        if (it == styles.constEnd()) return;
        panelColor = U(it->pc); accent = U(it->ac); buttonColor = U(it->bc); buttonHover = U(it->bh);
        textColor = U(it->tc); panelAlpha = U(it->pa); menuWidth = U(it->mw); titleSize = U(it->ts);
    };
    static const QSet<QString> logoWords{QStringLiteral("logo"), U("логотип"), U("лого")};

    for (const QString& item : parts.mid(1)) {
        const auto payload = commandPayload(item);
        const QString itemCmd = payload.first, itemRest = payload.second;
        const QString itemWord = firstWord(item).toLower();
        auto valueOr = [&](const QString& v) { return !v.isEmpty() ? v : tailOf(item); };
        if (itemWord == QLatin1String("style") || itemWord == U("стиль") || itemWord == U("тема")) { applyStyle(valueOr(itemRest)); continue; }
        if (itemWord == QLatin1String("subtitle") || itemWord == U("подзаголовок") || itemWord == U("описание")) { subtitle = valueOr(itemRest); continue; }
        if (itemWord == QLatin1String("hint") || itemWord == U("подсказка")) { hint = valueOr(itemRest); continue; }
        if (itemWord == QLatin1String("footer") || itemWord == U("низ") || itemWord == U("подпись")) { footer = valueOr(itemRest); continue; }
        if (itemWord == QLatin1String("side") || itemWord == U("сторона")) {
            const QString v = pyStrip(itemRest).toLower();
            if (v == QLatin1String("left")) panelX = QStringLiteral("0.22");
            else if (v == QLatin1String("center")) panelX = QStringLiteral("0.50");
            else if (v == QLatin1String("right")) panelX = QStringLiteral("0.78");
            continue;
        }
        if (itemWord == QLatin1String("dim") || itemWord == U("затемнение")) { dimAlpha = cleanFloatRange(itemRest, dimAlpha, 0.0, 0.95); continue; }
        if (itemWord == QLatin1String("alpha") || itemWord == U("прозрачность")) { panelAlpha = cleanFloatRange(itemRest, panelAlpha, 0.15, 1.0); continue; }
        if (itemWord == QLatin1String("width") || itemWord == U("ширина")) { menuWidth = cleanFloatRange(itemRest, menuWidth, 280.0, 900.0); continue; }
        if (itemWord == QLatin1String("title_size") || itemWord == U("размерзаголовка") || itemWord == U("размер")) {
            titleSize = cleanFloatRange(itemRest, titleSize, 20.0, 72.0);
            continue;
        }
        if (itemWord == QLatin1String("button") || itemWord == U("кнопка")) {
            const SL w = words(itemRest);
            if (w.size() >= 1 && pyIsHexColor(w[0])) buttonColor = w[0];
            if (w.size() >= 2 && pyIsHexColor(w[1])) buttonHover = w[1];
            if (w.size() >= 3 && pyIsHexColor(w[2])) textColor = w[2];
            continue;
        }
        if (itemCmd == QLatin1String("bg")) {
            const QString v = itemRest;
            if (!v.isEmpty())
                bgImage = (v.startsWith(QLatin1String("bg ")) || v.startsWith(QLatin1String("cg ")) || v.startsWith(QLatin1String("genry "))) ? v : QStringLiteral("bg ") + v;
            continue;
        }
        if (itemCmd == QLatin1String("music")) {
            if (!itemRest.isEmpty()) music = itemRest;
            continue;
        }
        if (logoWords.contains(itemCmd) || logoWords.contains(itemWord)) {
            QString v = itemRest;
            if (v.isEmpty()) v = tailOf(item);
            if (!v.isEmpty()) {
                logoImage = v;
                if (v.contains(QLatin1Char('/')) || pyBasename(v).contains(QLatin1Char('.'))) logoImage = relModPath(modId, v);
            }
            continue;
        }
        if (itemCmd == QLatin1String("character") || itemWord == QLatin1String("sprite") || itemWord == U("спрайт")) {
            SL w = words(itemRest);
            if (!w.isEmpty()) {
                const QString pen = w.size() >= 2 ? w.at(w.size() - 2).toLower() : QString();
                if (w.size() >= 3 && (pen == QLatin1String("x") || pen == QLatin1String("xalign"))) {
                    charX = cleanFloatRange(w.last(), QStringLiteral("0.82"), -0.5, 1.5);
                    w = w.mid(0, w.size() - 2);
                } else if (isPosition(w.last())) {
                    static const QHash<QString, QString> px{{QStringLiteral("left"), QStringLiteral("0.16")}, {QStringLiteral("fleft"), QStringLiteral("0.02")},
                                                            {QStringLiteral("cleft"), QStringLiteral("0.34")}, {QStringLiteral("center"), QStringLiteral("0.50")},
                                                            {QStringLiteral("truecenter"), QStringLiteral("0.50")}, {QStringLiteral("cright"), QStringLiteral("0.66")},
                                                            {QStringLiteral("right"), QStringLiteral("0.84")}, {QStringLiteral("fright"), QStringLiteral("0.98")}};
                    charX = px.value(w.takeLast(), QStringLiteral("0.82"));
                }
                charImage = joinW(w);
            }
            continue;
        }
        if (itemWord == QLatin1String("panel") || itemWord == U("панель")) {
            const SL w = words(itemRest);
            if (!w.isEmpty()) {
                const QString fb = cleanFloatRange(w[0], QStringLiteral("0.78"), 0.0, 1.0);
                if (w[0] == QLatin1String("left")) panelX = QStringLiteral("0.20");
                else if (w[0] == QLatin1String("center")) panelX = QStringLiteral("0.50");
                else if (w[0] == QLatin1String("right")) panelX = QStringLiteral("0.78");
                else panelX = fb;
            }
            if (w.size() >= 2 && pyIsHexColor(w[1])) panelColor = w[1];
            if (w.size() >= 3 && pyIsHexColor(w[2])) accent = w[2];
            if (w.size() >= 4) panelAlpha = cleanFloatRange(w[3], panelAlpha, 0.15, 1.0);
            if (w.size() >= 5 && pyIsHexColor(w[4])) buttonColor = w[4];
            if (w.size() >= 6 && pyIsHexColor(w[5])) buttonHover = w[5];
            continue;
        }
        if (item.contains(QLatin1String("->"))) {
            const SL ct = pySplitSep(item, QStringLiteral("->"), 1);
            const QString caption = pyStrip(ct[0]);
            const QString target = c.lab(pyStrip(ct[1]));
            if (!caption.isEmpty() && !target.isEmpty()) items.push_back({caption, target});
        }
    }
    if (items.isEmpty()) items.push_back({U("Продолжить"), QString()});
    SL py;
    for (const auto& it : items) py << QStringLiteral("(%1, %2)").arg(pyUQ(it.first), pyQ(it.second));
    const QString pyItems = QLatin1Char('[') + py.join(QStringLiteral(", ")) + QLatin1Char(']');
    SL lines;
    if (!music.isEmpty()) {
        if (music.contains(QLatin1Char('/')) || pyBasename(music).contains(QLatin1Char('.'))) {
            const QString path = music.startsWith(QLatin1String("mods/")) ? music : relModPath(modId, music);
            lines << QStringLiteral("    play music %1 fadein 1").arg(pyQ(path));
        } else {
            const QString resolve = QStringLiteral("genry_resolve_music(%1)").arg(pyQ(music));
            lines << QStringLiteral("    $ _genry_music_file = ") + resolve << QStringLiteral("    if _genry_music_file:")
                  << QStringLiteral("        play music ") + playFile(c.opt.legacy, QStringLiteral("_genry_music_file"), resolve) + QStringLiteral(" fadein 1")
                  << QStringLiteral("    else:")
                  << QStringLiteral("        $ genry_warn_missing_audio(u\"screenmenu music\", %1)").arg(pyQ(music));
        }
    }
    // plain concatenation: chained QString::arg would re-scan user text for %N
    const QString none = QStringLiteral("None");
    lines << QStringLiteral("    call screen genry_screen_menu(title=") + pyUQ(title) + QStringLiteral(", items=") + pyItems +
                 QStringLiteral(", bg_image=") + (bgImage.isEmpty() ? none : pyQ(bgImage)) +
                 QStringLiteral(", logo_image=") + (logoImage.isEmpty() ? none : pyQ(logoImage)) +
                 QStringLiteral(", char_image=") + (charImage.isEmpty() ? none : pyQ(charImage)) + QStringLiteral(", char_x=") + charX +
                 QStringLiteral(", char_y=") + charY + QStringLiteral(", panel_x=") + panelX + QStringLiteral(", panel_color=") + pyQ(panelColor) +
                 QStringLiteral(", accent=") + pyQ(accent) + QStringLiteral(", panel_alpha=") + panelAlpha +
                 QStringLiteral(", button_color=") + pyQ(buttonColor) + QStringLiteral(", button_hover=") + pyQ(buttonHover) +
                 QStringLiteral(", text_color=") + pyQ(textColor) + QStringLiteral(", title_size=") + titleSize +
                 QStringLiteral(", menu_width=") + menuWidth + QStringLiteral(", subtitle=") + pyUQ(subtitle) + QStringLiteral(", hint=") + pyUQ(hint) +
                 QStringLiteral(", footer=") + pyUQ(footer) + QStringLiteral(", dim_alpha=") + dimAlpha + QStringLiteral(") with dissolve")
          << QStringLiteral("    $ _genry_screen_target = _return") << QStringLiteral("    if _genry_screen_target:")
          << QStringLiteral("        jump expression _genry_screen_target");
    return lines;
}

SL compileFloatingThought(const QString& rest)
{
    SL p;
    for (const QString& x : pySplitSep(rest, QStringLiteral("|"))) p << pyStrip(x);
    const QString text = !p.isEmpty() && !p[0].isEmpty() ? p[0] : U("Мысль.");
    auto part = [&](int i, const char* d) { return p.size() > i && !p[i].isEmpty() ? p[i] : U(d); };
    auto numeric = [](const QString& v, const char* d) {
        double x;
        return pyFloat(v, &x) ? pyRepr(x) : U(d);
    };
    const QString x = numeric(part(1, "0.5"), "0.5"), y = numeric(part(2, "0.18"), "0.18"), s = numeric(part(3, "3.0"), "3.0");
    return {QStringLiteral("    show expression Text(%1, style=\"genry_thought\") as genry_thought_text at genry_thought_float(%2, %3, %4)")
                .arg(pyQ(text), x, y, s)};
}

SL compileColorFilter(const QString& rest)
{
    SL w = words(rest);
    QString effect;
    if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
    const QString token = !w.isEmpty() ? w[0].toLower() : QString();
    QString key;
    if (token.isEmpty() || T().filterStop.contains(token)) key = QStringLiteral("none");
    else {
        key = T().filterAliases.value(token);
        if (key.isEmpty())
            return {U("    # фильтр: неизвестно '%1'. Доступно: сепия, чб, ночь, тепло, холод, сон, выцвет, хоррор, нет").arg(token)};
    }
    SL r{QStringLiteral("    show layer master at genry_filter_") + key};
    if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
    return r;
}

SL compileWeather(const QString& rest, const Ctx& c)
{
    SL w = words(rest);
    if (w.isEmpty()) return {U("    # weather command needs: snow|rain|leaf|heart|spark|dust  или  стоп")};
    QString effect;
    if (isEffect(w.last())) effect = w.takeLast();
    const QString token = !w.isEmpty() ? w[0].toLower() : QString();
    if (T().weatherStop.contains(token) || w.isEmpty()) {
        SL r;
        for (const auto& kv : T().weather) r << QStringLiteral("    hide genry_weather_") + kv.first;
        if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
        return r;
    }
    const QString key = T().weatherAliases.value(token);
    if (key.isEmpty()) return {U("    # weather: неизвестный тип '%1'. Доступно: снег, дождь, листья, сердца, искры, пыль, стоп").arg(token)};
    QString line = QStringLiteral("    show genry_weather_") + key;
    if (!c.opt.legacy) {
        // V1: layered soft particles from the mod's genry_fx pictures, «слабо»/«сильно» = how many
        // (the old weather: 60 six-pixel squares starting above the screen - barely visible)
        int level = 2;
        for (const QString& x : w.mid(1))
            if (const int l = weatherLevelWord(x)) level = l;
        line = QStringLiteral("    show %1 as genry_weather_%2").arg(c.sys(QStringLiteral("weather_%1_%2").arg(key).arg(level)), key);
    }
    if (!effect.isEmpty() && effect != QLatin1String("none")) line += QStringLiteral(" with ") + effect;
    return {line};
}

// the image definitions of every «погода» the mod shows (V1)
SL v1WeatherImages(const QString& all, const QString& modId, const Ctx& c)
{
    static const QString kinds = QStringLiteral("(snow|rain|leaf|heart|spark|dust)_([123])");
    const QRegularExpression re(QRegularExpression::escape(c.sys(QStringLiteral("weather_"))) + kinds);
    QStringList used;
    for (auto it = re.globalMatch(all); it.hasNext();) {
        const QString n = it.next().captured(0);
        if (!used.contains(n)) used << n;
    }
    if (used.isEmpty()) return {};
    used.sort();
    SL r{QString(), QStringLiteral("transform %1:").arg(c.sys(QStringLiteral("fx_spin"))), QStringLiteral("    rotate 0"),
         QStringLiteral("    linear 5.0 rotate 360"), QStringLiteral("    repeat"),
         QStringLiteral("transform %1:").arg(c.sys(QStringLiteral("fx_spinfast"))), QStringLiteral("    rotate 0"),
         QStringLiteral("    linear 2.6 rotate 360"), QStringLiteral("    repeat"),
         QStringLiteral("transform %1:").arg(c.sys(QStringLiteral("fx_twinkle"))), QStringLiteral("    alpha 1.0"),
         QStringLiteral("    linear 0.9 alpha 0.35"), QStringLiteral("    linear 0.9 alpha 1.0"), QStringLiteral("    repeat"), QString()};
    for (const QString& name : used) {
        const QRegularExpressionMatch m = re.match(name);
        const QString key = m.captured(1);
        const int level = m.captured(2).toInt();
        SL parts;
        const QString tint = weatherTint(key, level);
        if (!tint.isEmpty()) parts << QStringLiteral("Solid(\"%1\")").arg(tint);
        for (const WeatherLayer& l : weatherLayers(key)) {
            QString d = QStringLiteral("Transform(%1, zoom=%2, alpha=%3%4)")
                            .arg(pyQ(relModPath(modId, QStringLiteral("images/genry_fx/") + l.png + QStringLiteral(".png"))))
                            .arg(l.zoom).arg(l.alpha)
                            .arg(l.rotate != 0 ? QStringLiteral(", rotate=%1").arg(l.rotate) : QString());
            if (!l.anim.isEmpty()) d = QStringLiteral("At(%1, %2)").arg(d, c.sys(QStringLiteral("fx_") + l.anim));
            const int count = std::max(1, int(std::lround(l.count * weatherLevelFactor(level))));
            parts << QStringLiteral("SnowBlossom(%1, count=%2, border=60, xspeed=(%3, %4), yspeed=(%5, %6), fast=True)")
                         .arg(d).arg(count).arg(l.xs0).arg(l.xs1).arg(l.ys0).arg(l.ys1);
        }
        r << QStringLiteral("image %1 = Fixed(%2)").arg(name, parts.join(QStringLiteral(", ")));
    }
    return r;
}

QString effectOf(const QString& rest)
{
    const QString s = pyStrip(rest);
    return isEffect(s) ? s : QString();
}

// ------------------------------------------------------------------ story post-processing

bool lineIsTerminal(const QString& line)
{
    const QString s = pyStrip(line);
    return s == QLatin1String("return") || s.startsWith(QLatin1String("jump ")) || s.startsWith(QLatin1String("call "));
}

QString labelNameFromLine(const QString& line)
{
    if (!line.startsWith(QLatin1String("label "))) return {};
    QString n = pySplit(line, 1).value(1);
    while (n.endsWith(QLatin1Char(':'))) n.chop(1);
    return pyStrip(n);
}

bool shouldAutojump(const QString& current, const QString& next, const QSet<QString>& choiceTargets)
{
    if (current.isEmpty() || next.isEmpty()) return false;
    if (choiceTargets.contains(current)) return false;
    return next == current + QStringLiteral("_next");
}

SL appendMissingTargetLabels(SL lines, const SL& extraTargets)
{
    QSet<QString> defined;
    for (const QString& l : lines) if (l.startsWith(QLatin1String("label "))) defined.insert(labelNameFromLine(l));
    SL targets;
    static const QRegularExpression ident(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    static const QRegularExpression zone(QStringLiteral("\\$ set_zone\\([^,]+,\\s*\"([A-Za-z_][A-Za-z0-9_]*)\"\\)"));
    auto add = [&](QString t) {
        t = pyStripChars(pyStrip(t), QStringLiteral("\"'"));
        QString chk = t;
        if (chk.endsWith(QLatin1Char('\n'))) chk.chop(1);
        if (ident.match(chk).hasMatch() && !targets.contains(t)) targets << t;
    };
    for (const QString& line : lines) {
        const QString s = pyStrip(line);
        if (s.startsWith(QLatin1String("jump "))) { add(pySplit(s, 1).value(1)); continue; }
        if (s.startsWith(QLatin1String("call "))) { add(pySplitSep(pySplit(s, 1).value(1), QStringLiteral("("), 1).value(0)); continue; }
        const auto m = zone.match(s);
        if (m.hasMatch()) add(m.captured(1));
    }
    for (const QString& t : extraTargets) add(t);
    for (const QString& t : targets) {
        if (defined.contains(t)) continue;
        lines << QString() << QStringLiteral("label %1:").arg(t) << QStringLiteral("    return");
        defined.insert(t);
    }
    return lines;
}

SL endgameTerminator()
{
    return {QStringLiteral("    stop music fadeout 2"), QStringLiteral("    stop ambience fadeout 2"), QStringLiteral("    stop sound"), QStringLiteral("    return")};
}

SL ensureLabelExits(const SL& lines, const QSet<QString>& choiceTargets)
{
    SL fixed;
    bool inLabel = false, hasBody = false;
    QString current, lastExec;
    auto close = [&](const QString& next) {
        if (!inLabel) return;
        if (!hasBody || !lineIsTerminal(lastExec)) {
            if (shouldAutojump(current, next, choiceTargets)) fixed << QStringLiteral("    jump ") + next;
            else if (next.isEmpty()) fixed << endgameTerminator();
            else fixed << QStringLiteral("    return");
        }
    };
    for (const QString& line : lines) {
        if (line.startsWith(QLatin1String("label "))) {
            close(labelNameFromLine(line));
            fixed << line;
            inLabel = true;
            current = labelNameFromLine(line);
            hasBody = false;
            lastExec.clear();
            continue;
        }
        if (inLabel && line.startsWith(I4) && !pyStrip(line).isEmpty() && !pyLStrip(line).startsWith(QLatin1Char('#'))) {
            hasBody = true;
            lastExec = pyStrip(line);
        }
        fixed << line;
    }
    close(QString());
    return fixed;
}

SL collapseRedundantReturns(const SL& lines)
{
    SL fixed;
    QString lastReal;
    for (const QString& line : lines) {
        if (pyStrip(line) == QLatin1String("return") && lastReal == QLatin1String("return")) continue;
        fixed << line;
        if (!pyStrip(line).isEmpty() && !pyLStrip(line).startsWith(QLatin1Char('#'))) lastReal = pyStrip(line);
    }
    return fixed;
}

QString cmdOf(const QString& stripped) { return normalizeCommand(firstWord(stripped)); }

SL repairSelfJumpSceneSplits(const SL& body, const Ctx& c)
{
    SL fixed;
    QString current, pending;
    bool hasPending = false;
    QSet<QString> seen;
    auto hasContent = [&](int start) {
        for (int k = start; k < body.size(); ++k) {
            const QString item = pyStrip(repairStoryLine(body[k], c.opt));
            if (item.isEmpty() || item.startsWith(QLatin1Char('@')) || item.startsWith(QLatin1Char('#'))) continue;
            if (item.startsWith(QLatin1Char(':')) || cmdOf(item) == QLatin1String("label")) return false;
            return true;
        }
        return false;
    };
    auto fresh = [&](const QString& b) {
        const QString base = c.sl(b.isEmpty() ? QStringLiteral("scene_next") : b);
        QString cand = base;
        for (int idx = 2; seen.contains(cand); ++idx) cand = QStringLiteral("%1_%2").arg(base).arg(idx);
        seen.insert(cand);
        return cand;
    };
    for (int index = 0; index < body.size(); ++index) {
        const QString raw = body[index];
        const QString stripped = pyStrip(repairStoryLine(raw, c.opt));
        const QString cmd = cmdOf(stripped);
        const bool special = stripped.startsWith(QLatin1Char('@')) || stripped.startsWith(QLatin1Char('#')) || stripped.startsWith(QLatin1Char(':'));
        if (hasPending && !stripped.isEmpty() && !special && cmd != QLatin1String("label")) {
            if (!fixed.isEmpty() && !pyStrip(fixed.last()).isEmpty()) fixed << QString();
            fixed << QStringLiteral(": ") + pending;
            current = pending;
            hasPending = false;
        } else if (hasPending && (stripped.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label"))) {
            hasPending = false;
        }
        if (stripped.startsWith(QLatin1Char(':'))) {
            current = c.sl(pyStrip(stripped.mid(1)));
            if (!current.isEmpty()) seen.insert(current);
        } else if (cmd == QLatin1String("label") && stripped.contains(QLatin1Char(' '))) {
            current = c.sl(pyStrip(pySplit(stripped, 1).value(1)));
            if (!current.isEmpty()) seen.insert(current);
        }
        if (cmd == QLatin1String("jump") && !current.isEmpty() && stripped.contains(QLatin1Char(' '))) {
            const QString target = pyStrip(pySplit(stripped, 1).value(1));
            if (c.sl(target) == current) {
                const QString prefix = raw.left(raw.size() - pyLStrip(raw).size());
                if (hasContent(index + 1)) {
                    const QString nt = fresh(current + QStringLiteral("_next"));
                    fixed << prefix + U("переход ") + nt;
                    pending = nt;
                    hasPending = true;
                } else {
                    fixed << prefix + U("конецсцены");
                }
                continue;
            }
        }
        if (stripped == QLatin1String("return")) {
            fixed << raw.left(raw.size() - pyLStrip(raw).size()) + U("конецсцены");
            continue;
        }
        fixed << raw;
    }
    return fixed;
}

SL removeEyesopenBeforeJump(const SL& body, const Ctx& c)
{
    SL fixed;
    for (int i = 0; i < body.size(); ++i) {
        if (cmdOf(pyStrip(repairStoryLine(body[i], c.opt))) == QLatin1String("eyesopen")) {
            int j = i + 1;
            while (j < body.size() && pyStrip(repairStoryLine(body[j], c.opt)).isEmpty()) ++j;
            if (j < body.size() && cmdOf(pyStrip(repairStoryLine(body[j], c.opt))) == QLatin1String("jump")) continue;
        }
        fixed << body[i];
    }
    return fixed;
}

SL forceNoneTransitionsWhileEyesClosed(const SL& body, const Ctx& c)
{
    SL fixed;
    bool closed = false;
    QString carry;
    bool hasCarry = false;
    static const QSet<QString> visual{QStringLiteral("bg"), QStringLiteral("showbg"), QStringLiteral("cg"), QStringLiteral("show"),
                                      QStringLiteral("hide"), QStringLiteral("scene")};
    for (const QString& raw : body) {
        const QString stripped = pyStrip(repairStoryLine(raw, c.opt));
        if (stripped.isEmpty() || stripped.startsWith(QLatin1Char('#'))) { fixed << raw; continue; }
        const SL parts = words(stripped);
        if (parts.isEmpty()) { fixed << raw; continue; }
        const QString rawCmd = parts[0];
        const QString cmd = normalizeCommand(rawCmd);
        const QString rest = pyStrip(stripped.mid(rawCmd.size()));
        QString currentLabel;
        if (stripped.startsWith(QLatin1Char(':'))) currentLabel = c.sl(pyStrip(stripped.mid(1)));
        else if (cmd == QLatin1String("label")) currentLabel = c.sl(rest);
        if (hasCarry) {
            if (currentLabel == carry) { closed = true; hasCarry = false; }
            else { fixed << raw; continue; }
        }
        if (cmd == QLatin1String("eyesclose")) { closed = true; fixed << raw; continue; }
        if (cmd == QLatin1String("eyesopen")) { closed = false; fixed << raw; continue; }
        if (closed && cmd == QLatin1String("jump")) {
            const QString t = c.sl(rest);
            if (!t.isEmpty()) { carry = t; hasCarry = true; closed = false; }
            fixed << raw;
            continue;
        }
        if (closed && (cmd == QLatin1String("return") || cmd == QLatin1String("sceneend") || cmd == QLatin1String("gameend"))) {
            closed = false;
            hasCarry = false;
            fixed << raw;
            continue;
        }
        if (closed && visual.contains(cmd)) {
            SL rw = words(rest);
            if (rw.size() >= 2 && rw.at(rw.size() - 2).toLower() == QLatin1String("with") && isEffect(rw.last())) {
                rw = rw.mid(0, rw.size() - 2);
                fixed << rawCmd + QLatin1Char(' ') + joinW(rw);
                continue;
            }
            if (!rw.isEmpty() && isEffect(rw.last()) && rw.last() != QLatin1String("none")) {
                rw.last() = QStringLiteral("none");
                fixed << rawCmd + QLatin1Char(' ') + joinW(rw);
                continue;
            }
        }
        fixed << raw;
    }
    return fixed;
}

QString appendBlock(SL& out, const char* const* arr, const QString& modId)
{
    for (int i = 0; arr[i]; ++i) out << QString::fromUtf8(arr[i]).replace(QStringLiteral("@@MODID@@"), modId);
    return {};
}

} // namespace

// ================================================================== public vocabulary

const QHash<QString, QString>& renames() { return T().renames; }
const QHash<QString, QString>& speakers() { return T().speakers; }

// V1-only commands. Kept apart from the old builder's RENAMES so those tables stay byte-equal to it.
const QHash<QString, QString>& v1Renames()
{
    static const QHash<QString, QString> m{
        {U("запомнит"), QStringLiteral("remember")}, {U("запомнила"), QStringLiteral("remember")}, {U("запомнил"), QStringLiteral("remember")},
        {QStringLiteral("remember"), QStringLiteral("remember")},
        {U("шкала"), QStringLiteral("meter")}, {QStringLiteral("meter"), QStringLiteral("meter")},
        {U("шкалы"), QStringLiteral("meters")}, {U("отношения"), QStringLiteral("meters")}, {QStringLiteral("meters"), QStringLiteral("meters")},
        {U("лучшаяшкала"), QStringLiteral("bestmeter")}, {U("получшейшкале"), QStringLiteral("bestmeter")}, {QStringLiteral("bestmeter"), QStringLiteral("bestmeter")},
        {U("убратьпредмет"), QStringLiteral("itemremove")}, {U("забратьпредмет"), QStringLiteral("itemremove")}, {QStringLiteral("itemremove"), QStringLiteral("itemremove")},
        {U("инвентарь"), QStringLiteral("inventory")}, {QStringLiteral("inventory"), QStringLiteral("inventory")},
        {U("менюмода"), QStringLiteral("modmenu")}, {U("главноеменю"), QStringLiteral("modmenu")}, {QStringLiteral("modmenu"), QStringLiteral("modmenu")},
        {U("конецменюмода"), QStringLiteral("endmodmenu")}, {QStringLiteral("endmodmenu"), QStringLiteral("endmodmenu")},
        {U("галерея"), QStringLiteral("gallery")}, {QStringLiteral("gallery"), QStringLiteral("gallery")},
        {U("достижения"), QStringLiteral("achievements")}, {QStringLiteral("achievements"), QStringLiteral("achievements")},
        {U("новаяглава"), QStringLiteral("newchapter")}, {QStringLiteral("newchapter"), QStringLiteral("newchapter")},
        {U("главы"), QStringLiteral("chapters")}, {QStringLiteral("chapters"), QStringLiteral("chapters")},
        {U("окно"), QStringLiteral("sayskin")}, {U("окнодиалога"), QStringLiteral("sayskin")}, {U("скинокна"), QStringLiteral("sayskin")},
        {QStringLiteral("sayskin"), QStringLiteral("sayskin")},
        {U("флешбэк"), QStringLiteral("flashback")}, {U("флешбек"), QStringLiteral("flashback")}, {U("флэшбек"), QStringLiteral("flashback")},
        {QStringLiteral("flashback"), QStringLiteral("flashback")},
        {U("конецфлешбэка"), QStringLiteral("endflashback")}, {U("конецфлешбека"), QStringLiteral("endflashback")},
        {QStringLiteral("endflashback"), QStringLiteral("endflashback")},
        {U("память"), QStringLiteral("memories")}, {U("журналпамяти"), QStringLiteral("memories")}, {QStringLiteral("memories"), QStringLiteral("memories")},
        {U("параллакс"), QStringLiteral("parallax")}, {U("3д"), QStringLiteral("parallax")}, {QStringLiteral("3d"), QStringLiteral("parallax")},
        {QStringLiteral("parallax"), QStringLiteral("parallax")},
        {U("табло"), QStringLiteral("splitflap")}, {QStringLiteral("splitflap"), QStringLiteral("splitflap")},
        {U("фото"), QStringLiteral("phonephoto")}, {U("селфи"), QStringLiteral("phonephoto")}, {QStringLiteral("phonephoto"), QStringLiteral("phonephoto")},
        {U("голосовое"), QStringLiteral("phonevoice")}, {QStringLiteral("phonevoice"), QStringLiteral("phonevoice")},
        {U("звонок"), QStringLiteral("phonecall")}, {QStringLiteral("phonecall"), QStringLiteral("phonecall")},
        {U("пост"), QStringLiteral("phonepost")}, {QStringLiteral("phonepost"), QStringLiteral("phonepost")},
        {U("лента"), QStringLiteral("phonefeed")}, {QStringLiteral("phonefeed"), QStringLiteral("phonefeed")},
        {U("пуш"), QStringLiteral("phonepush")}, {U("пушуведомление"), QStringLiteral("phonepush")}, {QStringLiteral("phonepush"), QStringLiteral("phonepush")},
        {U("коммент"), QStringLiteral("phonecomment")}, {U("комментарий"), QStringLiteral("phonecomment")},
        {QStringLiteral("phonecomment"), QStringLiteral("phonecomment")},
        {U("рабочийстол"), QStringLiteral("phonehome")}, {QStringLiteral("phonehome"), QStringLiteral("phonehome")},
        {U("телефон"), QStringLiteral("phonestart")}, {U("открытьтелефон"), QStringLiteral("phonestart")},
        {U("закрытьтелефон"), QStringLiteral("phoneend")},
    };
    return m;
}

QString normalizeCommand(const QString& word)
{
    const QString key = word.toLower();
    const auto it = T().renames.constFind(key);
    return it != T().renames.constEnd() ? *it : v1Renames().value(key, key);
}

bool isCommandName(const QString& id)
{
    static const QSet<QString> v1 = [] {       // thread-safe init: the preview renders on a worker thread
        QSet<QString> s;
        for (const QString& c : v1Renames()) s.insert(c);
        return s;
    }();
    return T().commandNames.contains(id) || v1.contains(id);
}
bool isEffect(const QString& w) { return T().effects.contains(w); }
bool isPosition(const QString& w) { return T().positions.contains(w); }
bool isWalkPosition(const QString& w) { return T().walk.contains(w); }
double walkXalign(const QString& w) { return T().walk.value(w, 0.5); }

QString slugOf(const QString& s, const QString& fallback, const CompileOptions& opt) { return slug(s, fallback, opt.legacy); }

QString persistentKey(const QString& modId, const QString& name, const QString& fallback, const CompileOptions& opt)
{
    const QString key = slugOf(name, fallback, opt);
    const QString prefix = QStringLiteral("genry_") + slugOf(modId, QStringLiteral("mod"), opt);
    if (key.startsWith(prefix + QLatin1Char('_'))) return key;
    return prefix + QLatin1Char('_') + key;
}

QString sceneLabel(const QString& modId, const QString& name, const CompileOptions& opt)
{
    const CompileState st;
    const Ctx c{opt, modId, nullptr};
    return c.lab(name);
}

QString relModPath(const QString& modId, const QString& subpath)
{
    QString clean = subpath;
    clean.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (clean.startsWith(QLatin1Char('/'))) clean.remove(0, 1);
    if (clean.startsWith(QLatin1String("mods/"))) return clean;
    return QStringLiteral("mods/%1/%2").arg(modId, clean);
}

QString speakerId(const QString& name, const CompileState& st, const CompileOptions& opt)
{
    const QString key = pyStrip(name).toLower();
    if (st.speakers.contains(key)) return st.speakers.value(key);
    if (T().speakers.contains(key)) return T().speakers.value(key);
    const QString id = slugOf(key, QStringLiteral("genry"), opt);
    if (!opt.legacy && !opt.knownSpeakers.isEmpty() && !opt.knownSpeakers.contains(id) && id != QLatin1String("genry") &&
        id != QLatin1String("scar"))
        return QStringLiteral("genry_sp_") + id;
    return id;
}

QStringList weatherKeys()
{
    QStringList k;
    for (const auto& kv : T().weather) k << kv.first;
    return k;
}
QString weatherKey(const QString& word) { return T().weatherAliases.value(word.toLower()); }
QStringList filterKeys()
{
    QStringList k;
    for (const auto& kv : T().filters) k << kv.first;
    return k;
}
QString filterKey(const QString& word) { return T().filterAliases.value(word.toLower()); }

// ================================================================== meta / playable

ModMeta parseMeta(const QStringList& lines, QStringList* body, const CompileOptions& opt)
{
    ModMeta m;
    QStringList b;
    for (const QString& raw : lines) {
        const QString line = pyStrip(raw);
        if (line.startsWith(QLatin1Char('@'))) {
            const SL parts = pySplit(line.mid(1), 1);
            if (parts.size() == 2) {
                const QString key = pyStrip(parts[0]).toLower(), value = pyStrip(parts[1]);
                if (key == QLatin1String("mod_id")) m.modId = value;
                else if (key == QLatin1String("mod_name")) m.modName = value;
                else if (key == QLatin1String("author")) m.author = value;
                else if (key == QLatin1String("mod_title_font")) m.titleFont = value;
                else if (key == QLatin1String("mod_title_color")) m.titleColor = value;
                else if (key == QLatin1String("mod_title_size")) m.titleSize = value;
                else if (key == QLatin1String("mod_title_style")) m.titleStyle = value;
            }
            continue;
        }
        b << raw;
    }
    m.modId = slugOf(m.modId, QStringLiteral("genry_easy_mod"), opt);
    if (body) *body = b;
    return m;
}

bool hasPlayableBody(const QStringList& body, const CompileOptions& opt)
{
    static const QSet<QString> structural{QStringLiteral("character"), QStringLiteral("variable"), QStringLiteral("setvar"),
                                          QStringLiteral("addvar"), QStringLiteral("label")};
    for (const QString& raw : body) {
        const QString s = pyStrip(repairStoryLine(raw, opt));
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@')) || s.startsWith(QLatin1Char(':'))) continue;
        if (s.contains(QLatin1Char(':')) && !isCommandName(cmdOf(s)) && !s.toLower().startsWith(QLatin1String("http:")) &&
            !s.toLower().startsWith(QLatin1String("https:")))
            return true;
        const QString cmd = cmdOf(s);
        if (structural.contains(cmd)) continue;
        if (isCommandName(cmd)) return true;
    }
    return false;
}

// ================================================================== compile_line

// V1 options: after the positional fields every command may take «ключ=значение» items
// (and some bare switch words), e.g. «запомнит Алиса | Ты помог | флаг=помог | где=справа».
// Keys have Russian and English spellings; each command reads the ones it knows.
QString v1OptKey(const QString& k)
{
    static const QHash<QString, QString> keys{
        {U("текст"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("text")},
        {U("флаг"), QStringLiteral("flag")}, {U("запись"), QStringLiteral("flag")}, {QStringLiteral("flag"), QStringLiteral("flag")},
        {U("лицо"), QStringLiteral("face")}, {U("иконка"), QStringLiteral("face")}, {QStringLiteral("face"), QStringLiteral("face")},
        {QStringLiteral("icon"), QStringLiteral("face")},
        {U("где"), QStringLiteral("where")}, {U("позиция"), QStringLiteral("where")}, {QStringLiteral("where"), QStringLiteral("where")},
        {U("время"), QStringLiteral("time")}, {U("сек"), QStringLiteral("time")}, {QStringLiteral("time"), QStringLiteral("time")},
        {U("цвет"), QStringLiteral("color")}, {QStringLiteral("color"), QStringLiteral("color")},
        {U("звук"), QStringLiteral("sound")}, {QStringLiteral("sound"), QStringLiteral("sound")},
        {U("вид"), QStringLiteral("kind")}, {QStringLiteral("kind"), QStringLiteral("kind")},
        {U("старт"), QStringLiteral("start")}, {U("начало"), QStringLiteral("start")}, {QStringLiteral("start"), QStringLiteral("start")},
        {U("длительность"), QStringLiteral("duration")}, {QStringLiteral("duration"), QStringLiteral("duration")},
        {U("прозрачность"), QStringLiteral("alpha")}, {QStringLiteral("alpha"), QStringLiteral("alpha")},
        {U("тьма"), QStringLiteral("dim")}, {U("затемнение"), QStringLiteral("dim")}, {QStringLiteral("dim"), QStringLiteral("dim")},
        {U("музыка"), QStringLiteral("music")}, {QStringLiteral("music"), QStringLiteral("music")},
        {U("переход"), QStringLiteral("effect")}, {QStringLiteral("effect"), QStringLiteral("effect")},
    };
    return keys.value(pyStrip(k).toLower(), pyStrip(k).toLower());
}

V1Opts parseV1Opts(const QString& rest, const QHash<QString, QString>& switches)
{
    V1Opts o;
    for (const QString& raw : rest.split(QLatin1Char('|'))) {
        const QString p = pyStrip(raw);
        const int eq = int(p.indexOf(QLatin1Char('=')));
        if (eq > 0 && !p.left(eq).contains(QLatin1Char(' '))) { o.kv.insert(v1OptKey(p.left(eq)), pyStrip(p.mid(eq + 1))); continue; }
        const QString low = p.toLower();
        if (switches.contains(low)) { o.sw.insert(switches.value(low)); continue; }
        o.pos << p;
    }
    return o;
}

QString rememberWhere(const QString& where)
{
    const QString w = pyStrip(where).toLower();
    if (w == U("справа") || w == QLatin1String("right")) return QStringLiteral("tr");
    if (w == U("центр") || w == U("по центру") || w == U("сверху") || w == QLatin1String("center")) return QStringLiteral("tc");
    if (w == U("снизу") || w == U("внизу") || w == QLatin1String("bottom")) return QStringLiteral("bc");
    return QStringLiteral("tl");
}

// a character's face for popups: «dv smile pioneer» (or just «dv») -> a cropped head of the ES sprite
static QString faceDisplayable(const QString& face, const char* zoom = "0.2")
{
    QString s = face.simplified();
    if (s.isEmpty()) return QStringLiteral("None");
    if (!s.contains(QLatin1Char(' '))) s += QStringLiteral(" normal pioneer");
    return QStringLiteral("Transform(%1, crop=(300, 40, 300, 300), zoom=%2)").arg(pyQ(s), QLatin1String(zoom));
}

// the phone's clock: every message a minute later (the old builder's sms timing)
static QString nextPhoneTime(CompileState& st)
{
    int base = st.hasPhoneMin ? st.phoneMin : 14 * 60 + 25;
    base += 1;
    st.hasPhoneMin = true;
    st.phoneMin = base;
    return QStringLiteral("%1:%2").arg((base / 60) % 24, 2, 10, QLatin1Char('0')).arg(base % 60, 2, 10, QLatin1Char('0'));
}

static bool isMeWord(const QString& who)
{
    const QString wl = who.toLower();
    return wl == U("я") || wl == QLatin1String("me") || wl == QLatin1String("+") || wl == QLatin1String("i");
}

// Телефон 2.0 (V1): photos, voice messages, calls with branches, the camp feed. {} if not one of them.
static SL compileV1Phone(const QString& cmd, const QString& rest, const QString& modId, CompileState& st, const Ctx& c)
{
    auto media = [&](const QString& raw) {           // a file of the mod, or a Ren'Py image name («cg d1_food_normal»)
        const QString s = pyStrip(raw);
        return s.contains(QLatin1Char('/')) || pyBasename(s).contains(QLatin1Char('.')) ? relModPath(modId, s) : s;
    };
    auto split = [](const QString& r, QString* who, QString* body) {
        if (r.contains(QLatin1Char(':'))) {
            *who = pyStrip(r.section(QLatin1Char(':'), 0, 0));
            *body = pyStrip(r.section(QLatin1Char(':'), 1));
        } else {
            *who = QString();
            *body = pyStrip(r);
        }
    };
    auto message = [&](const QString& who, const QString& side, const QString& body, const QString& status) {
        const bool me = isMeWord(who);
        const QString name = me ? U("Я") : (who.isEmpty() ? QStringLiteral("???") : who);
        const QString append = QStringLiteral("    $ _genry_phone.append((%1, %2, %3, %4, %5))")
                                   .arg(pyQ((me ? QStringLiteral("me") : QStringLiteral("them")) + side), pyUQ(name), pyUQ(body), pyUQ(nextPhoneTime(st)), pyUQ(status));
        if (me) return SL{append, QStringLiteral("    $ renpy.pause(0.6)")};
        return SL{QStringLiteral("    $ _genry_phone_typing = ") + pyUQ(name), QStringLiteral("    $ renpy.pause(1.2)"),
                  QStringLiteral("    $ _genry_phone_typing = \"\""), append, QStringLiteral("    $ renpy.pause(0.6)")};
    };
    static const QSet<QString> homeWords{U("дом"), U("рабочийстол"), U("рабочий стол"), U("меню"), QStringLiteral("home")};
    if (cmd == QLatin1String("phonestart") && homeWords.contains(pyStrip(rest).toLower()))
        return compileV1Phone(QStringLiteral("phonehome"), QString(), modId, st, c);
    if (cmd == QLatin1String("phonestart")) {
        const QString contact = pyStrip(rest).isEmpty() ? U("СМС") : pyStrip(rest);
        st.hasPhoneMin = true;
        st.phoneMin = 14 * 60 + 25;
        return {QStringLiteral("    window hide"), QStringLiteral("    $ _genry_phone = []"), QStringLiteral("    $ _genry_phone_typing = \"\""),
                QStringLiteral("    $ _genry_phone_contact = ") + pyUQ(contact),
                QStringLiteral("    $ genry_phone_open = dict(genry_phone_open)"),      // reassigned -> saved with the game
                QStringLiteral("    show screen genry_phone2(_genry_phone, _genry_phone_contact)")};
    }
    if (cmd == QLatin1String("phoneend"))
        return {QStringLiteral("    hide screen genry_phone2"), QStringLiteral("    hide screen genry_phone"), QStringLiteral("    window show")};
    if (cmd == QLatin1String("phonephoto")) {
        // «фото Славя: cg d1_food_normal | Смотри, что нашла | 18+»
        QString who, body;
        split(rest, &who, &body);
        const SL p = body.split(QLatin1Char('|'));
        if (pyStrip(p.value(0)).isEmpty()) return {U("    # фото Кто: картинка | подпись | 18+")};
        QString caption;
        bool adult = false, once = false;
        for (int i = 1; i < p.size(); ++i) {
            const QString x = pyStrip(p[i]), xl = x.toLower();
            if (x == QLatin1String("18+") || x == QLatin1String("18") || xl == U("интим") || xl == U("скрыто")) adult = true;
            else if (xl == U("1раз") || xl == U("1 раз") || xl == U("одноразовое") || xl == U("одноразовая") || xl == QLatin1String("once")) once = true;
            else if (caption.isEmpty()) caption = x;
        }
        const QString img = media(p[0]);
        SL r = message(who, once ? QStringLiteral("_photo1") : adult ? QStringLiteral("_photo18") : QStringLiteral("_photo"), img, caption);
        if (!c.opt.legacy && !once)       // the phone's own gallery app keeps what the hero got
            r << QStringLiteral("    $ genry_ph_photos = genry_ph_photos + [(%1, %2)]").arg(pyUQ(img), pyUQ(caption));
        return r;
    }
    if (cmd == QLatin1String("phonepush")) {
        // «пуш Алиса: Ну и где ты шляешься?» - a notification banner at the top of the game screen
        QString who, body;
        split(rest, &who, &body);
        return {QStringLiteral("    show screen genry_ph_banner(%1, %2)").arg(pyUQ(who.isEmpty() ? U("Сообщение") : who), pyUQ(body))};
    }
    if (cmd == QLatin1String("phonecomment")) {
        // «коммент Алиса: Красиво!» - under the newest post of the feed
        QString who, body;
        split(rest, &who, &body);
        const QString feed = c.sys(QStringLiteral("feed"));
        return {QStringLiteral("    if %1:").arg(feed),
                QStringLiteral("        $ %1[0][\"comments\"] = %1[0].get(\"comments\", []) + [(%2, %3)]").arg(feed, pyUQ(who.isEmpty() ? QStringLiteral("???") : who), pyUQ(body))};
    }
    if (cmd == QLatin1String("phonehome")) {
        return {QStringLiteral("    window hide"), QStringLiteral("    call screen genry_phone_home(%1)").arg(c.sys(QStringLiteral("feed"))),
                QStringLiteral("    window show")};
    }
    if (cmd == QLatin1String("phonevoice")) {
        // «голосовое Славя: audio/slavya_01.ogg | 0:07»
        QString who, body;
        split(rest, &who, &body);
        const QString file = pyStrip(body.section(QLatin1Char('|'), 0, 0));
        if (file.isEmpty()) return {U("    # голосовое Кто: audio/файл.ogg | 0:07")};
        QString len = pyStrip(body.section(QLatin1Char('|'), 1, 1));
        if (len.isEmpty()) len = QStringLiteral("0:05");
        return message(who, QStringLiteral("_voice"), relModPath(modId, file), len);
    }
    if (cmd == QLatin1String("phonecall")) {
        // «звонок Славя | принять -> sl_talk | сбросить -> alone | нет ответа -> missed | ждать=10 | лицо=sl smile pioneer»
        const SL parts = rest.split(QLatin1Char('|'));
        const QString who = pyStrip(parts.value(0)).isEmpty() ? QStringLiteral("???") : pyStrip(parts.value(0));
        QString accept, decline, missed, face, wait = QStringLiteral("0");
        for (int i = 1; i < parts.size(); ++i) {
            const QString x = pyStrip(parts[i]);
            if (x.contains(QLatin1String("->"))) {
                const QString k = pyStrip(x.section(QStringLiteral("->"), 0, 0)).toLower(), t = pyStrip(x.section(QStringLiteral("->"), 1));
                if (k.startsWith(U("прин")) || k.startsWith(U("ответ")) || k == QLatin1String("accept")) accept = t;
                else if (k.startsWith(U("сброс")) || k.startsWith(U("откл")) || k == QLatin1String("decline")) decline = t;
                else missed = t;
            } else if (x.contains(QLatin1Char('='))) {
                const QString k = pyStrip(x.section(QLatin1Char('='), 0, 0)).toLower(), v = pyStrip(x.section(QLatin1Char('='), 1));
                if (k == U("лицо") || k == QLatin1String("face")) face = v;
                else if (k == U("ждать") || k == U("время") || k == QLatin1String("wait")) wait = cleanNumber(v, QStringLiteral("0"));
            }
        }
        const QString name = pyUQ(who);
        auto line = [&](const QString& text) {
            return QStringLiteral("        $ _genry_phone.append((\"call\", %1, %2, %3, u\"\"))").arg(name, pyUQ(text), pyUQ(nextPhoneTime(st)));
        };
        SL r{QStringLiteral("    $ _genry_ring = genry_resolve_audio_expr(\"sfx_home_phone_ring\")"), QStringLiteral("    if _genry_ring:"),
             QStringLiteral("        play sound genry_resolve_audio_expr(\"sfx_home_phone_ring\") loop"),
             QStringLiteral("    call screen genry_phone_call(%1, face=%2, secs=%3)").arg(name, faceDisplayable(face, "0.69"), wait),
             QStringLiteral("    stop sound"), QStringLiteral("    if _return == \"accept\":"),
             QStringLiteral("        $ _genry_ring = genry_resolve_audio_expr(\"sfx_home_phone_take\")"), QStringLiteral("        if _genry_ring:"),
             QStringLiteral("            play sound genry_resolve_audio_expr(\"sfx_home_phone_take\")")};
        r << line(U("Звонок · принят"));
        if (!accept.isEmpty()) r << QStringLiteral("        jump ") + c.lab(accept);
        r << QStringLiteral("    elif _return == \"decline\":") << line(U("Вызов отклонён"));
        if (!decline.isEmpty()) r << QStringLiteral("        jump ") + c.lab(decline);
        r << QStringLiteral("    else:") << line(U("Пропущенный вызов"));
        if (!missed.isEmpty()) r << QStringLiteral("        jump ") + c.lab(missed);
        return r;
    }
    if (cmd == QLatin1String("phonepost")) {
        // «пост Славя: Утро на пляже! | cg d1_food_normal | лайки=12»
        QString who, body;
        split(rest, &who, &body);
        const SL p = body.split(QLatin1Char('|'));
        QString text, img, likes = QStringLiteral("0");
        for (int i = 0; i < p.size(); ++i) {
            const QString x = pyStrip(p[i]);
            const QString k = x.section(QLatin1Char('='), 0, 0).trimmed().toLower();
            if (x.contains(QLatin1Char('=')) && (k == U("лайки") || k == QLatin1String("likes"))) likes = QString::number(x.section(QLatin1Char('='), 1).trimmed().toInt());
            else if (i == 0) text = x;
            else if (img.isEmpty() && !x.isEmpty()) img = media(x);
        }
        return {QStringLiteral("    $ %1.insert(0, {\"who\": %2, \"text\": %3, \"img\": %4, \"likes\": %5, \"liked\": False})")
                    .arg(c.sys(QStringLiteral("feed")), pyUQ(who.isEmpty() ? QStringLiteral("???") : who), pyUQ(text), img.isEmpty() ? QStringLiteral("None") : pyUQ(img), likes)};
    }
    if (cmd == QLatin1String("phonefeed")) {
        const QString title = pyStrip(rest).isEmpty() ? U("Лента лагеря") : pyStrip(rest);
        return {QStringLiteral("    window hide"), QStringLiteral("    call screen genry_phone_feed(%1, title=%2)").arg(c.sys(QStringLiteral("feed")), pyUQ(title)),
                QStringLiteral("    window show")};
    }
    return {};
}

// Телефон 3.0 pictures (PhoneAssets.cpp, written by the builder) + the cast's round faces for avatars
static SL v1PhoneImages(const QString& modId)
{
    SL r{QString()};
    for (const QString& n : phoneAssetNames())
        r << QStringLiteral("image genry_ph %1 = %2").arg(n, pyQ(relModPath(modId, QStringLiteral("images/genry_phone/") + n + QStringLiteral(".png"))));
    SL ava;
    for (const QString& id : esChibiIds()) {
        const QString f = esChibiFile(id);
        if (f.isEmpty() || id == QLatin1String("?")) continue;
        r << QStringLiteral("image genry_chibi %1 = %2").arg(f, pyQ(relModPath(modId, QStringLiteral("images/genry_chibi/") + f + QStringLiteral(".png"))));
        ava << QStringLiteral("%1: \"genry_chibi %2\"").arg(pyUQ(id), f);
    }
    for (auto it = speakers().begin(); it != speakers().end(); ++it) {       // «Алиса» -> dv -> her face
        const QString f = esChibiFile(it.value());
        if (!f.isEmpty() && it.value() != QLatin1String("?")) ava << QStringLiteral("%1: \"genry_chibi %2\"").arg(pyUQ(it.key()), f);
    }
    ava.sort();
    r << QStringLiteral("init 3 python:") << QStringLiteral("    genry_ph_ava.update({%1})").arg(ava.join(QStringLiteral(", ")));
    return r;
}

// V1-only features: Telltale «запомнит», relationship meters, a real inventory, the mod's
// gallery and achievements. Returns {} when `cmd` is not one of them (the old code handles it).
static SL compileV1Feature(const QString& cmd, const QString& rest, const QString& modId, CompileState& st, const Ctx& c)
{
    const QString arg0 = firstWord(rest).toLower();
    const bool asButton = arg0 == U("кнопка") || arg0 == QLatin1String("button");
    const bool asHide = arg0 == U("скрыть") || arg0 == U("убрать") || arg0 == QLatin1String("hide");
    if (cmd == QLatin1String("addvar")) {
        const SL p = words(rest);
        if (p.size() < 2) return {};
        const QString v = c.var(p[0]);
        SL r{QStringLiteral("    $ ") + v + QStringLiteral(" += ") + p[1]};
        const auto m = st.meters.constFind(v);
        if (m != st.meters.constEnd())        // a meter: clamp it and show «Алиса +1» with its bar
            r << QStringLiteral("    $ genry_meter_ping(") + pyUQ(m->value(0)) + QStringLiteral(", ") + p[1] + QStringLiteral(", ") + pyQ(m->value(1)) +
                     QStringLiteral(", ") + pyQ(v) + QStringLiteral(", ") + m->value(2) + QStringLiteral(", ") + m->value(3) + QLatin1Char(')');
        return r;
    }
    if (cmd == QLatin1String("remember")) {
        // «запомнит Алиса | свой текст | флаг=помог | лицо=dv smile pioneer | где=справа | время=5 | цвет=#ff7a00 | звук=sfx_x | тихо»
        static const QHash<QString, QString> sw{{U("тихо"), QStringLiteral("quiet")}, {QStringLiteral("quiet"), QStringLiteral("quiet")}};
        const V1Opts o = parseV1Opts(rest, sw);
        const QString who = o.pos.value(0);
        QString what = o.get(QStringLiteral("text"), o.pos.value(1));
        if (what.isEmpty()) what = who.isEmpty() ? U("Это запомнят.") : who + U(" это запомнит.");
        const QString color = pyIsHexColor(o.get(QStringLiteral("color"))) ? o.get(QStringLiteral("color")) : QStringLiteral("#ffd27d");
        SL r;
        const QString flag = o.get(QStringLiteral("flag"));
        if (!flag.isEmpty()) r << QStringLiteral("    $ ") + c.var(flag) + QStringLiteral(" = True");      // «если помог -> …» later
        r << QStringLiteral("    $ %1.append((%2, %3))").arg(c.sys(QStringLiteral("memories")), pyUQ(who), pyUQ(what));
        r << QStringLiteral("    show screen genry_remember(") + pyUQ(what) + QStringLiteral(", icon=") + faceDisplayable(o.get(QStringLiteral("face"))) +
                 QStringLiteral(", where=") + pyQ(rememberWhere(o.get(QStringLiteral("where")))) + QStringLiteral(", secs=") +
                 cleanNumber(o.get(QStringLiteral("time")), QStringLiteral("3.4")) + QStringLiteral(", accent=") + pyQ(color) + QLatin1Char(')');
        const QString sound = o.get(QStringLiteral("sound"));
        if (!o.has(QStringLiteral("quiet")) && !sound.isEmpty()) {
            if (sound.contains(QLatin1Char('/')) || sound.contains(QLatin1Char('.'))) r << QStringLiteral("    play sound ") + pyQ(relModPath(modId, sound));
            else r << QStringLiteral("    play sound genry_resolve_audio_expr(%1)").arg(pyQ(sound));
        }
        return r;
    }
    if (cmd == QLatin1String("memories"))
        return {QStringLiteral("    call screen genry_memories(") + c.sys(QStringLiteral("memories")) + QStringLiteral(", called=True)")};
    if (cmd == QLatin1String("meter")) return {};        // «шкала» is declared by compileStory's pre-pass
    if (cmd == QLatin1String("meters")) {
        if (asButton) return {QStringLiteral("    show screen genry_meters_button(") + c.sys(QStringLiteral("meters")) + QLatin1Char(')')};
        if (asHide) return {QStringLiteral("    hide screen genry_meters_button")};
        return {QStringLiteral("    call screen genry_meters(") + c.sys(QStringLiteral("meters")) + QStringLiteral(", called=True)")};
    }
    if (cmd == QLatin1String("bestmeter")) {
        // лучшаяшкала симпатия_алисы -> alisa_end | симпатия_слави -> slavya_end | иначе -> bad_end
        SL pairs, jumps;
        QString otherwise;
        for (const QString& part : rest.split(QLatin1Char('|'))) {
            if (!part.contains(QLatin1String("->"))) continue;
            const QString left = pyStrip(part.section(QStringLiteral("->"), 0, 0));
            const QString target = c.lab(pyStrip(part.section(QStringLiteral("->"), 1)));
            const QString l = left.toLower();
            if (l == U("иначе") || l == QLatin1String("else") || l == U("ничья")) { otherwise = target; continue; }
            pairs << QStringLiteral("(%1, %2)").arg(pyQ(target), c.var(left));
            jumps << QStringLiteral("    if _genry_best == ") + pyQ(target) + QLatin1Char(':') << QStringLiteral("        jump ") + target;
        }
        if (pairs.isEmpty()) return {U("    # лучшаяшкала: шкала -> сцена | шкала -> сцена | иначе -> сцена")};
        SL r{QStringLiteral("    $ _genry_best = genry_best_of([") + pairs.join(QStringLiteral(", ")) + QStringLiteral("])")};
        r << jumps;
        if (!otherwise.isEmpty()) r << QStringLiteral("    jump ") + otherwise;
        return r;
    }
    const QString inv = c.sys(QStringLiteral("inv"));
    auto itemKey = [&](const QString& raw) { return pyQ(c.sl(pyStrip(raw), "item")); };
    if (cmd == QLatin1String("itemget")) {
        // V1: the inventory belongs to the playthrough (the old builder set a persistent flag,
        // so a key picked up once stayed in every new game)
        const SL p = c.pipes(rest);
        if (p.isEmpty() || pyStrip(p[0]).isEmpty()) return {};
        const QString key = itemKey(p[0]);
        const QString caption = p.size() > 1 && !pyStrip(p[1]).isEmpty() ? pyStrip(p[1]) : pyStrip(p[0]);
        SL r{QStringLiteral("    if %1 not in %2:").arg(key, inv), QStringLiteral("        $ %1.append(%2)").arg(inv, key)};
        r << compileNotifyPopup(U("Предмет: %1 | Инвентарь | | #143247 | справа | 2.7").arg(caption), modId, c);
        return r;
    }
    if (cmd == QLatin1String("itemremove")) {
        if (pyStrip(rest).isEmpty()) return {U("    # убратьпредмет key")};
        const QString key = itemKey(pyStrip(rest).section(QLatin1Char('|'), 0, 0));
        return {QStringLiteral("    if %1 in %2:").arg(key, inv), QStringLiteral("        $ %1.remove(%2)").arg(inv, key)};
    }
    if (cmd == QLatin1String("ifitem") && rest.contains(QLatin1String("->"))) {
        const SL ct = pySplitSep(rest, QStringLiteral("->"), 1);
        return {QStringLiteral("    if %1 in %2:").arg(itemKey(ct[0]), inv), QStringLiteral("        jump ") + c.lab(pyStrip(ct[1]))};
    }
    if (cmd == QLatin1String("inventory")) {
        const QString items = c.sys(QStringLiteral("items"));
        if (asButton) return {QStringLiteral("    show screen genry_inventory_button(%1, %2)").arg(items, pyQ(inv))};
        if (asHide) return {QStringLiteral("    hide screen genry_inventory_button")};
        return {QStringLiteral("    call screen genry_inventory(%1, %2, called=True)").arg(items, pyQ(inv))};
    }
    if (cmd == QLatin1String("newchapter")) {
        // «новаяглава Название | картинка»: opens the chapter for the mod's chapter screen + a title card
        const QString title = pyStrip(rest.section(QLatin1Char('|'), 0, 0));
        if (title.isEmpty()) return {U("    # новаяглава Название | картинка")};
        SL r{QStringLiteral("    $ persistent.%1 = True").arg(persistentKey(modId, QStringLiteral("ch_") + title, QStringLiteral("chapter"), c.opt))};
        r << compileTitlecard(title, !c.opt.legacy, st.eyesClosed);
        return r;
    }
    if (cmd == QLatin1String("chapters"))
        return {QStringLiteral("    call screen genry_chapters(") + c.sys(QStringLiteral("chapters")) + QStringLiteral(", called=True)"),
                QStringLiteral("    if _return:"), QStringLiteral("        jump expression _return")};
    if (cmd == QLatin1String("sayskin")) {
        static const QHash<QString, QString> skins{{U("нуар"), QStringLiteral("noir")}, {QStringLiteral("noir"), QStringLiteral("noir")},
                                                   {U("кино"), QStringLiteral("kino")}, {U("субтитры"), QStringLiteral("kino")}, {QStringLiteral("kino"), QStringLiteral("kino")},
                                                   {U("пиксель"), QStringLiteral("pixel")}, {U("ретро"), QStringLiteral("pixel")}, {QStringLiteral("pixel"), QStringLiteral("pixel")},
                                                   {U("книга"), QStringLiteral("book")}, {U("бумага"), QStringLiteral("book")}, {QStringLiteral("book"), QStringLiteral("book")}};
        const QString skin = skins.value(arg0);
        return {QStringLiteral("    $ genry_say_skin = ") + (skin.isEmpty() ? QStringLiteral("None") : pyQ(skin))};
    }
    if (cmd == QLatin1String("flashback")) {
        // сепия + помехи + киношные полосы разом; «флешбэк ч/б» - в чёрно-белом
        st.activePrologueDream = true;
        SL r{QStringLiteral("    show screen genry_flashback_frame")};
        r << compileColorFilter(arg0 == U("чб") || arg0 == U("ч/б") ? U("чб") : U("сепия"));
        r << compileMemoryEffect(QStringLiteral("staticfx"), QStringLiteral("0.25"));
        return r;
    }
    if (cmd == QLatin1String("endflashback")) {
        st.activePrologueDream = false;
        SL r{QStringLiteral("    hide screen genry_flashback_frame")};
        r << compileColorFilter(U("нет"));
        r << compileClearOverlays();
        return r;
    }
    if (cmd == QLatin1String("parallax")) {
        // «параллакс» / «параллакс 0.5» / «параллакс выкл»
        double s = 1.0;
        static const QSet<QString> off{U("выкл"), U("стоп"), U("нет"), U("убрать"), QStringLiteral("off"), QStringLiteral("0")};
        if (off.contains(arg0)) s = 0.0;
        else if (!arg0.isEmpty()) {
            double d;
            if (pyFloat(QString(arg0).replace(QLatin1Char(','), QLatin1Char('.')), &d)) s = std::max(0.1, std::min(3.0, d));
        }
        return {QStringLiteral("    $ genry_parallax = ") + QString::number(s, 'g', 3)};
    }
    if (cmd == QLatin1String("splitflap")) {
        // «табло Первый день | 5»: the text flips in letter by letter, stays, fades
        const QString text = pyStrip(rest.section(QLatin1Char('|'), 0, 0)).toUpper();
        if (text.isEmpty()) return {U("    # табло Текст | секунды")};
        const QString secs = cleanNumber(pyStrip(rest.section(QLatin1Char('|'), 1, 1)), QString::number(0.45 + text.size() * 0.06 + 1.8, 'f', 1));
        return {QStringLiteral("    window hide"),
                QStringLiteral("    show expression DynamicDisplayable(genry_flap_frame, text=%1) as genry_flap:").arg(pyUQ(text)),
                QStringLiteral("        xalign 0.5"), QStringLiteral("        yalign 0.45"), QStringLiteral("    with dissolve"),
                QStringLiteral("    play sound \"sound/sfx/click_1.ogg\""), QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(secs),
                QStringLiteral("    hide genry_flap"), QStringLiteral("    with dissolve"), QStringLiteral("    window show")};
    }
    if (cmd == QLatin1String("gallery")) return {QStringLiteral("    call screen genry_gallery(") + c.sys(QStringLiteral("cgs")) + QStringLiteral(", called=True)")};
    if (cmd == QLatin1String("achievements"))
        return {QStringLiteral("    call screen genry_achievements(") + c.sys(QStringLiteral("achievements")) + QStringLiteral(", called=True)")};
    return {};
}

static QStringList compileLineCore(const QString& line, const QString& modId, CompileState& st, const CompileOptions& opt)
{
    const Ctx c{opt, modId, &st.modVars, st.modMenu};
    const QString stripped = pyStrip(repairStoryLine(line, opt));
    if (stripped.isEmpty() || stripped.startsWith(QLatin1Char('#'))) return {};

    if (stripped.startsWith(QLatin1Char(':'))) {
        QString name = c.sl(pyStrip(stripped.mid(1)));
        name = name == QLatin1String("start") && !(st.modMenu && !opt.legacy) ? c.sl(modId, "genry_mod") : c.lab(pyStrip(stripped.mid(1)));
        st.eyesClosed = st.autoOpen = false;
        st.timeSynced = false;
        return {QString(), QStringLiteral("label %1:").arg(name)};
    }
    const QString low = stripped.toLower();
    if (stripped.contains(QLatin1Char(':')) && !isCommandName(cmdOf(stripped)) && !low.startsWith(QLatin1String("http:")) &&
        !low.startsWith(QLatin1String("https:"))) {
        const SL st2 = pySplitSep(stripped, QStringLiteral(":"), 1);
        return {I4 + speakerFor(st2[0], st, opt) + QLatin1Char(' ') + pyQ(pyStrip(st2[1]))};
    }

    const SL parts = words(stripped);
    if (parts.isEmpty()) return {};
    const QString cmd = normalizeCommand(parts[0]);
    const QString rest = pyStrip(stripped.mid(parts[0].size()));

    if (cmd == QLatin1String("label")) {
        QString name = c.sl(rest);
        name = name == QLatin1String("start") ? c.sl(modId, "genry_mod") : c.lab(rest);
        return {QString(), QStringLiteral("label %1:").arg(name)};
    }
    if (cmd == QLatin1String("say")) return {I4 + pyQ(rest)};
    if (cmd == QLatin1String("screenmenu")) return compileScreenMenu(rest, modId, c);

    auto withEffectLine = [](QString line, const QString& effect) {
        if (!effect.isEmpty() && effect != QLatin1String("none")) line += QStringLiteral(" with ") + effect;
        return line;
    };
    if (cmd == QLatin1String("bg") || cmd == QLatin1String("showbg") || cmd == QLatin1String("cg")) {
        SL w = words(rest);
        QString effect;
        if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
        QString image = joinW(w);
        const QString lowImg = pyStrip(image).toLower();
        const bool plain = lowImg == QLatin1String("black") || lowImg == QLatin1String("white");
        SL timeLines;
        if (!opt.legacy && cmd != QLatin1String("cg") && !plain) {
            // V1: a night heroine on a day beach no more. The story said «ночь» and the picture is a day one -> the
            // game's night version of the same place when there is one; otherwise the heroines follow the picture.
            QString t = bgTimeOf(image);
            if (!t.isEmpty() && st.timeExplicit && t != st.timeOfDay) {
                const QString other = bgAtTime(image, st.timeOfDay);
                if (!other.isEmpty()) {
                    image = other;
                    t = st.timeOfDay;
                }
            }
            if (!t.isEmpty() && (t != st.timeOfDay || !st.timeSynced)) {
                timeLines = compileTimeOfDay(t);        // a scene opened from the app mid-mod gets the right colours too
                if (t != st.timeOfDay) st.timeExplicit = false;
                st.timeOfDay = t;
                st.timeSynced = true;
            }
        }
        QString out;
        if (cmd == QLatin1String("cg")) out = QStringLiteral("    scene cg ") + image;
        else if (cmd == QLatin1String("bg")) out = plain ? QStringLiteral("    scene ") + image : QStringLiteral("    scene bg ") + image;
        else out = plain ? QStringLiteral("    show ") + image : QStringLiteral("    show bg ") + image;
        if (cmd != QLatin1String("showbg")) {
            st.activePrologueDream = false;
            st.activeSpriteTags.clear();
        }
        return timeLines << withEffectLine(out, effect);
    }
    if (cmd == QLatin1String("scene")) {
        st.activePrologueDream = false;
        st.activeSpriteTags.clear();
        return {QStringLiteral("    scene ") + rest};
    }
    if (cmd == QLatin1String("show")) {
        SL w = words(rest);
        QString effect, pos;
        if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
        if (w.size() >= 2 && w.at(w.size() - 2) == QLatin1String("at") && isPosition(w.last())) {
            pos = w.takeLast();
            w.removeLast();
        } else if (!w.isEmpty() && isPosition(w.last())) {
            pos = w.takeLast();
        }
        QString image = pyStrip(joinW(w));
        if (!opt.legacy) {       // V1 «показать dv smile pioneer румянец пот»: the sprite with its overlay picture
            image = withOverlays(image);
            if (image.contains(QLatin1String(" genry_ov_"))) st.overlayImages.insert(image);
            // as ES writes it: a new face melts in (dspr), a heroine walks in with dissolve - no snapping.
            // «none» after the sprite keeps it instant.
            if (effect.isEmpty())
                effect = st.activeSpriteTags.contains(image.section(QLatin1Char(' '), 0, 0)) ? QStringLiteral("dspr") : QStringLiteral("dissolve");
        }
        const QString out = withEffectLine(QStringLiteral("    show ") + image + spriteShowAtClause(image, pos), effect);
        rememberSpriteTag(st, image);
        return {out};
    }
    if (cmd == QLatin1String("walk")) return compileWalk(rest);
    if (cmd == QLatin1String("bigshow")) return compileBigShow(rest);
    if (cmd == QLatin1String("mirror")) return compileMirrorShow(rest, QStringLiteral("1.0"), c);
    if (cmd == QLatin1String("mirrorbig")) return compileMirrorShow(rest, QStringLiteral("1.25"), c);
    if (cmd == QLatin1String("conflictfocus")) return compileConflictFocus(rest, c);
    if (cmd == QLatin1String("fullheight")) return compileFullheight(rest, c);
    if (cmd == QLatin1String("crowdshow")) return compileCrowdShow(rest);
    if (cmd == QLatin1String("enterleft")) return compileEnterShow(rest, true);
    if (cmd == QLatin1String("enterright")) return compileEnterShow(rest, false);
    if (cmd == QLatin1String("zoomshow")) return compileZoomShow(rest);
    if (cmd == QLatin1String("dim")) {
        const QString a = cleanNumber(rest.isEmpty() ? QStringLiteral("0.55") : rest, QStringLiteral("0.55"));
        return {QStringLiteral("    show genry_black:"), QStringLiteral("        alpha ") + a, QStringLiteral("    with dissolve")};
    }
    if (cmd == QLatin1String("undim")) return {QStringLiteral("    hide genry_black"), QStringLiteral("    with dissolve")};
    if (cmd == QLatin1String("exitleft")) return compileExitShow(rest, true);
    if (cmd == QLatin1String("exitright")) return compileExitShow(rest, false);
    if (cmd == QLatin1String("pulse")) return compilePulse(rest);
    if (cmd == QLatin1String("timeofday")) {
        const SL r = compileTimeOfDay(rest);
        for (const QString& x : r) {
            if (x.contains(QLatin1String("day_time()"))) st.timeOfDay = QStringLiteral("day");
            else if (x.contains(QLatin1String("sunset_time()"))) st.timeOfDay = QStringLiteral("sunset");
            else if (x.contains(QLatin1String("night_time()"))) st.timeOfDay = QStringLiteral("night");
        }
        st.timeSynced = st.timeExplicit = true;
        return r;
    }
    if (cmd == QLatin1String("eyesclose")) {
        st.eyesClosed = !opt.legacy;
        return compileEyesClose(rest);
    }
    if (cmd == QLatin1String("eyesopen")) {
        st.eyesClosed = st.autoOpen = false;
        return compileEyesOpen(rest);
    }
    if (cmd == QLatin1String("eyesblink")) {
        st.eyesClosed = st.autoOpen = false;
        return compileEyesBlink(rest);
    }
    if (cmd == QLatin1String("sleepyeyes")) return {QStringLiteral("    show blinking onlayer overlay")};
    if (cmd == QLatin1String("stopsleepyeyes")) return {QStringLiteral("    hide blinking onlayer overlay")};
    if (cmd == QLatin1String("ghostmove")) return compileGhostMove(rest);
    if (cmd == QLatin1String("flash")) return compileFlash(rest);
    if (cmd == QLatin1String("titlecard")) return compileTitlecard(rest, !opt.legacy, st.eyesClosed);
    if (cmd == QLatin1String("creditsroll")) {
        const bool shut = !opt.legacy && st.eyesClosed;
        st.eyesClosed = st.autoOpen = false;
        return compileCreditsRoll(rest, shut);
    }
    if (cmd == QLatin1String("note")) return compileLargeText(QStringLiteral("note"), rest, opt.legacy);
    if (cmd == QLatin1String("monologue") || cmd == QLatin1String("diary") || cmd == QLatin1String("memorynote") || cmd == QLatin1String("bigtext"))
        return compileLargeText(cmd, rest, opt.legacy);
    if (cmd == QLatin1String("notedim"))
        return {QStringLiteral("    show genry_note_dim onlayer overlay:"), QStringLiteral("        alpha ") + cleanAlpha(rest, QStringLiteral("0.60")),
                QStringLiteral("    with dissolve")};
    if (cmd == QLatin1String("notebg")) return compileNoteBg(rest);
    if (cmd == QLatin1String("notenvl")) return compileNoteNvl();
    if (cmd == QLatin1String("noteadv") || cmd == QLatin1String("closenote")) return compileNoteAdv();
    if (cmd == QLatin1String("staticfx") || cmd == QLatin1String("noisefx") || cmd == QLatin1String("glitchfx") || cmd == QLatin1String("vhsfx") ||
        cmd == QLatin1String("memoryfx") || cmd == QLatin1String("dreamfx")) {
        st.activePrologueDream = true;
        return compileMemoryEffect(cmd, rest);
    }
    if (cmd == QLatin1String("pixelfx")) return {QStringLiteral("    with pixellate")};
    if (cmd == QLatin1String("clearfx")) {
        st.activePrologueDream = false;
        return compileClearOverlays();
    }
    if (cmd == QLatin1String("dream")) {
        const QString e = isEffect(pyStrip(rest)) ? pyStrip(rest) : QStringLiteral("dissolve");
        if (e == QLatin1String("none")) return {QStringLiteral("    show prologue_dream") + effectZorder()};
        return {QStringLiteral("    show prologue_dream%1 with %2").arg(effectZorder(), e)};
    }
    if (cmd == QLatin1String("stopdream")) {
        const QString e = isEffect(pyStrip(rest)) ? pyStrip(rest) : QStringLiteral("dissolve");
        if (e == QLatin1String("none")) return {QStringLiteral("    hide prologue_dream")};
        return {QStringLiteral("    hide prologue_dream with ") + e};
    }
    if (cmd == QLatin1String("chapterpng")) return compileChapterPng(rest, c);
    if (cmd == QLatin1String("hide")) {
        SL w = words(rest);
        QString effect;
        if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
        return {withEffectLine(QStringLiteral("    hide ") + joinW(w), effect)};
    }
    if (cmd == QLatin1String("music")) return compileMusicPlay(rest, c);
    if (cmd == QLatin1String("musicfile")) return compileMusicFile(rest, modId);
    if (cmd == QLatin1String("musicqueue")) return compileAudioQueue(QStringLiteral("music"), rest, modId, true, c);
    if (cmd == QLatin1String("soundqueue")) return compileAudioQueue(QStringLiteral("sound"), rest, modId, false, c);
    if (cmd == QLatin1String("sound") || cmd == QLatin1String("voice")) return compileSoundExpr(rest, c);
    if (cmd == QLatin1String("soundfile") || cmd == QLatin1String("voicefile")) return {QStringLiteral("    play sound ") + pyQ(relModPath(modId, rest))};
    if (cmd == QLatin1String("voicedsay")) return compileVoicedSay(rest, modId, st, c);
    if (cmd == QLatin1String("punchedsay")) return compilePunchedSay(rest, st, c);
    if (cmd == QLatin1String("ambience")) return compileAmbience(rest, modId, c);
    if ((cmd == QLatin1String("windowhide") || cmd == QLatin1String("windowshow")) && !opt.legacy) {
        // V1: the author's own «окно скрыть/показать» - stays as written (the V1 «window auto» pass leaves these
        // alone); the transition is the statement's argument, «window hide with x» does not parse
        const QString e = effectOf(rest);
        const QString f = cmd == QLatin1String("windowhide") ? QStringLiteral("_window_hide") : QStringLiteral("_window_show");
        return {e.isEmpty() || e == QLatin1String("none") ? QStringLiteral("    $ %1()").arg(f) : QStringLiteral("    $ %1(%2)").arg(f, e)};
    }
    if (cmd == QLatin1String("windowhide")) return {withEffectLine(QStringLiteral("    window hide"), effectOf(rest))};
    if (cmd == QLatin1String("windowshow")) return {withEffectLine(QStringLiteral("    window show"), effectOf(rest))};
    if (cmd == QLatin1String("nvlstart")) {
        const QString e = isEffect(pyStrip(rest)) ? pyStrip(rest) : QStringLiteral("dissolve");
        return {QStringLiteral("    window hide"), QStringLiteral("    $ set_mode_nvl()"),
                QStringLiteral("    nvl show") + (e != QLatin1String("none") ? QLatin1Char(' ') + e : QString())};
    }
    if (cmd == QLatin1String("nvlend")) {
        const QString e = isEffect(pyStrip(rest)) ? pyStrip(rest) : QStringLiteral("dissolve");
        return {QStringLiteral("    nvl clear"), QStringLiteral("    nvl hide") + (e != QLatin1String("none") ? QLatin1Char(' ') + e : QString()),
                QStringLiteral("    $ set_mode_adv()"), QStringLiteral("    window show")};
    }
    if (cmd == QLatin1String("stopmusic")) return {QStringLiteral("    stop music fadeout 2")};
    if (cmd == QLatin1String("stopsound")) return {QStringLiteral("    stop sound")};
    if (cmd == QLatin1String("stopambience")) return {QStringLiteral("    stop ambience fadeout 2")};
    if (cmd == QLatin1String("stopallaudio"))
        return {QStringLiteral("    stop music fadeout 2"), QStringLiteral("    stop ambience fadeout 2"), QStringLiteral("    stop sound")};
    if (cmd == QLatin1String("stop")) {
        QString target = firstWord(pyStrip(rest.isEmpty() ? QStringLiteral("music") : rest)).toLower();
        const QString norm = normalizeCommand(target);
        static const QHash<QString, QString> map{{U("музыка"), QStringLiteral("music")}, {QStringLiteral("music"), QStringLiteral("music")},
                                                 {U("звук"), QStringLiteral("sound")}, {QStringLiteral("sound"), QStringLiteral("sound")},
                                                 {U("атмосфера"), QStringLiteral("ambience")}, {QStringLiteral("ambience"), QStringLiteral("ambience")},
                                                 {U("видео"), QStringLiteral("movie")}, {QStringLiteral("video"), QStringLiteral("movie")},
                                                 {U("всё"), QStringLiteral("all")}, {U("все"), QStringLiteral("all")}, {QStringLiteral("all"), QStringLiteral("all")}};
        target = map.value(target, map.value(norm, norm.isEmpty() ? target : norm));
        if (target == U("музыка") || target == QLatin1String("stopmusic")) target = QStringLiteral("music");
        else if (target == U("звук") || target == QLatin1String("stopsound")) target = QStringLiteral("sound");
        else if (target == U("атмосфера") || target == QLatin1String("stopambience")) target = QStringLiteral("ambience");
        else if (target == U("видео") || target == QLatin1String("video")) target = QStringLiteral("movie");
        else if (target == U("всё") || target == U("все") || target == QLatin1String("stopallaudio")) target = QStringLiteral("all");
        if (target == QLatin1String("all"))
            return {QStringLiteral("    stop music fadeout 2"), QStringLiteral("    stop ambience fadeout 2"), QStringLiteral("    stop sound"),
                    QStringLiteral("    stop movie")};
        if (target == QLatin1String("music")) return {QStringLiteral("    stop music fadeout 2")};
        if (target == QLatin1String("ambience")) return {QStringLiteral("    stop ambience fadeout 2")};
        return {QStringLiteral("    stop ") + target};
    }
    if (cmd == QLatin1String("hideall")) {
        SL r;
        for (const char* t : {"dv", "un", "sl", "mi", "us", "mt", "el", "sh", "mz", "uv", "cs", "genry", "scar"}) {
            // V1: «scar» is no image of the game - ES lint flags hiding it
            if (!opt.legacy && QLatin1String(t) == QLatin1String("scar")) continue;
            r << QStringLiteral("    hide ") + U(t);
        }
        r << QStringLiteral("    with dissolve");
        return r;
    }
    if (cmd == QLatin1String("effect")) {
        const QString e = pyStrip(rest).isEmpty() ? QStringLiteral("dissolve") : pyStrip(rest);
        if (e == QLatin1String("none")) return {};
        if (isEffect(e)) return {QStringLiteral("    with ") + e};
        SL all = T().effects.values();
        all.sort();
        return {QStringLiteral("    # effect command needs one of: ") + all.join(QStringLiteral(", "))};
    }
    if (cmd == QLatin1String("shake")) return compileShake(rest);
    // V1: «es:video/opening.ogv» is the game's own video (game/video), everything else lives in the mod
    auto videoPath = [&](const QString& raw) {
        const QString p = pyStrip(raw);
        return !opt.legacy && p.startsWith(QLatin1String("es:")) ? p.mid(3) : relModPath(modId, p);
    };
    if (cmd == QLatin1String("video")) return {QStringLiteral("    $ renpy.movie_cutscene(%1)").arg(pyQ(videoPath(rest)))};
    if (cmd == QLatin1String("videobg")) {
        SL w = words(rest);
        QString effect;
        if (!w.isEmpty() && isEffect(w.last())) effect = w.takeLast();
        if (w.isEmpty()) return {U("    # videobg command needs a video path")};
        SL r{QStringLiteral("    scene black"), QStringLiteral("    show expression Movie(play=%1) as genry_video_bg").arg(pyQ(videoPath(joinW(w))))};
        if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
        return r;
    }
    if (cmd == QLatin1String("stopvideobg")) {
        const SL w = words(rest);
        const QString effect = !w.isEmpty() && isEffect(w.last()) ? w.last() : QString();
        SL r{QStringLiteral("    hide genry_video_bg")};
        if (!effect.isEmpty() && effect != QLatin1String("none")) r << QStringLiteral("    with ") + effect;
        return r;
    }
    if (cmd == QLatin1String("chapter")) {
        const SL p = words(rest);
        const QString backdrop = !p.isEmpty() ? p[0] : QStringLiteral("days");
        const QString day = p.size() > 1 ? p[1] : QStringLiteral("1");
        QString title = rest;
        if (p.size() > 2) title = pyStrip(pySplit(rest, 2).value(2));
        else if (p.size() > 1) title = U("День ") + day;
        return {QStringLiteral("    window hide"), QStringLiteral("    $ backdrop = ") + pyQ(backdrop),
                QStringLiteral("    $ new_chapter(%1, %2)").arg(day, pyUQ(title)), QStringLiteral("    scene black"), QStringLiteral("    with dissolve")};
    }
    if (cmd == QLatin1String("timeskip")) {
        st.activePrologueDream = false;
        st.activeSpriteTags.clear();
        const SL r = compileTimeskip(rest, !opt.legacy, st.eyesClosed);
        if (!opt.legacy) st.eyesClosed = st.autoOpen = true;
        return r;
    }
    if (cmd == QLatin1String("achievement")) {
        const SL p = words(rest);
        if (p.isEmpty()) return {U("    # achievement command needs image path")};
        return compileAchievementPopup(relModPath(modId, p[0]), p.size() > 1 ? p[1] : QStringLiteral("3"), c);
    }
    if (cmd == QLatin1String("map") && !opt.legacy) {
        // V1: our own camp map on the game's art (genry_camp_map). ES's map gets its zones in ES's own
        // `label start` (init_map_zones), which a mod started from «Моды» never passes, other mods (7DL)
        // hang on the same layer, and the Steam build has no chibi icons - the stock map came up dead.
        const MapSpec m = parseMapSpec(rest);
        SL items;
        for (const MapEntry& p : m.places) {
            const EsMapZone* z = esMapZone(p.zone);
            if (!z) continue;                                               // the check names the wrong place
            const QString file = esChibiFile(p.chibi);
            const QString chibi = file.isEmpty() ? QStringLiteral("None")
                                                 : pyQ(relModPath(modId, QStringLiteral("images/genry_chibi/") + file + QStringLiteral(".png")));
            items << QStringLiteral("(%1, %2, %3, %4, %5, %6, %7, %8, %9, %10)")
                         .arg(pyQ(z->id), pyQ(c.lab(p.target)), chibi, pyUQ(z->title))
                         .arg(z->x1).arg(z->y1).arg(z->x2).arg(z->y2).arg(z->cx).arg(z->cy);
        }
        SL r{QStringLiteral("    window hide"), QStringLiteral("    $ _genry_map = [%1]").arg(items.join(QStringLiteral(", ")))};
        if (m.tour) {
            // ES day 2's walk-around list: a visited place goes out; all visited -> «готово» (or the list starts over)
            QStringList ids;
            for (const MapEntry& p : m.places) if (!p.zone.isEmpty()) ids << p.zone;
            const QString seen = c.sys(QStringLiteral("map_seen"));
            const QString key = pyQ(ids.join(QLatin1Char(',')));
            r << QStringLiteral("    $ _genry_map = [genry_z for genry_z in _genry_map if genry_z[0] not in %1.get(%2, [])]").arg(seen, key);
            r << QStringLiteral("    if not _genry_map:");
            if (!m.done.isEmpty()) r << QStringLiteral("        jump ") + c.lab(m.done);
            else r << QStringLiteral("        $ %1[%2] = []").arg(seen, key)
                   << QStringLiteral("        $ _genry_map = [%1]").arg(items.join(QStringLiteral(", ")));
            r << QStringLiteral("    scene expression genry_map_pic(\"bgpic\")") << QStringLiteral("    $ renpy.transition(dissolve)")
              << QStringLiteral("    call screen genry_camp_map(_genry_map)")
              << QStringLiteral("    $ %1.setdefault(%2, []).append(_return[0])").arg(seen, key);
        } else {
            // the map itself under the places: the way into a place dissolves from the map, not the scene before it
            r << QStringLiteral("    scene expression genry_map_pic(\"bgpic\")") << QStringLiteral("    $ renpy.transition(dissolve)")
              << QStringLiteral("    call screen genry_camp_map(_genry_map)");
        }
        r << QStringLiteral("    jump expression _return[1]");
        return r;
    }
    if (cmd == QLatin1String("map")) {
        SL r{QStringLiteral("    $ disable_all_zones()")};
        if (!opt.legacy) r.prepend(QStringLiteral("    window hide"));     // as ES day2 does: no empty dialogue box over the map
        for (QString entry : pySplitSep(rest, QStringLiteral(","))) {
            entry = pyStrip(entry);
            if (entry.isEmpty()) continue;
            QString chibi;
            bool hasChibi = false;
            if (entry.contains(QLatin1Char('@'))) {
                const int at = int(entry.lastIndexOf(QLatin1Char('@')));
                chibi = pyStrip(entry.mid(at + 1));
                entry = pyStrip(entry.left(at));
                hasChibi = true;
            } else if (entry.contains(QLatin1String(" chibi "))) {
                const int at = int(entry.lastIndexOf(QLatin1String(" chibi ")));
                chibi = pyStrip(entry.mid(at + 7));
                entry = pyStrip(entry.left(at));
                hasChibi = true;
            }
            QString zone, target;
            if (entry.contains(QLatin1Char(':'))) {
                const SL zt = pySplitSep(entry, QStringLiteral(":"), 1);
                zone = zt[0];
                target = zt[1];
            } else if (entry.contains(QLatin1String("->"))) {
                const SL zt = pySplitSep(entry, QStringLiteral("->"), 1);
                zone = zt[0];
                target = zt[1];
            } else {
                continue;
            }
            zone = pyStrip(zone);
            r << QStringLiteral("    $ set_zone(%1, %2)").arg(pyQ(zone), pyQ(c.lab(pyStrip(target))));
            if (hasChibi && !chibi.isEmpty()) {
                const QString ch = words(chibi).value(0);
                // V1: the Steam build has no map_icon_nXX.png - the mod brings its own faces
                const QString file = esChibiFile(ch);
                if (!opt.legacy && !file.isEmpty())
                    r << QStringLiteral("    $ map.chibi[%1] = %2").arg(pyQ(ch), pyQ(relModPath(modId, QStringLiteral("images/genry_chibi/") + file + QStringLiteral(".png"))));
                r << QStringLiteral("    $ set_chibi(%1, %2)").arg(pyQ(zone), pyQ(ch));
            }
        }
        r << QStringLiteral("    $ show_map()");
        return r;
    }
    if (cmd == QLatin1String("zone")) {
        const SL p = words(rest);
        if (p.size() >= 2) return {QStringLiteral("    $ set_zone(%1, %2)").arg(pyQ(p[0]), pyQ(c.lab(p[1])))};
        return {QStringLiteral("    # zone command needs: zone_label target_label")};
    }
    if (cmd == QLatin1String("chibi")) {
        const SL p = words(rest);
        if (p.size() >= 2) return {QStringLiteral("    $ set_chibi(%1, %2)").arg(pyQ(p[0]), pyQ(p[1]))};
        return {QStringLiteral("    # chibi command needs: zone chibi")};
    }
    if (cmd == QLatin1String("disablezone")) {
        if (!rest.isEmpty()) return {QStringLiteral("    $ reset_zone(%1)").arg(pyQ(pyStrip(rest)))};
        return {QStringLiteral("    $ disable_current_zone()")};
    }
    if (cmd == QLatin1String("resetzone")) {
        if (!rest.isEmpty()) return {QStringLiteral("    $ reset_zone(%1)").arg(pyQ(pyStrip(rest)))};
        return {QStringLiteral("    $ reset_current_zone()")};
    }
    if (!opt.legacy) {
        const SL ph = compileV1Phone(cmd, rest, modId, st, c);
        if (!ph.isEmpty()) return ph;
        const SL v1 = compileV1Feature(cmd, rest, modId, st, c);
        if (!v1.isEmpty() || cmd == QLatin1String("meter")) return v1;
    }
    if (cmd == QLatin1String("setvar")) {
        const SL p = words(rest);
        if (p.size() >= 2) return {QStringLiteral("    $ %1 = %2").arg(c.var(p[0]), pyStrip(pySplit(rest, 1).value(1)))};
        return {QStringLiteral("    # setvar command needs: name value")};
    }
    if (cmd == QLatin1String("addvar")) {
        const SL p = words(rest);
        if (p.size() >= 2) return {QStringLiteral("    $ %1 += %2").arg(c.var(p[0]), p[1])};
        return {QStringLiteral("    # addvar command needs: name value")};
    }
    if (cmd == QLatin1String("ifjump")) {
        if (rest.contains(QLatin1String("->"))) {
            const SL ct = pySplitSep(rest, QStringLiteral("->"), 1);
            return {QStringLiteral("    if %1:").arg(asciiCondition(pyStrip(ct[0]), c)), QStringLiteral("        jump ") + c.lab(pyStrip(ct[1]))};
        }
        return {QStringLiteral("    # if command needs: condition -> label")};
    }
    if (cmd == QLatin1String("jump")) return {QStringLiteral("    jump ") + c.lab(rest)};
    if (cmd == QLatin1String("callscene")) return {QStringLiteral("    call ") + c.lab(rest)};
    if (cmd == QLatin1String("return")) return {QStringLiteral("    return")};
    if (cmd == QLatin1String("endgame")) return endgameTerminator();
    if (cmd == QLatin1String("pause")) return {QStringLiteral("    $ renpy.pause(%1, hard=True)").arg(rest.isEmpty() ? QStringLiteral("1.0") : rest)};
    if (cmd == QLatin1String("renpy")) return {I4 + rest};
    if (cmd == QLatin1String("unlockgallery")) return {QStringLiteral("    $ persistent.%1 = True").arg(persistentKey(modId, rest, QStringLiteral("cg01"), opt))};
    if (cmd == QLatin1String("itemget") || cmd == QLatin1String("replayunlock")) {
        const bool item = cmd == QLatin1String("itemget");
        const SL p = c.pipes(rest);
        if (p.isEmpty()) return {item ? QStringLiteral("    # itemget needs: key | caption | [icon]") : QStringLiteral("    # replayunlock needs: key | caption")};
        const QString rawKey = pyStrip(p[0]);
        const QString key = persistentKey(modId, (item ? QStringLiteral("item_") : QStringLiteral("replay_")) + rawKey,
                                          item ? QStringLiteral("item") : QStringLiteral("replay"), opt);
        const QString caption = p.size() > 1 && !pyStrip(p[1]).isEmpty() ? pyStrip(p[1]) : rawKey;
        SL r{QStringLiteral("    $ persistent.%1 = True").arg(key)};
        r << compileNotifyPopup(item ? U("Предмет: %1 | Инвентарь | | #143247 | справа | 2.7").arg(caption)
                                     : U("Сцена открыта: %1 | Реплей | | #17324b | справа | 2.7").arg(caption),
                                modId, c);
        return r;
    }
    if (cmd == QLatin1String("ifitem")) {
        if (rest.contains(QLatin1String("->"))) {
            const SL ct = pySplitSep(rest, QStringLiteral("->"), 1);
            const QString keyText = pyStrip(ct[0]);
            if (!keyText.isEmpty()) {
                const QString key = persistentKey(modId, QStringLiteral("item_") + keyText, QStringLiteral("item"), opt);
                return {QStringLiteral("    if getattr(persistent, %1, False):").arg(pyQ(key)), QStringLiteral("        jump ") + c.lab(pyStrip(ct[1]))};
            }
        }
        return {QStringLiteral("    # ifitem command needs: key -> label")};
    }
    if (cmd == QLatin1String("persistentvar")) {
        const SL p = words(rest);
        if (p.size() >= 2)
            return {QStringLiteral("    $ persistent.%1 = %2").arg(persistentKey(modId, p[0], QStringLiteral("value"), opt), pyStrip(pySplit(rest, 1).value(1)))};
        return {QStringLiteral("    # persistentvar command needs: name value")};
    }
    if (cmd == QLatin1String("persistentadd")) {
        const SL p = words(rest);
        if (p.size() >= 2) {
            const QString name = persistentKey(modId, p[0], QStringLiteral("value"), opt);
            return {QStringLiteral("    $ persistent.%1 = getattr(persistent, %2, 0) + %3").arg(name, pyQ(name), p[1])};
        }
        return {QStringLiteral("    # persistentadd command needs: name value")};
    }
    if (cmd == QLatin1String("weather")) return compileWeather(rest, c);
    if (cmd == QLatin1String("notify")) return compileNotifyPopup(rest, modId, c);
    if (cmd == QLatin1String("unlockachievement")) {
        const SL p = c.pipes(rest);
        if (p.isEmpty()) return {QStringLiteral("    # unlockachievement needs: name | caption | [icon] | [seconds]")};
        const QString name = persistentKey(modId, p[0], QStringLiteral("ach"), opt);
        const QString caption = p.size() > 1 && !pyStrip(p[1]).isEmpty() ? pyStrip(p[1]) : pyStrip(p[0]);
        SL r{QStringLiteral("    $ persistent.%1 = True").arg(name)};
        const QString icon = p.size() > 2 ? pyStrip(p[2]) : QString();
        if (!icon.isEmpty())
            r << compileAchievementPopup(relModPath(modId, icon), p.size() > 3 ? cleanNumber(p[3], QStringLiteral("3")) : QStringLiteral("3"), c);
        else
            r << compileNotifyPopup(U("Достижение: %1 | Достижение | | #2b173d | справа | 3.0").arg(caption), modId, c);
        return r;
    }
    if (cmd == QLatin1String("musicplayer")) {
        const SL items = c.pipes(rest);
        SL pairs;
        for (int i = 0; i + 1 < items.size(); i += 2) {
            const QString n = pyStrip(items[i]), t = pyStrip(items[i + 1]);
            if (!n.isEmpty() && !t.isEmpty()) pairs << QStringLiteral("(%1, %2)").arg(pyUQ(n), pyQ(relModPath(modId, t)));
        }
        if (pairs.isEmpty()) return {QStringLiteral("    # musicplayer needs: Название | audio/file.ogg | Название2 | audio/file2.ogg")};
        return {QStringLiteral("    call screen genry_music_player([%1])").arg(pairs.join(QStringLiteral(", ")))};
    }
    if (cmd == QLatin1String("phonestart")) {
        const QString contact = pyStrip(rest).isEmpty() ? U("СМС") : pyStrip(rest);
        st.hasPhoneMin = true;
        st.phoneMin = 14 * 60 + 25;
        return {QStringLiteral("    window hide"), QStringLiteral("    $ _genry_phone = []"), QStringLiteral("    $ _genry_phone_typing = \"\""),
                QStringLiteral("    $ _genry_phone_contact = ") + pyUQ(contact),
                QStringLiteral("    show screen genry_phone(_genry_phone, _genry_phone_contact)")};
    }
    if (cmd == QLatin1String("sms")) {
        QString who, text;
        if (rest.contains(QLatin1Char(':'))) {
            const SL wt = pySplitSep(rest, QStringLiteral(":"), 1);
            who = wt[0];
            text = wt[1];
        } else {
            text = rest;
        }
        who = pyStrip(who);
        text = pyStrip(text);
        const QString wl = who.toLower();
        const bool me = wl == U("я") || wl == QLatin1String("me") || wl == QLatin1String("+") || wl == QLatin1String("i");
        const QString name = me ? U("Я") : (who.isEmpty() ? QStringLiteral("???") : who);
        int base = st.hasPhoneMin ? st.phoneMin : 14 * 60 + 25;
        base += 1;
        st.hasPhoneMin = true;
        st.phoneMin = base;
        const QString tstr = QStringLiteral("%1:%2").arg((base / 60) % 24, 2, 10, QLatin1Char('0')).arg(base % 60, 2, 10, QLatin1Char('0'));
        const QString status = me ? (opt.legacy ? U("✓✓") : U("√√")) : QString();   // V1: calibri has no ✓
        const QString append = QStringLiteral("    $ _genry_phone.append((%1, %2, %3, %4, %5))")
                                   .arg(pyQ(me ? QStringLiteral("me") : QStringLiteral("them")), pyUQ(name), pyUQ(text), pyUQ(tstr), pyUQ(status));
        if (!me)
            return {QStringLiteral("    $ _genry_phone_typing = ") + pyUQ(name), QStringLiteral("    $ renpy.pause(1.2)"),
                    QStringLiteral("    $ _genry_phone_typing = \"\""), append, QStringLiteral("    $ renpy.pause(0.6)")};
        return {append, QStringLiteral("    $ renpy.pause(0.6)")};
    }
    if (cmd == QLatin1String("phoneend")) return {QStringLiteral("    hide screen genry_phone"), QStringLiteral("    window show")};
    if (cmd == QLatin1String("floatingthought")) return compileFloatingThought(rest);
    if (cmd == QLatin1String("hidethought")) return {withEffectLine(QStringLiteral("    hide genry_thought_text"), effectOf(rest))};
    if (cmd == QLatin1String("colorfilter")) return compileColorFilter(rest);
    if (cmd == QLatin1String("ifpersistent")) {
        if (rest.contains(QLatin1String("->"))) {
            const SL ct = pySplitSep(rest, QStringLiteral("->"), 1);
            const SL cond = words(ct[0]);
            if (!cond.isEmpty()) {
                const QString key = persistentKey(modId, cond[0], QStringLiteral("value"), opt);
                const QString tail = pyStrip(pyStrip(ct[0]).mid(cond[0].size()));
                const QString full = pyStrip(QStringLiteral("persistent.%1 %2").arg(key, tail));
                return {QStringLiteral("    if %1:").arg(full), QStringLiteral("        jump ") + c.lab(pyStrip(ct[1]))};
            }
        }
        return {QStringLiteral("    # ifpersistent command needs: name >= value -> label")};
    }
    return {QStringLiteral("    # Unknown command kept as comment: ") + stripped};
}

// ================================================================== compile_story

const QStringList& hentaiPatchCgs()
{
    // the "image cg ..." lines of old_hentai.rpyc (Steam Workshop 1118110148): the Steam
    // build declares these too but ships without the files
    static const QStringList names{
        QStringLiteral("d2_mt_undressed"), QStringLiteral("d2_mt_undressed_2"), QStringLiteral("d2_sl_swim"),
        QStringLiteral("d3_sl_bathhouse"), QStringLiteral("d5_dv_us_wash"), QStringLiteral("d5_dv_us_wash_2"),
        QStringLiteral("d5_dv_us_wash_3"), QStringLiteral("d5_dv_us_wash_4"), QStringLiteral("d6_dv_hentai"),
        QStringLiteral("d6_dv_hentai_2"), QStringLiteral("d6_sl_swim"), QStringLiteral("d6_sl_hentai_1"),
        QStringLiteral("d6_sl_hentai_2"), QStringLiteral("d7_sl_morning"), QStringLiteral("d7_sl_morning_2"),
        QStringLiteral("d7_un_hentai"), QStringLiteral("d7_un_hentai_3"), QStringLiteral("miku_h_1_cenz"),
        QStringLiteral("miku_h_2_cenz"), QStringLiteral("uvao_h_cenz")};
    return names;
}

QString choiceStyleOf(const QString& word)
{
    static const QSet<QString> images{U("визуальный"), U("визуально"), QStringLiteral("visual"), U("картинки"), U("картинками"),
                                      U("картинка"), U("7дл"), QStringLiteral("7dl"), QStringLiteral("images"), QStringLiteral("image"),
                                      U("полосы"), U("арты")};
    static const QSet<QString> buttons{U("кнопки"), U("кнопками"), QStringLiteral("buttons"), U("тёмный"), U("темный")};
    const QString w = pyStrip(word).toLower();
    if (images.contains(w)) return QStringLiteral("images");
    if (buttons.contains(w)) return QStringLiteral("buttons");
    static const QSet<QString> phone{U("телефон"), U("в телефоне"), U("ответ"), U("смс"), U("переписка"), QStringLiteral("phone")};
    if (phone.contains(w)) return QStringLiteral("phone");      // V1: reply bubbles inside the phone
    // «выбор на время 8», «выбор таймер 5», «выбор 8 сек», «выбор telltale»
    static const QRegularExpression timed(QStringLiteral("(время|таймер|timer|telltale|сек|^\\d+([.,]\\d+)?$)"),
                                          QRegularExpression::UseUnicodePropertiesOption);
    if (!w.isEmpty() && timed.match(w).hasMatch()) return QStringLiteral("timed");
    return QStringLiteral("es");
}

QString choiceSeconds(const QString& style)
{
    static const QRegularExpression num(QStringLiteral("(\\d+(?:[.,]\\d+)?)"));
    const auto m = num.match(style);
    if (!m.hasMatch()) return QStringLiteral("10.0");
    double v = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    v = qBound(2.0, v, 60.0);
    return pyRepr(v);
}

bool isTimeoutWord(const QString& s)
{
    static const QSet<QString> w{U("время вышло"), U("таймаут"), QStringLiteral("timeout"), U("не успел"), U("не успела"), U("молчание"),
                                 U("молчать"), U("если не успел"), U("по таймеру")};
    return w.contains(pyStrip(s).toLower());
}

QString choiceImage(const QString& raw, QString* kind, const QSet<QString>& customImages)
{
    const QString s = raw.simplified();
    const QString first = s.section(QLatin1Char(' '), 0, 0).toLower();
    const QString rest = s.section(QLatin1Char(' '), 1);
    auto result = [&](const char* k, const QString& name) {
        if (kind) *kind = QLatin1String(k);
        return name;
    };
    if (customImages.contains(s.toLower()))
        return result(first == QLatin1String("bg") || first == QLatin1String("cg") ? "bg" : "sprite", s.toLower());
    if (first == QLatin1String("bg") || first == QLatin1String("cg")) return result("bg", first + QLatin1Char(' ') + rest);
    if (first == U("фон")) return result("bg", QStringLiteral("bg ") + rest);
    if (first == U("цг")) return result("bg", QStringLiteral("cg ") + rest);
    if (T().spritePrefixes.contains(first) || first.startsWith(QLatin1String("genry_"))) return result("sprite", s);
    return result("bg", QStringLiteral("bg ") + s);
}

// V1: the 7DL-style picture menu (their day-0 hero choice: the screen cut into tall strips,
// the hovered one lights up). Plain `menu:` underneath, so rollback and "chosen" still work.
static SL imageMenuScreen()
{
    return {QString(),
            U("# GenryBL V1: картиночный выбор в духе 7ДЛ — полосы на весь экран, наведённая загорается"),
            QStringLiteral("init python:"),
            QStringLiteral("    def genry_choice_panel(img, kind, w):"),
            QStringLiteral("        h = config.screen_height"),
            QStringLiteral("        if not img:"),
            QStringLiteral("            return Solid(\"#18221d\", xsize=w, ysize=h)"),
            QStringLiteral("        if kind == \"sprite\":"),
            QStringLiteral("            sw = 1050 if img.endswith(\" close\") else (630 if img.endswith(\" far\") else 900)"),
            QStringLiteral("            pic = Transform(img, crop=((sw - w) // 2, 0, w, h)) if w < sw else Transform(img, xalign=0.5)"),
            QStringLiteral("            return Fixed(Solid(\"#111814\"), pic, xsize=w, ysize=h)"),
            QStringLiteral("        sw = config.screen_width"),
            QStringLiteral("        return Transform(img, crop=((sw - w) // 2, 0, w, h)) if w < sw else img"),
            QString(),
            QStringLiteral("transform genry_choice_strip:"),
            QStringLiteral("    on show:"),
            QStringLiteral("        alpha 0.0"),
            QStringLiteral("        easein 0.4 alpha 0.55"),
            QStringLiteral("    on idle:"),
            QStringLiteral("        easein 0.25 alpha 0.55"),
            QStringLiteral("    on hover:"),
            QStringLiteral("        easein 0.25 alpha 1.0"),
            QString(),
            QStringLiteral("screen genry_choice_img(items):"),
            QStringLiteral("    zorder 50"),
            QStringLiteral("    modal True"),
            QStringLiteral("    $ genry_n = max(1, len(items))"),
            QStringLiteral("    $ genry_w = config.screen_width // genry_n"),
            QStringLiteral("    add Solid(\"#000000\")"),
            QStringLiteral("    for genry_i, genry_it in enumerate(items):"),
            QStringLiteral("        button at genry_choice_strip:"),
            QStringLiteral("            xpos genry_i * genry_w"),
            QStringLiteral("            xysize (genry_w, config.screen_height)"),
            QStringLiteral("            padding (0, 0)"),
            QStringLiteral("            background None"),
            QStringLiteral("            action genry_it.action"),
            QStringLiteral("            activate_sound \"sound/sfx/click_1.ogg\""),
            QStringLiteral("            fixed:"),
            QStringLiteral("                xysize (genry_w, config.screen_height)"),
            QStringLiteral("                add genry_choice_panel(genry_it.kwargs.get(\"img\"), genry_it.kwargs.get(\"kind\", \"bg\"), genry_w)"),
            QStringLiteral("                add Solid(\"#000000a0\", xsize=genry_w, ysize=250) yalign 1.0"),
            QStringLiteral("                vbox:"),
            QStringLiteral("                    xalign 0.5"),
            QStringLiteral("                    yalign 0.95"),
            QStringLiteral("                    xmaximum genry_w - 60"),
            QStringLiteral("                    spacing 6"),
            QStringLiteral("                    text genry_it.caption size 44 color \"#ffffff\" outlines [(3, \"#000000cc\", 0, 0)] text_align 0.5 xalign 0.5 font \"fonts/calibri.ttf\""),
            QStringLiteral("                    if genry_it.chosen:"),
            U("                        text \"√ уже выбирали\" size 22 color \"#9bd35a\" xalign 0.5 font \"fonts/calibri.ttf\""),
            QStringLiteral("    for genry_i in range(1, genry_n):"),
            QStringLiteral("        add Solid(\"#ffffff30\", xsize=2, ysize=config.screen_height) xpos genry_i * genry_w - 1")};
}

// V1: a mod that shows a CG of the 18+ patch would crash for every player without the
// patch ("Couldn't find file images/cg/..."). The mod carries the pictures itself (images/genry_patch,
// Builder copies them from GenryBL's own copy of the patch), a dark card only if even that is missing.
static SL hentaiPatchFallback(const SL& rpy, const QString& modId)
{
    // the patch's pictures a mod shows: the player's patch first, else the mod's own copy (also over the
    // Steam scripts' declarations of files the Steam build lacks), else a dark card instead of a crash
    static const QRegularExpression cgRe(QStringLiteral("\\bcg ([A-Za-z0-9_]+)\\b"));
    SL used;
    for (const QString& line : rpy) {
        const QString s = pyStrip(line);
        if (s.startsWith(QLatin1Char('#'))) continue;
        for (auto it = cgRe.globalMatch(line); it.hasNext();) {
            const QString n = it.next().captured(1);
            if (esPatchImage(QStringLiteral("cg ") + n) && !used.contains(n)) used << n;
        }
    }
    if (used.isEmpty()) return {};
    SL q;
    for (const QString& n : used) {
        const QString path = esPatchImage(QStringLiteral("cg ") + n)->path;
        q << QStringLiteral("(%1, %2, %3)").arg(pyQ(n), pyQ(path),
                                                 pyQ(QStringLiteral("mods/%1/images/genry_patch/%2").arg(modId, path.section(QLatin1Char('/'), -1))));
    }
    return {QString(), QStringLiteral("init 990 python:"),
            QStringLiteral("    for _genry_cg, _genry_f, _genry_own in [%1]:").arg(q.join(QStringLiteral(", "))),
            QStringLiteral("        if renpy.loadable(_genry_f):"),
            QStringLiteral("            if not renpy.has_image((\"cg\", _genry_cg), exact=True):"),
            QStringLiteral("                renpy.image((\"cg\", _genry_cg), _genry_f)"),
            QStringLiteral("        elif renpy.loadable(_genry_own):"),
            QStringLiteral("            renpy.image((\"cg\", _genry_cg), _genry_own)"),
            QStringLiteral("        else:"),
            U("            renpy.image((\"cg\", _genry_cg), Fixed(Solid(\"#0b0b12\"), Text(u\"Здесь CG из хентай-патча\\n(Мастерская Steam, id 1118110148)\", "
              "size=40, color=\"#e8e8e8\", text_align=0.5, xalign=0.5, yalign=0.5)))")};
}

// V1: under shut eyes nothing plays a transition (it would be dead time in the dark), and after «скачок» the eyes
// open by themselves right before the first thing the player has to see or answer - a line, a choice, a popup, a
// jump to another scene. The place, the heroines, the time of day, the music are set up under the lids first.
bool quietUnderLids(const QString& cmd)
{
    static const QSet<QString> quiet{
        QStringLiteral("bg"), QStringLiteral("showbg"), QStringLiteral("cg"), QStringLiteral("scene"), QStringLiteral("show"),
        QStringLiteral("hide"), QStringLiteral("hideall"), QStringLiteral("timeofday"), QStringLiteral("music"), QStringLiteral("musicfile"),
        QStringLiteral("musicqueue"), QStringLiteral("ambience"), QStringLiteral("stopmusic"), QStringLiteral("stopambience"),
        QStringLiteral("stopsound"), QStringLiteral("stopallaudio"), QStringLiteral("stop"), QStringLiteral("setvar"), QStringLiteral("addvar"),
        QStringLiteral("variable"), QStringLiteral("persistentvar"), QStringLiteral("weather"), QStringLiteral("colorfilter"),
        QStringLiteral("character"), QStringLiteral("meter")};
    return quiet.contains(cmd);
}

QStringList compileLine(const QString& line, const QString& modId, CompileState& st, const CompileOptions& opt)
{
    if (opt.legacy || !st.eyesClosed) return compileLineCore(line, modId, st, opt);
    const QString stripped = pyStrip(line);
    if (stripped.isEmpty() || stripped.startsWith(QLatin1Char('#')) || stripped.startsWith(QLatin1Char(':'))) return compileLineCore(line, modId, st, opt);
    const QString cmd = normalizeCommand(firstWord(stripped));
    const bool say = stripped.contains(QLatin1Char(':')) && !isCommandName(cmdOf(stripped));
    if (!say && quietUnderLids(cmd)) {
        QStringList r = compileLineCore(line, modId, st, opt);
        static const QRegularExpression withRe(QStringLiteral("^(    (?:scene|show|hide) .+?) with [A-Za-z0-9_]+$"));
        for (QString& x : r) {
            const auto m = withRe.match(x);
            if (m.hasMatch()) x = m.captured(1);
        }
        return r;
    }
    if (!st.autoOpen || cmd == QLatin1String("eyesopen") || cmd == QLatin1String("eyesblink") || cmd == QLatin1String("timeskip") ||
        cmd == QLatin1String("titlecard") || cmd == QLatin1String("creditsroll"))
        return compileLineCore(line, modId, st, opt);
    st.eyesClosed = st.autoOpen = false;
    return compileEyesOpen(QStringLiteral("1.6")) + compileLineCore(line, modId, st, opt);
}

QString compileStory(const ModMeta& meta, const QStringList& bodyRaw, const CompileOptions& opt, const QVector<CustomImage>& images, QString* error)
{
    // V1 «пиши как сценарий»: headings, «Алиса (злая, слева): …», prose -> ordinary commands first
    const QStringList bodyIn = opt.legacy ? bodyRaw : expandScreenplay(bodyRaw);
    QSet<QString> modVars;
    SL varOrder;
    QHash<QString, QString> varInit;
    if (!opt.legacy) {
        for (const QString& raw : bodyIn) {
            const QString s = pyStrip(raw);
            const QString w = firstWord(s);
            const QString cmd = normalizeCommand(w);
            if (cmd != QLatin1String("setvar") && cmd != QLatin1String("addvar") && cmd != QLatin1String("variable")) continue;
            const SL p = pySplit(pyStrip(s.mid(w.size())));
            if (p.isEmpty()) continue;
            const QString v = slugOf(p[0], QStringLiteral("value"), opt);
            if (!modVars.contains(v)) { modVars.insert(v); varOrder << v; }
            if (cmd == QLatin1String("variable") && p.size() >= 2 && !varInit.contains(v))
                varInit.insert(v, pyStrip(pySplit(pyStrip(s.mid(w.size())), 1).value(1)));
        }
    }
    // V1 pre-pass: meters, the item registry, achievements, the mod's CGs and its main menu block
    struct Meter { QString raw, title, color, lo, hi; };
    struct Item { QString key, caption, icon, desc; };
    QVector<Meter> meterList;
    QVector<Item> itemList;
    QSet<QString> itemSeen;
    QVector<QPair<QString, QString>> achList;
    SL cgList, menuLines, srcBody;
    QString menuStyle;
    struct Chapter { QString scene, title, image; };
    QVector<Chapter> chapterList;
    bool modMenu = false;
    if (!opt.legacy) {
        bool inMenu = false;
        QString curScene = QStringLiteral("start");
        auto fields = [](const QString& rest) {
            SL f;
            for (const QString& x : rest.split(QLatin1Char('|'))) f << pyStrip(x);
            return f;
        };
        for (const QString& raw : bodyIn) {
            const QString s = pyStrip(raw);
            const QString w = firstWord(s);
            const QString cmd = normalizeCommand(w);
            const QString rest = pyStrip(s.mid(w.size()));
            if (inMenu) {
                if (cmd == QLatin1String("endmodmenu") || cmd == QLatin1String("endchoice")) inMenu = false;
                else if (!s.isEmpty() && !s.startsWith(QLatin1Char('#'))) menuLines << s;
                continue;
            }
            if (cmd == QLatin1String("modmenu")) { inMenu = modMenu = true; menuStyle = rest.toLower(); continue; }
            srcBody << raw;
            if (s.startsWith(QLatin1Char(':'))) curScene = pyStrip(s.mid(1));
            if (cmd == QLatin1String("newchapter")) {
                const SL f = fields(rest);
                if (!f.value(0).isEmpty()) chapterList.push_back({curScene, f[0], f.value(1)});
            }
            if (cmd == QLatin1String("meter")) {
                const SL f = fields(rest);
                if (f.value(0).isEmpty()) continue;
                const QString v = slugOf(f[0], QStringLiteral("value"), opt);
                const QString lo = pyIsNumber(f.value(3)) ? f[3] : QStringLiteral("0");
                const QString hi = pyIsNumber(f.value(4)) ? f[4] : QStringLiteral("10");
                if (!modVars.contains(v)) { modVars.insert(v); varOrder << v; }
                if (!varInit.contains(v)) varInit.insert(v, lo.toDouble() > 0 ? lo : QStringLiteral("0"));
                meterList.push_back({f[0], f.value(1).isEmpty() ? f[0] : f[1], pyIsHexColor(f.value(2)) ? f[2] : QStringLiteral("#ffd27d"), lo, hi});
            } else if (cmd == QLatin1String("remember")) {
                const QString flag = parseV1Opts(rest).get(QStringLiteral("flag"));      // «флаг=помог»: False until remembered
                if (!flag.isEmpty()) {
                    const QString v = slugOf(flag, QStringLiteral("value"), opt);
                    if (!modVars.contains(v)) { modVars.insert(v); varOrder << v; }
                    if (!varInit.contains(v)) varInit.insert(v, QStringLiteral("False"));
                }
            } else if (cmd == QLatin1String("itemget")) {
                const SL f = fields(rest);
                const QString key = slugOf(f.value(0), QStringLiteral("item"), opt);
                if (f.value(0).isEmpty() || itemSeen.contains(key)) continue;
                itemSeen.insert(key);
                QString icon = f.value(2);
                if (!icon.isEmpty() && (icon.contains(QLatin1Char('/')) || icon.contains(QLatin1Char('.')))) icon = relModPath(meta.modId, icon);
                itemList.push_back({key, f.value(1).isEmpty() ? f[0] : f[1], icon, f.value(3)});
            } else if (cmd == QLatin1String("unlockachievement")) {
                const SL f = fields(rest);
                if (!f.value(0).isEmpty())
                    achList.push_back({persistentKey(meta.modId, f[0], QStringLiteral("ach"), opt), f.value(1).isEmpty() ? f[0] : f[1]});
            } else if (cmd == QLatin1String("cg")) {
                SL ww = pySplit(rest);
                if (!ww.isEmpty() && isEffect(ww.last())) ww.removeLast();
                const QString name = QStringLiteral("cg ") + ww.join(QLatin1Char(' '));
                if (!ww.isEmpty() && !cgList.contains(name)) cgList << name;
            }
        }
    }
    const SL& storyBody = opt.legacy ? bodyIn : srcBody;
    const Ctx c{opt, meta.modId, &modVars, modMenu};
    const QString modId = meta.modId;
    QString display = meta.modName;
    QString titleFont = meta.titleFont;
    titleFont.replace(QLatin1Char('\\'), QLatin1Char('/'));
    titleFont = pyStrip(titleFont);
    const QString titleColor = pyStrip(meta.titleColor), titleSize = pyStrip(meta.titleSize);
    if (!opt.legacy) {       // V1: «@mod_title_style b i» - bold / italic in the ES mods list
        const QString st = meta.titleStyle.toLower();
        if (st.contains(QLatin1Char('i'))) display = QStringLiteral("{i}%1{/i}").arg(display);
        if (st.contains(QLatin1Char('b'))) display = QStringLiteral("{b}%1{/b}").arg(display);
    }
    if (!titleFont.isEmpty()) {
        // V1: «es:fonts/x.ttf» = a font of the game itself (nothing copied into the mod)
        if (!opt.legacy && titleFont.startsWith(QLatin1String("es:"))) titleFont = titleFont.mid(3);
        else if (!titleFont.startsWith(QLatin1String("mods/"))) titleFont = relModPath(modId, titleFont);
        display = QStringLiteral("{font=%1}%2{/font}").arg(titleFont, display);
    }
    if (!titleColor.isEmpty() && pyIsHexColor(titleColor)) display = QStringLiteral("{color=%1}%2{/color}").arg(titleColor, display);
    static const QRegularExpression size3(QStringLiteral("^[0-9]{1,3}$"));
    if (!titleSize.isEmpty() && size3.match(titleSize).hasMatch()) display = QStringLiteral("{size=%1}%2{/size}").arg(titleSize, display);

    SL initLines, scriptLines;
    QHash<QString, QString> speakerMap;
    auto addInit = [&](const QString& l) { if (!initLines.contains(l)) initLines << l; };
    for (const QString& raw : storyBody) {
        const QString s = pyStrip(raw);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#'))) { scriptLines << raw; continue; }
        const SL parts = words(s);
        const QString cmd = parts.isEmpty() ? QString() : normalizeCommand(parts[0]);
        const QString rest = parts.isEmpty() ? QString() : pyStrip(s.mid(parts[0].size()));
        if (cmd == QLatin1String("character")) {
            SL v = words(rest);
            if (!v.isEmpty()) {
                const QString id = c.sl(v[0], "char");
                // V1: "gg" (or even "dv") as a bare global is overwritten by any other mod - or
                // overwrites ES's own Alisa. ES Doc's error #1; the mod prefix makes it private.
                const QString var = opt.legacy ? id : c.var(QStringLiteral("chr_") + id);
                QString color = QStringLiteral("#008000");
                SL nameParts = v.mid(1);
                if (!nameParts.isEmpty() && pyIsHexColor(nameParts.last())) color = nameParts.takeLast();
                QString name = pyStrip(joinW(nameParts));
                if (name.isEmpty()) name = id;
                addInit(QStringLiteral("    $ %1 = Character(%2, color=%3, what_color=\"#f1d076\")").arg(var, pyUQ(name), pyQ(color)));
                speakerMap.insert(id.toLower(), var);
                speakerMap.insert(name.toLower(), var);
            }
            continue;
        }
        if (cmd == QLatin1String("variable")) {
            const SL v = words(rest);
            if (v.size() >= 2 && opt.legacy) addInit(QStringLiteral("    $ %1 = %2").arg(c.sl(v[0], "value"), pyStrip(pySplit(rest, 1).value(1))));
            continue;   // V1: becomes a top-level default below
        }
        scriptLines << raw;
    }
    SL body = repairSelfJumpSceneSplits(scriptLines, c);
    body = removeEyesopenBeforeJump(body, c);
    body = forceNoneTransitionsWhileEyesClosed(body, c);

    SL out;
    appendBlock(out, kHeadA, modId);
    out << QStringLiteral("    $ mods[%1] = %2").arg(pyQ(modId), pyUQ(display));
    appendBlock(out, kHeadB, modId);
    out << initLines;
    const int declareAt = int(out.size());
    for (const CustomImage& img : images) out << QStringLiteral("    image %1 = %2").arg(img.name, pyQ(img.path));
    appendBlock(out, kHeadC, modId);
    if (!opt.legacy) {
        // V1: the old header's `screen choice` replaced the choice menu of the WHOLE game (ES's own
        // story and every other mod). It becomes the mod family's own screen, called by name.
        for (QString& l : out)
            if (l == QLatin1String("screen choice(items):")) { l = QStringLiteral("screen genry_choice(items):"); break; }
    }
    for (const QString& v : varOrder) out << QStringLiteral("default %1 = %2").arg(c.var(v), varInit.value(v, QStringLiteral("0")));
    if (!varOrder.isEmpty()) out << QString();
    const int scriptStart = int(out.size());

    QSet<QString> customNames;
    for (const CustomImage& img : images) customNames.insert(img.name.toLower());
    bool hasLabel = false, inChoice = false, usedImageMenu = false;
    QString choiceStyle;                  // V1: "es" (the game's own menu), "buttons" (genry_choice), "images" (7DL strips)
    struct ChoiceItem { QString caption, target, image; };
    QVector<ChoiceItem> choiceItems;
    QSet<QString> choiceTargets;
    SL screenmenuTargets;
    CompileState st;
    st.speakers = speakerMap;
    st.modVars = modVars;
    st.modMenu = modMenu;
    for (const Meter& m : meterList) st.meters.insert(c.var(m.raw), {m.title, m.color, m.lo, m.hi});
    QString timeoutTarget, choiceSecs;
    bool timeoutSet = false;
    auto flushChoice = [&] {
        SL r;
        if (!choiceItems.isEmpty()) {
            const bool timed = !opt.legacy && choiceStyle == QLatin1String("timed");
            bool images = choiceStyle == QLatin1String("images");
            for (const auto& it : choiceItems) if (!it.image.isEmpty()) images = true;
            if (timed) r << QStringLiteral("    menu (screen=\"genry_choice_timed\", seconds=%1):").arg(choiceSecs);
            else if (!opt.legacy && choiceStyle == QLatin1String("phone")) r << QStringLiteral("    menu (screen=\"genry_phone_reply\"):");
            else if (opt.legacy || (!images && choiceStyle == QLatin1String("es"))) r << QStringLiteral("    menu:");
            else if (images) r << QStringLiteral("    menu (screen=\"genry_choice_img\"):");
            else r << QStringLiteral("    menu (screen=\"genry_choice\"):");
            usedImageMenu = usedImageMenu || (images && !opt.legacy && !timed);
            if (timed) {      // the timer picks this hidden item: its scene, or the story just goes on
                r << QStringLiteral("        \"...\" (timeout=True):");
                if (timeoutSet) {
                    choiceTargets.insert(c.lab(timeoutTarget));
                    r << QStringLiteral("            jump ") + c.lab(timeoutTarget);
                } else {
                    r << QStringLiteral("            pass");
                }
            }
            for (const auto& it : choiceItems) {
                choiceTargets.insert(c.lab(it.target));
                QString head = pyQ(it.caption);
                if (!it.image.isEmpty()) {
                    QString kind;
                    const QString img = choiceImage(it.image, &kind, customNames);
                    head += QStringLiteral(" (img=") + pyQ(img) + QStringLiteral(", kind=") + pyQ(kind) + QLatin1Char(')');
                }
                r << QStringLiteral("        ") + head + QLatin1Char(':') << QStringLiteral("            jump ") + c.lab(it.target);
            }
            if (timed) {      // Ren'Py shows the items in order: the timeout one goes last (hidden anyway)
                const int n = int(choiceItems.size());
                const SL timeoutLines = r.mid(1, 2);
                r.remove(1, 2);
                r.insert(1 + 2 * n, timeoutLines.value(1));
                r.insert(1 + 2 * n, timeoutLines.value(0));
            }
        }
        return r;
    };
    for (const QString& raw : body) {
        QString line = raw;
        while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QString stripped = pyStrip(line);
        const QString cmd = cmdOf(stripped);
        if (stripped.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) hasLabel = true;
        if (inChoice) {
            if (stripped.isEmpty() || stripped.startsWith(QLatin1Char('#'))) continue;
            if (cmd == QLatin1String("endchoice")) {
                out << flushChoice();
                choiceItems.clear();
                inChoice = false;
                continue;
            }
            if (stripped.startsWith(QLatin1Char('-')) && stripped.contains(QLatin1String("->"))) {
                const SL tt = pySplitSep(stripped.mid(1), QStringLiteral("->"), 1);
                QString target = pyStrip(tt[1]), image;
                if (!opt.legacy && target.contains(QLatin1Char('|'))) {      // V1: «-> сцена | картинка»
                    image = pyStrip(target.section(QLatin1Char('|'), 1));
                    target = pyStrip(target.section(QLatin1Char('|'), 0, 0));
                }
                choiceItems.push_back({pyStrip(tt[0]), target, image});
                continue;
            }
            if (!opt.legacy && stripped.contains(QLatin1String("->")) && isTimeoutWord(stripped.section(QStringLiteral("->"), 0, 0))) {
                timeoutTarget = pyStrip(stripped.section(QStringLiteral("->"), 1));     // «время вышло -> молчание»
                timeoutSet = !timeoutTarget.isEmpty();
                continue;
            }
            if (error) *error = QStringLiteral("ValueError: Bad choice line: ") + stripped;
            return {};
        }
        if (cmd == QLatin1String("choice")) {
            if (!opt.legacy && st.autoOpen) {           // the choice after «скачок»: the eyes open first
                out << compileEyesOpen(QStringLiteral("1.6"));
                st.eyesClosed = st.autoOpen = false;
            }
            inChoice = true;
            choiceItems.clear();
            const QString style = pyStrip(stripped.mid(firstWord(stripped).size()));
            choiceStyle = choiceStyleOf(style);
            choiceSecs = choiceSeconds(style);
            timeoutTarget.clear();
            timeoutSet = false;
            continue;
        }
        if (cmd == QLatin1String("screenmenu")) {
            for (const QString& t : screenMenuTargets(pyStrip(stripped.mid(firstWord(stripped).size())), c)) {
                if (!screenmenuTargets.contains(t)) screenmenuTargets << t;
                choiceTargets.insert(t);
            }
        }
        if ((cmd == QLatin1String("staticfx") || cmd == QLatin1String("noisefx") || cmd == QLatin1String("vhsfx")) && !out.isEmpty()) {
            static const QRegularExpression withRe(QStringLiteral("^(    .+?) with ([A-Za-z0-9_]+)$"));
            const QString last = out.last();
            const auto m = withRe.match(last);
            if (m.hasMatch() && (last.startsWith(QLatin1String("    scene ")) || last.startsWith(QLatin1String("    show bg ")))) {
                out.last() = m.captured(1);
                st.activePrologueDream = true;
                for (const QString& fx : compileMemoryEffect(cmd, pyStrip(stripped.mid(firstWord(stripped).size()))))
                    if (!pyStrip(fx).startsWith(QLatin1String("with "))) out << fx;
                out << QStringLiteral("    with ") + m.captured(2);
                continue;
            }
        }
        out << compileLine(line, modId, st, opt);
    }
    if (inChoice) out << flushChoice();
    int declShift = 0;
    if (!opt.legacy) {
        // V1: a speaker ES does not know gets a real Character instead of a NameError crash
        SL decl;
        for (auto it = st.unknownSpeakers.begin(); it != st.unknownSpeakers.end(); ++it)
            decl << QStringLiteral("    $ %1 = Character(%2, color=\"#e8e8e8\")").arg(it.key(), pyUQ(it.value()));
        // румянец / пот / слёзы / мокрая: the ES sprite + the overlay the builder draws for it
        QStringList ov(st.overlayImages.begin(), st.overlayImages.end());
        ov.sort();
        for (const QString& image : ov) {
            QString base;
            splitOverlays(image, &base, nullptr);
            decl << QStringLiteral("    image %1 = Fixed(%2, %3, fit_first=True)")
                        .arg(image, pyQ(base), pyQ(relModPath(modId, QStringLiteral("images/genry_ov/") + overlayFile(image))));
        }
        for (int i = int(decl.size()) - 1; i >= 0; --i) out.insert(declareAt, decl[i]);
        declShift = int(decl.size());
    }
    SL menuScreen;
    if (modMenu) {
        // V1 «менюмода»: label <mod> = the mod's own main menu, the story itself starts at <mod>__start
        const int at = scriptStart + declShift;
        bool preludeFirst = true;
        for (int i = at; i < out.size(); ++i) {
            const QString s = pyStrip(out[i]);
            if (s.isEmpty() || s.startsWith(QLatin1Char('#'))) continue;
            preludeFirst = !out[i].startsWith(QLatin1String("label "));
            break;
        }
        if (preludeFirst) out.insert(at, QStringLiteral("label %1__start:").arg(modId));
        QString title = meta.modName, logo, startLab;
        SL prelude, buttons, returns, heroes;
        for (const QString& s : menuLines) {
            const QString w = firstWord(s), lw = w.toLower(), rest = pyStrip(s.mid(w.size()));
            if (lw == U("заголовок") || lw == QLatin1String("title")) { title = rest; continue; }
            if (lw == U("лого") || lw == U("логотип") || lw == QLatin1String("logo")) { logo = rest; continue; }
            if (lw == U("стиль") || lw == QLatin1String("style")) { menuStyle = rest.toLower(); continue; }
            if (lw == U("герои") || lw == U("героини") || lw == U("вайфу") || lw == QLatin1String("heroes")) {
                for (const QString& h : rest.split(QLatin1Char('|'))) {
                    QString kind;
                    const QString img = choiceImage(pyStrip(h), &kind, customNames);
                    if (!pyStrip(h).isEmpty()) heroes << pyQ(img);
                }
                continue;
            }
            if (lw != U("кнопка") && lw != QLatin1String("button")) { prelude << compileLine(s, modId, st, opt); continue; }
            QString cap = rest, target;
            if (rest.contains(QLatin1String("->"))) {
                cap = pyStrip(rest.section(QStringLiteral("->"), 0, 0));
                target = pyStrip(rest.section(QStringLiteral("->"), 1));
            }
            const QString key = (target.isEmpty() ? cap : target).toLower();
            QString action;
            if (key == U("галерея") || key == QLatin1String("gallery")) action = QStringLiteral("Show(\"genry_gallery\", cgs=%1)").arg(c.sys(QStringLiteral("cgs")));
            else if (key == U("достижения") || key == QLatin1String("achievements"))
                action = QStringLiteral("Show(\"genry_achievements\", achs=%1)").arg(c.sys(QStringLiteral("achievements")));
            else if (key == U("шкалы") || key == U("отношения") || key == QLatin1String("meters"))
                action = QStringLiteral("Show(\"genry_meters\", meters=%1)").arg(c.sys(QStringLiteral("meters")));
            else if (key == U("главы") || key == QLatin1String("chapters"))
                action = QStringLiteral("Show(\"genry_chapters\", chs=%1)").arg(c.sys(QStringLiteral("chapters")));
            else if (key == U("настройки") || key == QLatin1String("settings")) action = QStringLiteral("ShowMenu(\"preferences\")");
            else if (key == U("загрузить") || key == U("продолжить") || key == QLatin1String("load")) action = QStringLiteral("ShowMenu(\"load\")");
            else if (key == U("выход") || key == U("выйти") || key == QLatin1String("exit")) action = QStringLiteral("Return(\"__exit\")");
            else {
                const QString lab = c.lab(target.isEmpty() ? QStringLiteral("start") : target);
                action = QStringLiteral("Return(") + pyQ(lab) + QLatin1Char(')');
                // the menu's music fades out: the route plays its own
                returns << QStringLiteral("    if _return == ") + pyQ(lab) + QLatin1Char(':') << QStringLiteral("        stop music fadeout 1.5")
                        << QStringLiteral("        jump ") + lab;
                if (!screenmenuTargets.contains(lab)) screenmenuTargets << lab;
                if (startLab.isEmpty()) startLab = lab;
            }
            buttons << pyUQ(cap) << action;
        }
        if (startLab.isEmpty()) startLab = c.lab(QStringLiteral("start"));
        if (buttons.isEmpty()) buttons << U("u\"Начать\"") << QStringLiteral("Return(%1)").arg(pyQ(startLab)) << U("u\"Выход\"")
                                       << QStringLiteral("Return(\"__exit\")");
        SL label{QStringLiteral("label %1:").arg(modId)};
        label << prelude << QStringLiteral("    call screen ") + c.sys(QStringLiteral("main_menu")) << QStringLiteral("    if _return == \"__exit\":")
              << QStringLiteral("        return") << returns
              << QStringLiteral("    if _return:") << QStringLiteral("        stop music fadeout 1.5")        // a chapter card: its scene
              << QStringLiteral("        jump expression _return")
              << QStringLiteral("    stop music fadeout 1.5") << QStringLiteral("    jump %1").arg(startLab) << QString();
        for (int i = int(label.size()) - 1; i >= 0; --i) out.insert(at, label[i]);
        hasLabel = true;
        const bool esStyle = menuStyle == U("бл") || menuStyle == U("оригинал") || menuStyle == QLatin1String("es");
        const bool sdlStyle = menuStyle == U("7дл") || menuStyle == QLatin1String("7dl");
        const QString corbelFont = QStringLiteral("font \"fonts/corbel.ttf\"");
        const QString click = QStringLiteral("activate_sound \"sound/sfx/click_1.ogg\"");
        auto buttonAction = [&](const QString& word) {       // the action of a button written «кнопка Галерея»
            for (int i = 0; i + 1 < buttons.size(); i += 2)
                if (buttons[i].toLower().contains(word)) return buttons[i + 1];
            return QString();
        };
        if (esStyle) {
            // ES's own board (screens.rpy main_menu imagemap): the same zones, leading into the mod
            const QString gallery = QStringLiteral("Show(\"genry_gallery\", cgs=%1)").arg(c.sys(QStringLiteral("cgs")));
            const QString achs = QStringLiteral("Show(\"genry_achievements\", achs=%1)").arg(c.sys(QStringLiteral("achievements")));
            menuScreen << QString() << QStringLiteral("screen %1():").arg(c.sys(QStringLiteral("main_menu"))) << QStringLiteral("    modal True")
                       << QStringLiteral("    imagemap:")
                       << QStringLiteral("        auto (\"images/gui/title_menu/mainmenu_en_%s.jpg\" if _preferences.language == \"english\" else \"images/gui/title_menu/mainmenu_%s.jpg\")")
                       << QStringLiteral("        hotspot (439, 265, 318, 621) action Return(%1) %2").arg(pyQ(startLab), click)
                       << QStringLiteral("        hotspot (787, 261, 270, 537) action ShowMenu(\"load\") ") + click
                       << QStringLiteral("        hotspot (1067, 748, 252, 312) action ShowMenu(\"preferences\") ") + click
                       << QStringLiteral("        hotspot (1083, 258, 229, 538) action ") + gallery + QLatin1Char(' ') + click
                       << QStringLiteral("        hotspot (1459, 532, 149, 295) action Return(\"__exit\") hovered Play(\"sound\", \"sound/sfx/menu_gate.ogg\")")
                       << QStringLiteral("    imagebutton auto \"images/gui/title_menu/owl_%s.png\" xpos 135 ypos 606 action ") + achs +
                              QStringLiteral(" hovered Play(\"sound\", \"sound/test.ogg\")");
            const QString chapters = buttonAction(U("глав"));
            if (!chapters.isEmpty())
                menuScreen << QStringLiteral("    textbutton u\"Главы\" action ") + chapters + QStringLiteral(" xpos 60 ypos 40 text_size 40 text_color \"#ffffff\" text_hover_color \"#ffd27d\" text_outlines [(3, \"#000000aa\", 0, 0)] background None hover_background None text_font \"fonts/corbel.ttf\" ") + click;
            if (!title.isEmpty())
                menuScreen << QStringLiteral("    frame:") << QStringLiteral("        xalign 0.5") << QStringLiteral("        yalign 0.985")
                           << QStringLiteral("        background Solid(\"#000000a0\")") << QStringLiteral("        padding (30, 10)")
                           << QStringLiteral("        text ") + pyUQ(title) + QStringLiteral(" size 40 color \"#ffd27d\" ") + corbelFont;
        } else if (sdlStyle) {
            // 7DL: a heroine big on the right (the player flips who stands there, remembered),
            // the menu in a column on the left that glows under the cursor
            const QString key = c.sys(QStringLiteral("waifu"));
            menuScreen << QString() << QStringLiteral("screen %1():").arg(c.sys(QStringLiteral("main_menu"))) << QStringLiteral("    modal True")
                       << QStringLiteral("    $ genry_heroes = [%1]").arg(heroes.join(QStringLiteral(", ")))
                       << QStringLiteral("    $ genry_hi = (getattr(persistent, %1, 0) or 0) % max(1, len(genry_heroes))").arg(pyQ(key))
                       << QStringLiteral("    add Solid(\"#00000038\")")
                       << QStringLiteral("    if genry_heroes:") << QStringLiteral("        add genry_heroes[genry_hi] xalign 0.8 yalign 1.0 at genry_waifu_in")
                       << QStringLiteral("    add Solid(\"#0b0f0cb0\", xsize=560, ysize=config.screen_height)");
            for (int k = 0; k < 10; ++k)
                menuScreen << QStringLiteral("    add Solid(\"#0b0f0c%1\", xsize=16, ysize=config.screen_height) xpos %2")
                                  .arg(int(0xb0 * (10 - k) / 11.0), 2, 16, QLatin1Char('0')).arg(560 + 16 * k);
            menuScreen << QStringLiteral("    vbox:") << QStringLiteral("        xpos 150") << QStringLiteral("        yalign 0.55")
                       << QStringLiteral("        xmaximum 520") << QStringLiteral("        spacing 6");
            if (!title.isEmpty())
                menuScreen << QStringLiteral("        text ") + pyUQ(title) + QStringLiteral(" size 70 color \"#ffffff\" outlines [(4, \"#ffd27d40\", 0, 0)] ") + corbelFont +
                                  QStringLiteral(" at genry_menu_in(0.0)")
                           << QStringLiteral("        null height 34");
            for (int i = 0; i + 1 < buttons.size(); i += 2)
                menuScreen << QStringLiteral("        textbutton %1 action %2 at genry_menu_in(%3) text_size 42 text_color \"#e8e8e8\" text_hover_color \"#ffd27d\" text_hover_outlines [(3, \"#ffd27d66\", 0, 0)] background None hover_background None text_font \"fonts/corbel.ttf\" %4")
                                  .arg(buttons[i], buttons[i + 1], pyRepr(0.15 + 0.08 * (i / 2)), click);
            menuScreen << QStringLiteral("    if len(genry_heroes) > 1:")
                       << QStringLiteral("        textbutton \"<\" action SetField(persistent, %1, (genry_hi - 1) % len(genry_heroes)) xalign 0.6 yalign 0.55 text_size 80 text_color \"#ffffff99\" text_hover_color \"#ffd27d\" background None hover_background None text_font \"fonts/corbel.ttf\" %2").arg(pyQ(key), click)
                       << QStringLiteral("        textbutton \">\" action SetField(persistent, %1, (genry_hi + 1) % len(genry_heroes)) xalign 0.985 yalign 0.55 text_size 80 text_color \"#ffffff99\" text_hover_color \"#ffd27d\" background None hover_background None text_font \"fonts/corbel.ttf\" %2").arg(pyQ(key), click);
        }
        // the panel: a dark column with a soft edge, the title (wraps), the author, then the
        // buttons sliding in one after another; hovered = gold dash + a step to the right
        const QString corbel = corbelFont;
        if (!esStyle && !sdlStyle) menuScreen << QString() << QStringLiteral("screen %1():").arg(c.sys(QStringLiteral("main_menu"))) << QStringLiteral("    modal True")
                   << QStringLiteral("    add Solid(\"#0b0f0cd8\", xsize=540, ysize=config.screen_height)");
        for (int k = 0; k < 10; ++k)       // the soft edge: 10 steps of 16px fading out
            menuScreen << QStringLiteral("    add Solid(\"#0b0f0c%1\", xsize=16, ysize=config.screen_height) xpos %2")
                              .arg(int(0xd8 * (10 - k) / 11.0), 2, 16, QLatin1Char('0')).arg(540 + 16 * k);
        menuScreen << QStringLiteral("    vbox:") << QStringLiteral("        xpos 90")
                   << QStringLiteral("        yalign 0.5") << QStringLiteral("        xmaximum 500") << QStringLiteral("        spacing 8");
        if (!logo.isEmpty()) {
            const QString img = logo.contains(QLatin1Char('/')) || logo.contains(QLatin1Char('.')) ? pyQ(relModPath(modId, logo)) : pyQ(logo);
            menuScreen << QStringLiteral("        add Transform(%1, fit=\"contain\", xysize=(460, 220))").arg(img);
        }
        if (!title.isEmpty())
            menuScreen << QStringLiteral("        text ") + pyUQ(title) + QStringLiteral(" size 64 color \"#ffd27d\" outlines [(3, \"#00000088\", 0, 0)] ") + corbel + QStringLiteral(" at genry_menu_in(0.0)");
        if (!meta.author.isEmpty())
            menuScreen << QStringLiteral("        text ") + pyUQ(U("автор: ") + meta.author) + QStringLiteral(" size 22 color \"#a9b9a4\" font \"fonts/calibri.ttf\" at genry_menu_in(0.05)");
        menuScreen << QStringLiteral("        null height 36");
        for (int i = 0; i + 1 < buttons.size(); i += 2) {
            menuScreen << QStringLiteral("        button at genry_menu_in(%1):").arg(pyRepr(0.15 + 0.08 * (i / 2)))
                       << QStringLiteral("            action ") + buttons[i + 1] << QStringLiteral("            background None")
                       << QStringLiteral("            hover_background None") << QStringLiteral("            activate_sound \"sound/sfx/click_1.ogg\"")
                       << QStringLiteral("            hbox at genry_menu_btn:") << QStringLiteral("                spacing 12")
                       << QStringLiteral("                text u\"—\" size 46 color \"#ffd27d00\" hover_color \"#ffd27d\" yalign 0.5 ") + corbel
                       << QStringLiteral("                text ") + buttons[i] + QStringLiteral(" size 46 color \"#eef6ff\" hover_color \"#ffd27d\" yalign 0.5 ") + corbel;
        }
    }

    int firstLabelIndex = -1;
    for (int i = 0; i < out.size(); ++i) if (out[i].startsWith(QLatin1String("label "))) { firstLabelIndex = i; break; }
    const QString modLabel = QStringLiteral("label %1:").arg(modId);
    // V1: the script really starts after the declarations inserted above (legacy: as before)
    const int scriptAt = opt.legacy ? scriptStart : scriptStart + declShift;
    if (!hasLabel) {
        out.insert(scriptAt, modLabel);
    } else {
        SL labels;
        for (const QString& x : out) if (x.startsWith(QLatin1String("label "))) labels << x;
        if (!opt.legacy && labels.contains(modLabel) && firstLabelIndex > scriptAt) {
            // V1: commands above the first scene («музыка …» before ": start") used to stay outside
            // any label - an indented line after `default`, a parse error that stops the WHOLE game.
            // They open the mod instead: moved to the top of the mod's own label.
            SL prelude;
            bool exec = false;
            for (int i = scriptAt; i < firstLabelIndex; ++i) {
                const QString s = pyStrip(out[i]);
                if (s.isEmpty()) continue;
                prelude << out[i];
                if (!s.startsWith(QLatin1Char('#'))) exec = true;
            }
            if (exec) {
                for (int i = firstLabelIndex - 1; i >= scriptAt; --i) out.removeAt(i);
                const int at = int(out.indexOf(modLabel)) + 1;
                for (int i = int(prelude.size()) - 1; i >= 0; --i) out.insert(at, prelude[i]);
            }
        }
        if (!labels.contains(modLabel)) {
            QString firstLabel = modId;
            if (!labels.isEmpty()) {
                firstLabel = pySplit(labels[0], 1).value(1);
                while (firstLabel.endsWith(QLatin1Char(':'))) firstLabel.chop(1);
            }
            if (firstLabelIndex >= 0 && firstLabelIndex > scriptAt) {
                bool hasPrelude = false;
                for (int i = scriptAt; i < firstLabelIndex; ++i) {
                    const QString s = pyStrip(out[i]);
                    if (!s.isEmpty() && !s.startsWith(QLatin1Char('#'))) { hasPrelude = true; break; }
                }
                if (hasPrelude) {
                    out.insert(scriptAt, modLabel);
                    ++firstLabelIndex;
                    QString lastExec;
                    for (int i = firstLabelIndex - 1; i >= scriptAt + 1; --i) {
                        const QString s = pyStrip(out[i]);
                        if (!s.isEmpty() && !s.startsWith(QLatin1Char('#'))) { lastExec = s; break; }
                    }
                    if (!lineIsTerminal(lastExec)) {
                        out.insert(firstLabelIndex, QString());
                        out.insert(firstLabelIndex, QStringLiteral("    jump ") + firstLabel);
                    }
                } else {
                    out.insert(scriptAt, QString());
                    out.insert(scriptAt, QStringLiteral("    jump ") + firstLabel);
                    out.insert(scriptAt, modLabel);
                }
            } else {
                const int idx = int(out.indexOf(labels[0]));
                out.insert(idx, QString());
                out.insert(idx, QStringLiteral("    jump ") + firstLabel);
                out.insert(idx, modLabel);
            }
        }
    }
    out = appendMissingTargetLabels(out, screenmenuTargets);
    out = ensureLabelExits(out, choiceTargets);
    out = collapseRedundantReturns(out);
    if (!opt.legacy) {
        // V1: the dialogue box the way ES itself shows it («window auto»): it melts away before a new background
        // or a menu and comes back with the next line - never an empty box over a fade to black. Every scene turns
        // the mode on (a jump from the app lands mid-mod); the builder's own hide/show keep it on.
        int i = 0;
        while (i < out.size() && !out[i].startsWith(QLatin1String("label "))) ++i;
        // a title card right before a new background: straight from the card's black into the new place
        // (old: the card faded back to the old scene and «фон … fade» went dark a second time)
        for (int k = i; k + 1 < out.size(); ++k) {
            if (out[k] != QLatin1String("    hide genry_black") || out[k + 1] != QLatin1String("    with Dissolve(0.8)")) continue;
            int n = k + 2;
            while (n < out.size() && out[n].startsWith(QLatin1String("    $ ")) && !out[n].contains(QLatin1String("pause"))) ++n;
            if (n < out.size() && out[n].startsWith(QLatin1String("    scene "))) {
                out.removeAt(k + 1);
                out.removeAt(k);
            }
        }
        for (; i < out.size(); ++i) {
            // closed eyes («закрытьглаза») and then a card on black («скачок», «титр»): the lids come off only once the
            // black is really on screen. ES's lids live on the overlay layer, which a transition does not touch - taken
            // off together with the fade, the old scene showed through for a second before going dark.
            if (out[i] == QLatin1String("    scene black") || out[i].startsWith(QLatin1String("    scene black with ")) ||
                out[i] == QLatin1String("    show genry_black")) {
                if (!out[i].contains(QLatin1String(" with ")) && i + 1 < out.size() && out[i + 1].startsWith(QLatin1String("    with "))) ++i;
                if (i + 1 < out.size() && out[i + 1] == QLatin1String("    hide blink onlayer overlay")) continue;
                out.insert(++i, QStringLiteral("    hide blink onlayer overlay"));
                continue;
            }
            if (out[i].startsWith(QLatin1String("    $ new_chapter("))) {
                out.insert(i++, QStringLiteral("    hide blink onlayer overlay"));
                continue;
            }
            if (out[i] == QLatin1String("    window hide")) out[i] = QStringLiteral("    window auto hide");
            else if (out[i] == QLatin1String("    window show")) out[i] = QStringLiteral("    window auto");
            else if (out[i].startsWith(QLatin1String("label ")) && out[i].endsWith(QLatin1Char(':'))) out.insert(++i, QStringLiteral("    window auto"));
        }
    }
    if (!opt.legacy) out << hentaiPatchFallback(out, meta.modId);
    if (usedImageMenu) out << imageMenuScreen();
    if (!opt.legacy) {
        // V1 features: only the definitions and screens this mod really uses
        const QString all = (out + menuScreen).join(QLatin1Char('\n'));
        auto block = [&](const char* rpy) { out << QString::fromUtf8(rpy).split(QLatin1Char('\n')); };
        const bool meters = all.contains(QLatin1String("genry_meter"));
        if (all.contains(QLatin1String("genry_choice_timed"))) block(kV1TimedChoice);
        if (all.contains(QLatin1String("genry_camp_map"))) block(kV1Map);
        if (all.contains(c.sys(QStringLiteral("map_seen")))) out << QString() << QStringLiteral("default %1 = {}").arg(c.sys(QStringLiteral("map_seen")));
        const bool memories = all.contains(QLatin1String("genry_remember(")) || all.contains(QLatin1String("genry_memories"));
        if (meters || memories) block(kV1Popups);
        if (memories) out << QString() << QStringLiteral("default %1 = []").arg(c.sys(QStringLiteral("memories")));
        if (meters) block(kV1Meters);
        if (!meterList.isEmpty()) {
            SL m;
            for (const Meter& x : meterList)
                m << QStringLiteral("(%1, %2, %3, %4, %5)").arg(pyUQ(x.title), pyQ(x.color), pyQ(c.var(x.raw)), x.lo, x.hi);
            out << QString() << QStringLiteral("define %1 = [%2]").arg(c.sys(QStringLiteral("meters")), m.join(QStringLiteral(", ")));
        }
        if (all.contains(c.sys(QStringLiteral("inv"))) || all.contains(QLatin1String("genry_inventory"))) {
            block(kV1Inventory);
            SL m;
            for (const Item& it : itemList)
                m << QStringLiteral("%1: (%2, %3, %4)").arg(pyQ(it.key), pyUQ(it.caption), it.icon.isEmpty() ? QStringLiteral("None") : pyQ(it.icon), pyUQ(it.desc));
            out << QString() << QStringLiteral("define %1 = {%2}").arg(c.sys(QStringLiteral("items")), m.join(QStringLiteral(", ")))
                << QStringLiteral("default %1 = []").arg(c.sys(QStringLiteral("inv")));
        }
        if (all.contains(QLatin1String("genry_gallery"))) {
            block(kV1Gallery);
            SL m;
            for (const QString& cg : cgList) m << pyQ(cg);
            out << QString() << QStringLiteral("define %1 = [%2]").arg(c.sys(QStringLiteral("cgs")), m.join(QStringLiteral(", ")));
        }
        if (all.contains(QLatin1String("genry_achievements"))) {
            block(kV1Achievements);
            SL m;
            for (const auto& a : achList) m << QStringLiteral("(%1, %2)").arg(pyQ(a.first), pyUQ(a.second));
            out << QString() << QStringLiteral("define %1 = [%2]").arg(c.sys(QStringLiteral("achievements")), m.join(QStringLiteral(", ")));
        }
        if (all.contains(QLatin1String("genry_chapters"))) {
            block(kV1Chapters);
            SL m;
            for (const Chapter& ch : chapterList) {
                QString img;
                if (!ch.image.isEmpty()) {
                    QString kind;
                    img = ch.image.contains(QLatin1Char('/')) || ch.image.contains(QLatin1Char('.')) ? relModPath(modId, ch.image)
                                                                                                      : choiceImage(ch.image, &kind, customNames);
                }
                m << QStringLiteral("(%1, %2, %3, %4)").arg(pyQ(c.lab(ch.scene)), pyUQ(ch.title), img.isEmpty() ? QStringLiteral("None") : pyQ(img),
                                                            pyQ(persistentKey(modId, QStringLiteral("ch_") + ch.title, QStringLiteral("chapter"), opt)));
            }
            out << QString() << QStringLiteral("define %1 = [%2]").arg(c.sys(QStringLiteral("chapters")), m.join(QStringLiteral(", ")));
        }
        if (all.contains(QLatin1String("genry_say_skin"))) block(kV1SaySkins);
        if (all.contains(QLatin1String("genry_flashback_frame"))) block(kV1Flashback);
        out << v1WeatherImages(all, modId, c);
        if (all.contains(QLatin1String("genry_parallax"))) block(kV1Parallax);
        if (all.contains(QLatin1String("genry_flap_frame"))) block(kV1SplitFlap);
        const bool feed = all.contains(QLatin1String("genry_phone_feed")) || all.contains(c.sys(QStringLiteral("feed")));
        const bool phone = feed || all.contains(QLatin1String("genry_phone2")) || all.contains(QLatin1String("genry_phone_call")) ||
                           all.contains(QLatin1String("genry_phone_reply")) || all.contains(QLatin1String("genry_ph_banner")) ||
                           all.contains(QLatin1String("genry_phone_home"));
        if (phone) {
            block(kV1Phone);
            block(kV1PhoneChat);
            out << v1PhoneImages(modId);
        }
        if (all.contains(QLatin1String("genry_phone_call"))) block(kV1PhoneCall);
        if (feed) {
            block(kV1PhoneFeed);
            out << QString() << QStringLiteral("default %1 = []").arg(c.sys(QStringLiteral("feed")));
        }
        if (all.contains(QLatin1String("genry_phone_home"))) block(kV1PhoneHome);
        if (!menuScreen.isEmpty()) {
            block(kV1MenuButton);
            out << menuScreen;
        }
        // «гардероб мастерской»: every sprite the mod shows that only the workshop layers make
        // (a show, a Fixed() overlay base, a picture of a choice or of the phone) gets ES's own kind
        // of definition; the builder puts the layers into the mod
        if (const Wardrobe* wr = wardrobe()) {
            static const QRegularExpression showRe(
                QStringLiteral("^\\s*show ([a-z][a-z0-9_]*(?: [a-z0-9_]+)+?)(?= at | with | as | behind | onlayer | zorder |:| *$)"));
            static const QRegularExpression quoted(QStringLiteral("\"([a-z][a-z0-9_]*(?: [a-z0-9_]+)+)\""));
            QSet<QString> seen;
            SL decl;
            auto consider = [&](const QString& n) {
                if (seen.contains(n) || customNames.contains(n.toLower())) return;
                seen.insert(n);
                const QString d = wr->definition(n, modId);
                if (!d.isEmpty()) decl << QStringLiteral("    ") + d;
            };
            for (const QString& l : out) {
                const auto m = showRe.match(l);
                if (m.hasMatch()) consider(m.captured(1));
                for (auto it = quoted.globalMatch(l); it.hasNext();) consider(it.next().captured(1));
            }
            decl.sort();
            for (int i = int(decl.size()) - 1; i >= 0; --i) out.insert(declareAt, decl[i]);
        }
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString compileText(const QString& storyText, const CompileOptions& opt, QString* error, const QVector<CustomImage>& images)
{
    QStringList body;
    const ModMeta meta = parseMeta(pySplitLines(stripBom(storyText)), &body, opt);
    return compileStory(meta, body, opt, images, error);
}

} // namespace gb
