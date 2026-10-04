#include "Scene.h"
#include "Compiler.h"
#include "EsAssets.h"
#include "Py.h"
#include "Text.h"
#include "Weather.h"
#include "Overlays.h"
#include "Screenplay.h"

#include <QRegularExpression>

namespace gb {

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

// ES's own position transforms (media.rpy): xalign N, xanchor 0.5, yanchor 0.0
bool esPosition(const QString& p, double* x)
{
    static const QHash<QString, double> pos{{QStringLiteral("center"), 0.5},   {QStringLiteral("truecenter"), 0.5}, {QStringLiteral("left"), 0.28},
                                            {QStringLiteral("right"), 0.72},   {QStringLiteral("fleft"), 0.16},     {QStringLiteral("fright"), 0.84},
                                            {QStringLiteral("cleft"), 0.355},  {QStringLiteral("cright"), 0.645}};
    auto it = pos.constFind(p);
    if (it == pos.constEnd()) return false;
    *x = *it;
    return true;
}

bool num(const QString& s, double* v)
{
    return pyIsNumber(s) && pyFloat(s, v);
}

QStringList pipes(const QString& rest)
{
    QStringList out;
    for (const QString& p : rest.split(QLatin1Char('|'))) if (!p.trimmed().isEmpty()) out << p.trimmed();
    return out;
}

QString tagOf(const QString& image) { return image.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty); }

bool tints(const QString& image)
{
    const QString t = tagOf(image).toLower();
    return !(t.isEmpty() || t == QLatin1String("black") || t == QLatin1String("white") || t == QLatin1String("blink") ||
             t == QLatin1String("unblink") || t == QLatin1String("prologue_dream") || t.startsWith(QLatin1String("genry_")));
}

// ATL "xalign X / yalign 1.0" placement used by the Genry sprite commands
SpriteShow aligned(const QString& image, const QString& tag, double xalign, double zoom = 1.0, double alpha = 1.0, bool mirror = false)
{
    SpriteShow s;
    s.image = image;
    s.tag = tag.isEmpty() ? tagOf(image) : tag;
    s.xpos = s.xanchor = xalign;
    s.ypos = s.yanchor = 1.0;
    s.zoom = zoom;
    s.alpha = alpha;
    s.mirror = mirror;
    s.timeTint = tints(image);
    return s;
}

void put(SceneState& st, const SpriteShow& s)
{
    for (SpriteShow& o : st.sprites)
        if (o.tag == s.tag) { o = s; return; }
    st.sprites.push_back(s);
}

void hideTag(SceneState& st, const QString& tag)
{
    st.sprites.erase(std::remove_if(st.sprites.begin(), st.sprites.end(), [&](const SpriteShow& s) { return s.tag == tag; }), st.sprites.end());
}

QString alias(const QString& base, const QString& prefix)
{
    QString b = base;
    b.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_')).replace(QLatin1Char('.'), QLatin1Char('_'));
    return prefix + QLatin1Char('_') + slug(b, QStringLiteral("sprite"), false).left(64);
}

void clearTransient(SceneState& st)
{
    st.cardKind.clear();
    st.cardText.clear();
    st.cardSub.clear();
    st.choices.clear();
    st.choiceStyle.clear();
    st.choiceImages.clear();
    st.choiceKinds.clear();
    st.choiceHints.clear();
    st.choiceAsked = false;
    st.menuTitle.clear();
    st.screenMenu = false;
    st.notifyTitle.clear();
    st.notifyText.clear();
    st.floating.clear();
    st.flash = false;
    st.map = false;
    st.nvlText.clear();
    st.musicPlayer.clear();
    st.achievement.clear();
    st.achievementPlate.clear();
    st.codeLock.clear();
    st.postcard = {};
    st.videoCard.clear();
    st.moment.clear();
    st.sound.clear();
    st.remember.clear();
    st.meterPing.clear();
    st.showMeters = st.showInventory = st.modMenuOpen = st.showGallery = st.showAchievements = false;
    st.phoneCall.clear();
    st.phoneCallFace.clear();
    st.feedOpen = false;
    st.phoneHome = false;
    st.pushWho.clear();
    st.pushText.clear();
}

void clearSay(SceneState& st)
{
    st.textPages = st.textPage = 1;
    st.textBoxes.clear();
    st.speakerId.clear();
    st.speakerName.clear();
    st.text.clear();
    st.thought = false;
}

void sayAdv(SceneState& st, const QString& who, const QString& text, const EsAssets* es);

// «[имя]», «[имя кому]», «[проснулся/проснулась]» the way the mod will show them (kV1Hero)
QString heroText(const QString& text, const SceneState& st)
{
    if (!text.contains(QLatin1Char('['))) return text;
    static const QRegularExpression br(QStringLiteral("(?<!\\[)\\[([^\\[\\]]*)\\]"));
    const QString hero = st.playerName.isEmpty() ? U("Семён") : st.playerName;
    QString out;
    int last = 0;
    for (auto it = br.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString inner = m.captured(1).trimmed(), low = inner.toLower();
        const QString w0 = low.section(QLatin1Char(' '), 0, 0);
        out += text.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        if (w0 == U("имя") || w0 == U("игрок")) {
            const int c = nameCaseIn(low.section(QLatin1Char(' '), 1), out);
            if (c >= 0) { out += declineName(hero, c, st.playerShe); continue; }
        }
        if (inner.count(QLatin1Char('/')) == 1) { out += inner.section(QLatin1Char('/'), st.playerShe ? 1 : 0, st.playerShe ? 1 : 0); continue; }
        out += m.captured(0);
    }
    return out + text.mid(last);
}

// in NVL mode ES stacks the lines on one page instead of the dialogue box
void say(SceneState& st, const QString& who, const QString& text, const EsAssets* es)
{
    sayAdv(st, who, text, es);
    // «незнакомка»: under the name the hero knows her by; she says her own name - from the next line she is herself
    if (st.strangers.contains(st.speakerId)) {
        const QString real = st.speakerName;
        st.speakerName = st.strangers.value(st.speakerId);
        const QString said = text.toLower();
        if ((!real.isEmpty() && said.contains(real.toLower())) || said.contains(pyStrip(who).toLower())) st.strangers.remove(st.speakerId);
    }
    if (!st.nvlMode) return;
    // the NVL page holds every box of a long line
    for (const QString& box : st.textBoxes.isEmpty() ? QStringList{st.text} : st.textBoxes)
        st.nvlPage << (st.speakerName + QLatin1Char('|') + st.speakerColor + QLatin1Char('|') + box);
    while (st.nvlPage.size() > 12) st.nvlPage.removeFirst();
    st.text.clear();
}

void sayAdv(SceneState& st, const QString& who, const QString& text, const EsAssets* es)
{
    CompileState cs;
    const QString id = speakerId(who, cs, {});
    st.text = heroText(text, st);
    // a line longer than the box: the game shows it in several boxes - the preview shows the first and «1/N»
    const QStringList boxes = splitForBox(st.text);
    st.textPages = int(boxes.size());
    st.textPage = 1;
    st.textBoxes = boxes.size() > 1 ? boxes : QStringList();
    st.text = boxes.value(0);
    st.speakerId = id;
    st.whatColor.clear();
    st.thought = false;
    if (st.customSpeakers.contains(pyStrip(who).toLower())) {
        const QStringList nc = st.customSpeakers.value(pyStrip(who).toLower()).split(QLatin1Char('|'));
        st.speakerName = nc.value(0);
        st.speakerColor = nc.value(1);
        st.whatColor = QStringLiteral("#f1d076");
        return;
    }
    if (id == QLatin1String("genry")) { st.speakerName = U("Генри"); st.speakerColor = QStringLiteral("#ffcc66"); return; }
    if (id == QLatin1String("scar")) { st.speakerName = U("Шрам"); st.speakerColor = QStringLiteral("#ff6666"); return; }
    if (es && es->characters.contains(id) && !es->characterName(id).isEmpty()) {
        st.speakerName = id == QLatin1String("me") && !st.playerName.isEmpty() ? st.playerName : es->characterName(id);
        st.speakerColor = es->characterColor(id, st.timeOfDay);
        return;
    }
    st.speakerName = pyStrip(who);          // unknown: shown as typed (V1 declares it)
    st.speakerColor = QStringLiteral("#c0c0c0");
}

} // namespace

SceneState sceneAt(const QString& storyText, int upto, const EsAssets* es)
{
    SceneState st;
    // «пиши как сценарий» lines become the commands the compiler will see; `upto` counts the story's own lines
    const QStringList srcLines = pySplitLines(stripBom(storyText));
    QVector<int> srcOf;
    const QStringList lines = expandScreenplay(srcLines, &srcOf);
    const int srcN = upto < 0 ? int(srcLines.size()) : qMin(upto, int(srcLines.size()));
    int n = 0;
    while (n < lines.size() && srcOf[n] < srcN) ++n;
    // «выбор»: the menu the cursor stands at (drawn over the frame after all the lines), the branches skipped
    struct Menu {
        bool on = false;
        QString style, seconds;
        QStringList choices, images, kinds, hints;
        int hover = 0;
        bool asked = false;
    } menu;
    QHash<int, int> skipFrom;
    // V1: what the whole story declares - meters, items, the mod's CGs and achievements
    QHash<QString, QString> itemCaption;
    struct AchDecl { QString key, title; bool hidden = false, plat = false; QString image; };
    QVector<AchDecl> achievementsDecl;                           // Достижения 2.0, one per key
    auto fields = [](const QString& r) {
        QStringList f;
        for (const QString& x : r.split(QLatin1Char('|'))) f << pyStrip(x);
        return f;
    };
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        const QString w0 = firstWord(s);
        const QString c0 = normalizeCommand(w0);
        const QString r0 = pyStrip(s.mid(w0.size()));
        if (c0 == QLatin1String("meter")) {
            const QStringList f = fields(r0);
            if (f.value(0).isEmpty()) continue;
            const QString v = slug(f[0], QStringLiteral("value"), false);
            st.meters.push_back({f.value(1).isEmpty() ? f[0] : f[1], pyIsHexColor(f.value(2)) ? f[2] : QStringLiteral("#ffd27d"), v,
                                 pyIsNumber(f.value(3)) ? f[3] : QStringLiteral("0"), pyIsNumber(f.value(4)) ? f[4] : QStringLiteral("10")});
            st.meterValues.insert(v, qMax(0.0, f.value(3).toDouble()));
        } else if (c0 == QLatin1String("itemget")) {
            const QStringList f = fields(r0);
            if (!f.value(0).isEmpty()) itemCaption.insert(slug(f[0], QStringLiteral("item"), false), f.value(1).isEmpty() ? f[0] : f[1]);
        } else if (c0 == QLatin1String("unlockachievement")) {
            const AchSpec a = parseAchievement(r0);
            if (a.key.isEmpty()) continue;
            bool seen = false;
            const QString pic = !a.image.isEmpty() ? a.image : a.icon;
            for (AchDecl& d : achievementsDecl)
                if (d.key == a.key) {
                    seen = true;
                    d.hidden = d.hidden || a.hidden;
                    d.plat = d.plat || a.plat;
                    if (d.image.isEmpty()) d.image = pic;
                }
            if (!seen) achievementsDecl.push_back({a.key, a.title, a.hidden, a.plat, pic});
        } else if (c0 == QLatin1String("cg")) {
            QStringList ww = pySplit(r0);
            if (!ww.isEmpty() && isEffect(ww.last())) ww.removeLast();
            const QString name = QStringLiteral("cg ") + ww.join(QLatin1Char(' '));
            if (!ww.isEmpty() && !st.galleryCgs.contains(name)) st.galleryCgs << name;
        }
    }
    auto meterOf = [&](const QString& v) -> int {
        for (int k = 0; k < st.meters.size(); ++k) if (st.meters[k].value(2) == v) return k;
        return -1;
    };
    QSet<QString> achieved;
    bool inModMenu = false;
    QString metaAuthor, metaName;
    for (int i = 0; i < n; ++i) {
        const QString s = pyStrip(lines[i]);
        if (!s.startsWith(QLatin1Char('@'))) continue;
        const QString key = pySplit(s.mid(1), 1).value(0).toLower();
        const QString value = pyStrip(s.mid(1 + key.size()));
        if (key == QLatin1String("author")) metaAuthor = value;
        else if (key == QLatin1String("mod_name")) metaName = value;
        else if (key == QLatin1String("hero_name")) st.playerName = value;      // the author's hero instead of Семён
        else if (key == QLatin1String("hero_gender")) st.playerShe = value.toLower().startsWith(U("она")) || value.toLower().startsWith(U("жен"));
    }
    for (int i = 0; i < n; ++i) {
        if (const auto sk = skipFrom.constFind(i); sk != skipFrom.constEnd()) {
            i = *sk - 1;
            continue;
        }
        const QString s = pyStrip(lines[i]);
        st.line = srcOf[i] + 1;
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@'))) continue;
        clearTransient(st);
        if (s.startsWith(QLatin1Char(':'))) continue;           // a scene label: the picture carries on

        const QString first = firstWord(s);
        const QString cmd = normalizeCommand(first);
        const QString rest = pyStrip(s.mid(first.size()));
        QStringList w = pySplit(rest);

        // V1 «менюмода … конецменюмода»: the mod's own main menu while the cursor is inside
        if (cmd == QLatin1String("modmenu")) {
            inModMenu = true;
            st.modMenuTitle = metaName;
            st.modMenuStyle = menuStyleKey(rest);
            st.modMenuHeroes.clear();
            st.modMenuKinds.clear();
            st.modMenuLogo.clear();
            st.modMenuAuthor = metaAuthor;
            st.modMenuButtons.clear();
            st.modMenuParts = MenuParts();
            st.modMenuOpen = true;
            continue;
        }
        if (inModMenu) {
            st.modMenuOpen = true;
            if (cmd == QLatin1String("endmodmenu") || cmd == QLatin1String("endchoice")) { inModMenu = false; continue; }
            const QString lw = first.toLower();
            if (menuPartLine(lw, rest, &st.modMenuParts)) continue;
            if (lw == U("заголовок") || lw == QLatin1String("title")) { st.modMenuTitle = rest; continue; }
            if (lw == U("лого") || lw == U("логотип") || lw == QLatin1String("logo")) { st.modMenuLogo = rest; continue; }
            if (lw == U("автор") || lw == QLatin1String("author")) {
                static const QSet<QString> none{U("нет"), U("скрыть"), U("убрать"), U("без"), QStringLiteral("-"), QStringLiteral("none"), QStringLiteral("no")};
                st.modMenuAuthor = none.contains(rest.toLower()) ? QString() : rest;
                continue;
            }
            if (lw == U("кнопка") || lw == QLatin1String("button")) {
                static const QRegularExpression nameTag(U("\\[(имя|игрок)\\]"), QRegularExpression::CaseInsensitiveOption);
                const QString cap = pyStrip(rest.section(QStringLiteral("->"), 0, 0));
                const QString target = rest.contains(QLatin1String("->")) ? pyStrip(rest.section(QStringLiteral("->"), 1)) : QString();
                st.modMenuButtons << QString(cap).replace(nameTag, st.playerName.isEmpty() ? U("Семён") : st.playerName);
                st.modMenuKinds << menuButtonKind(target.isEmpty() ? cap : target);
                continue;
            }
            if (lw == U("герои") || lw == U("героини") || lw == U("вайфу") || lw == QLatin1String("heroes")) {
                for (const QString& h : rest.split(QLatin1Char('|'))) if (!pyStrip(h).isEmpty()) st.modMenuHeroes << pyStrip(h);
                continue;
            }
            if (lw == U("стиль") || lw == QLatin1String("style")) { st.modMenuStyle = menuStyleKey(rest); continue; }
            // фон / музыка / показать … fall through: the menu stands on them
        }
        if (cmd == QLatin1String("meter")) continue;
        if (cmd == QLatin1String("remember")) {
            const QStringList f = fields(rest);
            st.remember = f.size() > 1 && !f[1].isEmpty() ? f[1] : (f.value(0).isEmpty() ? U("Это запомнят.") : f[0] + U(" это запомнит."));
            continue;
        }
        if (cmd == QLatin1String("meters")) {
            const QString a = w.value(0).toLower();
            if (a == U("кнопка") || a == QLatin1String("button")) st.metersButton = true;
            else if (a == U("скрыть") || a == U("убрать") || a == QLatin1String("hide")) st.metersButton = false;
            else st.showMeters = true;
            continue;
        }
        if ((cmd == QLatin1String("addvar") || cmd == QLatin1String("setvar")) && w.size() >= 2) {
            const QString v = slug(w[0], QStringLiteral("value"), false);
            const int k = meterOf(v);
            if (k >= 0) {
                double d = 0;
                pyFloat(w[1], &d);
                const double lo = st.meters[k].value(3).toDouble(), hi = st.meters[k].value(4).toDouble();
                double val = cmd == QLatin1String("addvar") ? st.meterValues.value(v) + d : d;
                val = qBound(lo, val, hi);
                st.meterValues.insert(v, val);
                if (cmd == QLatin1String("addvar"))
                    st.meterPing = {st.meters[k].value(0), st.meters[k].value(1), w[1], pyRepr(val), st.meters[k].value(3), st.meters[k].value(4)};
            }
            continue;
        }
        if (cmd == QLatin1String("itemget")) {
            const QStringList f = fields(rest);
            const QString cap = itemCaption.value(slug(f.value(0), QStringLiteral("item"), false), f.value(0));
            if (!cap.isEmpty() && !st.inventory.contains(cap)) st.inventory << cap;
            st.notifyTitle = U("Инвентарь");
            st.notifyText = U("Предмет: ") + cap;
            continue;
        }
        if (cmd == QLatin1String("itemremove")) {
            st.inventory.removeAll(itemCaption.value(slug(w.value(0), QStringLiteral("item"), false), w.value(0)));
            continue;
        }
        if (cmd == QLatin1String("inventory")) {
            const QString a = w.value(0).toLower();
            if (a == U("кнопка") || a == QLatin1String("button")) st.inventoryButton = true;
            else if (a == U("скрыть") || a == U("убрать") || a == QLatin1String("hide")) st.inventoryButton = false;
            else st.showInventory = true;
            continue;
        }
        if (cmd == QLatin1String("gallery")) { st.showGallery = true; continue; }
        if (cmd == QLatin1String("achievements")) {
            st.achievements.clear();
            for (const AchDecl& a : achievementsDecl)
                st.achievements << a.title + (achieved.contains(a.key) ? QStringLiteral("|1") : QStringLiteral("|0")) + (a.hidden ? QStringLiteral("|h|") : QStringLiteral("||")) + a.image;
            st.showAchievements = true;
            continue;
        }
        if (cmd == QLatin1String("postcard")) {                       // V2.1.2 the card in focus, face first
            const PostcardSpec p = parsePostcard(rest);
            st.postcard = {true, p.backFirst, p.front, QString(p.text).replace(QStringLiteral("\\n"), QStringLiteral("\n")), p.sign, p.to, p.caption};
            continue;
        }
        if (cmd == QLatin1String("codelock")) {
            const CodeLockSpec k = parseCodeLock(rest);
            st.codeLock = (k.hint.isEmpty() ? U("Какой код?") : k.hint) + QLatin1Char('|') + QString::number(k.code.size()) + QLatin1Char('|') + QString::number(k.tries);
            continue;
        }
        if (cmd == QLatin1String("flashlight")) {
            const FlashSpec f = parseFlashlight(rest);
            if (f.ok) {
                st.flashlight = !f.off;
                st.flashZoom = f.zoom;
                st.flashColor = f.color;
                continue;
            }
        }
        if (cmd == QLatin1String("unlockachievement")) {
            // ES's own plate slides in (not for «платина»: it comes by itself)
            const AchSpec a = parseAchievement(rest);
            if (!a.plat && !achieved.contains(a.key)) st.achievementPlate = a.title;
            if (!a.plat) achieved.insert(a.key);
            bool all = !achievementsDecl.isEmpty();
            for (const AchDecl& d : achievementsDecl) if (!d.plat && !achieved.contains(d.key)) all = false;
            for (const AchDecl& d : achievementsDecl)
                if (d.plat && all && !achieved.contains(d.key)) { achieved.insert(d.key); st.achievementPlate = d.title; }
            continue;
        }

        if (cmd == QLatin1String("endchoice")) continue;
        if (isChoiceItemLine(s) || (s.contains(QLatin1String("->")) && isTimeoutWord(s.section(QStringLiteral("->"), 0, 0)))) continue;
        if (cmd == QLatin1String("choice")) {
            // the block: its own options (a choice under an option is that option's), where it ends
            QVector<int> itemAt;
            int end = int(lines.size()), depth = 1;
            bool endLine = false;
            for (int k = i + 1; k < lines.size(); ++k) {
                const QString t = pyStrip(lines[k]);
                const QString ck = normalizeCommand(firstWord(t));
                if (t.startsWith(QLatin1Char(':')) || ck == QLatin1String("label")) { end = k; break; }
                if (ck == QLatin1String("choice")) { ++depth; continue; }
                if (ck == QLatin1String("endchoice")) {
                    if (--depth == 0) { end = k; endLine = true; break; }
                    continue;
                }
                if (depth == 1 && isChoiceItemLine(t)) itemAt << k;
            }
            const int cur = n - 1;
            if (itemAt.isEmpty()) continue;
            const int after = endLine ? end + 1 : end;
            if (cur >= after) {                     // the cursor is past it: none of the branches is on screen
                skipFrom.insert(itemAt[0], after);
                continue;
            }
            // the cursor in an option's own lines: that branch, as if the player chose it
            int inside = -1;
            for (int k = 0; k < itemAt.size(); ++k) {
                const int to = k + 1 < itemAt.size() ? itemAt[k + 1] : end;
                if (cur > itemAt[k] && cur < to) inside = k;
            }
            const QString curLine = cur >= 0 ? pyStrip(lines[cur]) : QString();
            if (inside >= 0 && !(curLine.contains(QLatin1String("->")) && isTimeoutWord(curLine.section(QStringLiteral("->"), 0, 0)))) {
                if (itemAt[inside] + 1 > itemAt[0]) skipFrom.insert(itemAt[0], itemAt[inside] + 1);
                continue;
            }
            // the menu: the question lines before the options happen, the options' own lines do not
            skipFrom.insert(itemAt[0], after);
            const ChoiceHead h = parseChoiceHead(rest);
            menu = Menu();
            menu.on = true;
            menu.style = h.style;
            menu.seconds = h.secs;
            menu.asked = itemAt[0] > i + 1 && !h.random && (h.style == QLatin1String("es") || h.style == QLatin1String("buttons"));
            for (int k = 0; k < itemAt.size(); ++k) {
                const ChoiceItemSpec it = parseChoiceItem(pyStrip(lines[itemAt[k]]));
                QString image, kind;
                if (!it.image.isEmpty()) {
                    image = choiceImage(it.image, &kind);
                    if (menu.style != QLatin1String("timed") && menu.style != QLatin1String("phone")) menu.style = QStringLiteral("images");
                }
                menu.choices << (it.caption.isEmpty() ? QStringLiteral("...") : it.caption);
                menu.images << image;
                menu.kinds << kind;
                menu.hints << (it.need.isEmpty() ? QString() : (it.hint.isEmpty() ? U("нужно: ") + it.need : it.hint));
                if (itemAt[k] == cur) menu.hover = k;
            }
            continue;
        }
        if (s.contains(QLatin1Char(':')) && !isCommandName(cmd) && !s.toLower().startsWith(QLatin1String("http"))) {
            say(st, s.section(QLatin1Char(':'), 0, 0), pyStrip(s.section(QLatin1Char(':'), 1)), es);
            continue;
        }
        auto popEffect = [&] { if (!w.isEmpty() && isEffect(w.last())) w.removeLast(); };

        if (cmd == QLatin1String("extend")) {            // the same box, the same speaker, the text goes on
            if (!rest.isEmpty()) st.text += (rest.front().isSpace() ? QString() : QStringLiteral(" ")) + heroText(rest, st);
            continue;
        }
        if (cmd == QLatin1String("say")) {
            clearSay(st);
            st.text = rest;
            if (st.nvlMode) { st.nvlPage << QStringLiteral("||") + rest; st.text.clear(); }
            continue;
        }
        if (cmd == QLatin1String("stranger") || cmd == QLatin1String("meet")) {
            CompileState cs;
            const QString id = speakerId(pyStrip(rest.section(QLatin1Char('|'), 0, 0)), cs, {});
            const QString shown = pyStrip(rest.section(QLatin1Char('|'), 1));
            if (cmd == QLatin1String("meet") || shown.isEmpty()) st.strangers.remove(id);
            else st.strangers.insert(id, shown);
            continue;
        }
        if (cmd == QLatin1String("character")) {
            QStringList v = w;
            if (!v.isEmpty()) {
                const QString id = slug(v[0], QStringLiteral("char"), false);
                QString color = QStringLiteral("#008000");
                QStringList np = v.mid(1);
                if (!np.isEmpty() && pyIsHexColor(np.last())) color = np.takeLast();
                const QString name = np.isEmpty() ? id : np.join(QLatin1Char(' '));
                st.customSpeakers.insert(id.toLower(), name + QLatin1Char('|') + color);
                st.customSpeakers.insert(name.toLower(), name + QLatin1Char('|') + color);
            }
            continue;
        }
        if (cmd == QLatin1String("voicedsay") || cmd == QLatin1String("punchedsay")) {
            const QStringList p = pipes(rest);
            if (p.size() >= 3) say(st, p[0], p.mid(2).join(QStringLiteral(" | ")), es);
            else if (w.size() >= 3) say(st, w[0], pyStrip(pySplit(rest, 2).value(2)), es);
            const QString second = p.size() >= 3 ? p[1] : w.value(1);
            if (cmd == QLatin1String("voicedsay")) st.sound = pyBasename(second);
            else st.moment = second.contains(QLatin1String("vpunch")) ? U("удар ↕") : U("удар ↔");
            continue;
        }
        if (cmd == QLatin1String("bg") || cmd == QLatin1String("cg") || cmd == QLatin1String("showbg") || cmd == QLatin1String("scene")) {
            if (cmd != QLatin1String("scene")) popEffect();
            QString image = w.join(QLatin1Char(' '));
            const QString low = image.toLower();
            st.videoBg.clear();
            if (cmd == QLatin1String("bg") || cmd == QLatin1String("showbg")) {
                // as the compiler does it: the heroines take the picture's time, «время ночь» + a day picture = its night one
                QString t = bgTimeOf(image);
                if (!t.isEmpty() && st.timeExplicit && t != st.spriteTime) {
                    const QString other = bgAtTime(image, st.spriteTime);
                    if (!other.isEmpty()) {
                        image = other;
                        t = st.spriteTime;
                    }
                }
                if (!t.isEmpty() && t != st.spriteTime) st.timeExplicit = false;
                if (!t.isEmpty()) st.timeOfDay = st.spriteTime = t;
            }
            if (cmd == QLatin1String("scene")) st.bg = rest;
            else if (cmd == QLatin1String("cg")) st.bg = QStringLiteral("cg ") + image;
            else st.bg = (low == QLatin1String("black") || low == QLatin1String("white")) ? low : QStringLiteral("bg ") + image;
            if (cmd != QLatin1String("showbg")) {
                st.sprites.clear();
                st.dream = false;
                clearSay(st);
            }
            continue;
        }
        if (cmd == QLatin1String("show")) {
            popEffect();
            QString pos;
            if (w.size() >= 2 && w.at(w.size() - 2) == QLatin1String("at") && isPosition(w.last())) { pos = w.takeLast(); w.removeLast(); }
            else if (!w.isEmpty() && isPosition(w.last())) pos = w.takeLast();
            const QString image = withOverlays(w.join(QLatin1Char(' ')));     // «румянец пот» -> genry_ov_blush_sweat
            if (image.isEmpty()) continue;
            const QString tag = tagOf(image);
            if (tag == QLatin1String("prologue_dream")) { st.dream = true; st.dreamAlpha = 1.0; continue; }
            SpriteShow sp;
            for (const SpriteShow& o : st.sprites) if (o.tag == tag) sp = o;     // Ren'Py keeps the old placement
            sp.tag = tag;
            sp.image = image;
            sp.timeTint = tints(image);
            double x;
            if (!pos.isEmpty() && esPosition(pos, &x)) {
                sp.xpos = x;
                sp.xanchor = 0.5;
                sp.ypos = pos == QLatin1String("truecenter") ? 0.5 : 0.0;
                sp.yanchor = pos == QLatin1String("truecenter") ? 0.5 : 0.0;
                sp.zoom = 1.0;
                sp.alpha = 1.0;
                sp.rotate = 0.0;
                sp.mirror = false;
            }
            put(st, sp);
            continue;
        }
        if (cmd == QLatin1String("keyframe")) {                      // V2.1.2 «ключ»: where the animation ends
            const Keyframe k = parseKeyframe(rest);
            const QString image = withOverlays(k.image);
            if (image.isEmpty()) continue;
            const QString tag = tagOf(image);
            SpriteShow sp;                                            // not shown yet: ES's «center»
            bool shown = false;
            for (const SpriteShow& o : st.sprites) if (o.tag == tag) { sp = o; shown = true; }
            sp.tag = tag;
            // «ключ sl | …»: the tag alone - Ren'Py shows the face it has (a later emotion is not undone)
            if (!shown || image.contains(QLatin1Char(' '))) { sp.image = image; sp.timeTint = tints(image); }
            if (k.hasX) { sp.xpos = k.x; sp.xanchor = 0.5; }
            if (k.hasY) { sp.ypos = k.y; sp.yanchor = 0.0; }
            if (k.hasZoom) sp.zoom = k.zoom;
            if (k.hasRotate) sp.rotate = k.rotate;
            if (k.hasAlpha) sp.alpha = k.alpha;
            if (k.hasFlip) sp.mirror = k.flip;
            put(st, sp);
            continue;
        }
        if (cmd == QLatin1String("hide")) {
            popEffect();
            if (!w.isEmpty()) hideTag(st, w[0]);
            continue;
        }
        if (cmd == QLatin1String("hideall")) {
            for (const char* t : {"dv", "un", "sl", "mi", "us", "mt", "el", "sh", "mz", "uv", "cs", "genry", "scar"}) hideTag(st, U(t));
            continue;
        }
        double v;
        if (cmd == QLatin1String("walk") && w.size() >= 4) {
            QString to = w.at(w.size() - 2);
            if (!isWalkPosition(to)) to = QStringLiteral("center");
            put(st, aligned(w.mid(0, w.size() - 3).join(QLatin1Char(' ')), {}, walkXalign(to)));
            continue;
        }
        if (cmd == QLatin1String("bigshow") && !w.isEmpty()) {
            popEffect();
            if (w.size() >= 6 && isWalkPosition(w.at(w.size() - 5)) && isWalkPosition(w.at(w.size() - 4)) && num(w.at(w.size() - 3), &v)) {
                double zoom = 1.25, alpha = 1.0, secs = 0;
                num(w.last(), &zoom);
                num(w.at(w.size() - 2), &alpha);
                num(w.at(w.size() - 3), &secs);
                const QString to = w.at(w.size() - 4), from = w.at(w.size() - 5);
                put(st, aligned(w.mid(0, w.size() - 5).join(QLatin1Char(' ')), {}, walkXalign(secs > 0 ? to : from), zoom, alpha));
                continue;
            }
            double zoom = 1.25;
            if (!w.isEmpty() && num(w.last(), &v)) { zoom = v; w.removeLast(); }
            QString pos = QStringLiteral("center");
            if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
            if (!w.isEmpty()) put(st, aligned(w.join(QLatin1Char(' ')), {}, walkXalign(pos), zoom));
            continue;
        }
        if ((cmd == QLatin1String("mirror") || cmd == QLatin1String("mirrorbig")) && !w.isEmpty()) {
            popEffect();
            double zoom = cmd == QLatin1String("mirror") ? 1.0 : 1.25;
            if (!w.isEmpty() && num(w.last(), &v)) { zoom = v; w.removeLast(); }
            QString pos = QStringLiteral("right");
            if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
            const QString image = w.join(QLatin1Char(' '));
            if (!image.isEmpty()) put(st, aligned(image, alias(image + QLatin1Char('_') + pos, QStringLiteral("genry_mirror")), walkXalign(pos), zoom, 1.0, true));
            continue;
        }
        if (cmd == QLatin1String("conflictfocus")) {
            const QStringList p = pipes(rest);
            if (p.size() >= 2) {
                const QString ap = p.size() > 2 && isWalkPosition(p[2]) ? p[2] : QStringLiteral("left");
                const QString pp = p.size() > 3 && isWalkPosition(p[3]) ? p[3] : QStringLiteral("right");
                double az = 1.12, pz = 0.96;
                if (p.size() > 4) num(p[4], &az);
                if (p.size() > 5) num(p[5], &pz);
                auto rightish = [](const QString& x) { return x == QLatin1String("right") || x == QLatin1String("fright") || x == QLatin1String("cright"); };
                put(st, aligned(p[1], alias(p[1] + QStringLiteral("_passive"), QStringLiteral("genry_focus")), walkXalign(pp), pz, 0.72, rightish(pp)));
                put(st, aligned(p[0], alias(p[0] + QStringLiteral("_active"), QStringLiteral("genry_focus")), walkXalign(ap), az, 1.0, rightish(ap)));
            }
            continue;
        }
        if (cmd == QLatin1String("fullheight")) {
            const QStringList p = pipes(rest);
            QString image, pos = QStringLiteral("center"), mode = QStringLiteral("auto");
            double zoom = 1.28, alpha = 1.0;
            if (!p.isEmpty()) {
                image = p[0];
                if (p.size() > 1 && isWalkPosition(p[1])) pos = p[1];
                if (p.size() > 2) num(p[2], &zoom);
                if (p.size() > 3) mode = p[3].toLower();
                if (p.size() > 4) num(p[4], &alpha);
            } else {
                popEffect();
                if (!w.isEmpty() && num(w.last(), &v)) { alpha = v; w.removeLast(); }
                if (!w.isEmpty() && QStringList{QStringLiteral("mirror"), QStringLiteral("flip"), U("зеркало"), QStringLiteral("normal"),
                                                QStringLiteral("no"), QStringLiteral("auto")}.contains(w.last().toLower()))
                    mode = w.takeLast().toLower();
                if (!w.isEmpty() && num(w.last(), &v)) { zoom = v; w.removeLast(); }
                if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
                image = w.join(QLatin1Char(' '));
            }
            bool mirror = QStringList{QStringLiteral("mirror"), QStringLiteral("flip"), U("зеркало"), QStringLiteral("yes"), QStringLiteral("true"),
                                      QStringLiteral("1")}.contains(mode);
            if (mode == QLatin1String("auto")) mirror = pos == QLatin1String("right") || pos == QLatin1String("fright") || pos == QLatin1String("cright");
            if (!image.isEmpty())
                put(st, aligned(image, alias(image + QStringLiteral("_fullheight_") + pos, QStringLiteral("genry_fullheight")), walkXalign(pos), zoom, alpha, mirror));
            continue;
        }
        if (cmd == QLatin1String("crowdshow")) {
            const QStringList items = pipes(rest).mid(0, 10);
            const int c = int(items.size());
            const double zoom = c <= 3 ? 0.98 : (c <= 5 ? 0.86 : 0.72);
            for (int k = 0; k < c; ++k) put(st, aligned(items[k], {}, double(k + 1) / double(c + 1), zoom));
            continue;
        }
        if ((cmd == QLatin1String("enterleft") || cmd == QLatin1String("enterright")) && w.size() >= 3) {
            double zoom = 1.0;
            if (w.size() >= 4 && num(w.last(), &v) && pyIsNumber(w.at(w.size() - 2))) { zoom = v; w.removeLast(); }
            w.removeLast();
            QString target = w.takeLast();
            if (!isWalkPosition(target)) target = QStringLiteral("center");
            if (!w.isEmpty()) put(st, aligned(w.join(QLatin1Char(' ')), {}, walkXalign(target), zoom));
            continue;
        }
        if (cmd == QLatin1String("zoomshow") && w.size() >= 5) {
            double ya = 0.5, xa = 0.5, zoom = 1.12;
            num(w.takeLast(), &ya);
            num(w.takeLast(), &xa);
            num(w.takeLast(), &zoom);
            w.removeLast();
            SpriteShow s = aligned(w.join(QLatin1Char(' ')), {}, xa, zoom);
            s.ypos = s.yanchor = ya;
            put(st, s);
            continue;
        }
        if ((cmd == QLatin1String("exitleft") || cmd == QLatin1String("exitright")) && w.size() >= 2) {
            if (w.size() >= 3 && pyIsNumber(w.last()) && pyIsNumber(w.at(w.size() - 2))) w.removeLast();
            w.removeLast();
            hideTag(st, tagOf(w.join(QLatin1Char(' '))));      // walked off screen
            continue;
        }
        if (cmd == QLatin1String("pulse") && !w.isEmpty()) {
            if (!w.isEmpty() && pyIsNumber(w.last())) w.removeLast();
            if (!w.isEmpty() && pyIsNumber(w.last())) w.removeLast();
            QString pos = QStringLiteral("center");
            if (!w.isEmpty() && isWalkPosition(w.last())) pos = w.takeLast();
            if (!w.isEmpty()) put(st, aligned(w.join(QLatin1Char(' ')), {}, walkXalign(pos)));
            continue;
        }
        if (cmd == QLatin1String("ghostmove") && w.size() >= 6) {
            double zoom = 1.0, alpha = 0.45;
            num(w.last(), &zoom);
            num(w.at(w.size() - 2), &alpha);
            QString to = w.at(w.size() - 4);
            if (!isWalkPosition(to)) to = QStringLiteral("right");
            put(st, aligned(w.mid(0, w.size() - 5).join(QLatin1Char(' ')), {}, walkXalign(to), zoom, alpha));
            continue;
        }
        if (cmd == QLatin1String("dim")) { st.dim = 0.55; num(rest, &st.dim); continue; }
        if (cmd == QLatin1String("undim")) { st.dim = 0; continue; }
        if (cmd == QLatin1String("notedim")) { st.noteDim = 0.6; num(rest, &st.noteDim); st.noteDim = qBound(0.0, st.noteDim, 1.0); continue; }
        if (cmd == QLatin1String("eyesclose")) { st.eyesClosed = true; st.eyesSeconds = 2.0; num(rest, &st.eyesSeconds); continue; }
        if (cmd == QLatin1String("eyesopen")) { st.eyesClosed = false; st.eyesSeconds = 2.0; num(rest, &st.eyesSeconds); continue; }
        if (cmd == QLatin1String("sleepyeyes")) { st.sleepy = true; continue; }
        if (cmd == QLatin1String("stopsleepyeyes")) { st.sleepy = false; continue; }
        if (cmd == QLatin1String("staticfx") || cmd == QLatin1String("noisefx") || cmd == QLatin1String("glitchfx") || cmd == QLatin1String("vhsfx") ||
            cmd == QLatin1String("memoryfx") || cmd == QLatin1String("dreamfx") || cmd == QLatin1String("dream")) {
            st.dream = true;
            st.dreamAlpha = cmd == QLatin1String("dream") ? 1.0 : 0.7;
            for (const QString& x : w) if (num(x, &v)) st.dreamAlpha = qBound(0.0, v, 1.0);
            if (cmd == QLatin1String("dreamfx")) st.sleepy = true;
            continue;
        }
        if (cmd == QLatin1String("stopdream")) { st.dream = false; continue; }
        if (cmd == QLatin1String("clearfx")) { st.dream = false; st.noteDim = 0; st.sleepy = false; st.eyesClosed = false; continue; }
        if (cmd == QLatin1String("flash")) { st.flash = true; continue; }
        if (cmd == QLatin1String("weather")) {
            popEffect();
            const QString t = w.value(0).toLower();
            st.weather = weatherKey(t);
            st.weatherLevel = 2;
            for (const QString& x : w.mid(1))
                if (const int l = weatherLevelWord(x)) st.weatherLevel = l;
            continue;
        }
        if (cmd == QLatin1String("colorfilter")) {
            popEffect();
            const QString k = filterKey(w.value(0));
            st.filter = (k == QLatin1String("none")) ? QString() : k;
            continue;
        }
        if (cmd == QLatin1String("timeofday")) {
            const QString m = rest.toLower();
            if (m == U("день") || m == QLatin1String("day") || m == U("утро") || m == QLatin1String("morning")) st.timeOfDay = st.spriteTime = QStringLiteral("day");
            else if (m == U("вечер") || m == U("закат") || m == QLatin1String("sunset") || m == QLatin1String("evening"))
                st.timeOfDay = st.spriteTime = QStringLiteral("sunset");
            else if (m == U("ночь") || m == QLatin1String("night")) st.timeOfDay = st.spriteTime = QStringLiteral("night");
            else if (m == U("пролог") || m == QLatin1String("prolog") || m == QLatin1String("prologue")) st.timeOfDay = QStringLiteral("prologue");
            st.timeExplicit = true;
            if (st.bg.startsWith(QLatin1String("bg "))) {          // the place turns evening / night too (as the compiler does)
                const QString other = bgAtTime(st.bg.mid(3), st.timeOfDay);
                if (!other.isEmpty()) st.bg = QStringLiteral("bg ") + other;
            }
            continue;
        }
        if (cmd == QLatin1String("askname")) {
            const QStringList p = rest.split(QLatin1Char('|'));
            const QString def = pyStrip(p.value(1));
            if (!def.isEmpty()) st.playerName = def;
            clearSay(st);
            st.cardKind = QStringLiteral("askname");
            st.cardText = pyStrip(p.value(0)).isEmpty() ? U("Как тебя зовут?") : pyStrip(p.value(0));
            st.cardSub = st.playerName.isEmpty() ? U("Семён") : st.playerName;
            continue;
        }
        if (cmd == QLatin1String("timeskip")) {
            st.bg = QStringLiteral("black");
            st.sprites.clear();
            st.dream = false;
            clearSay(st);
            st.cardKind = QStringLiteral("timeskip");
            st.cardText = rest.isEmpty() ? U("Прошло немного времени…") : (rest.at(0).isDigit() ? U("Прошло ") + rest : rest);
            continue;
        }
        if (cmd == QLatin1String("chapter")) {
            st.bg = QStringLiteral("black");
            st.sprites.clear();
            clearSay(st);
            st.cardKind = QStringLiteral("chapter");
            st.cardText = U("День ") + (w.size() > 1 ? w[1] : QStringLiteral("1"));
            st.cardSub = w.size() > 2 ? pyStrip(pySplit(rest, 2).value(2)) : QString();
            continue;
        }
        if (cmd == QLatin1String("chapterpng")) {
            const QStringList p = pipes(rest);
            st.cardKind = QStringLiteral("chapter");
            st.cardText = U("День ") + p.value(1, QStringLiteral("1"));
            st.cardSub = p.value(2);
            continue;
        }
        if (cmd == QLatin1String("titlecard")) { st.cardKind = QStringLiteral("title"); st.cardText = rest.isEmpty() ? U("Титр") : rest; continue; }
        if (cmd == QLatin1String("splitflap")) {
            st.cardKind = QStringLiteral("splitflap");
            st.cardText = pyStrip(rest.section(QLatin1Char('|'), 0, 0)).toUpper();
            continue;
        }
        if (cmd == QLatin1String("parallax")) {
            const QString a = w.value(0).toLower();
            st.moment = a == U("выкл") || a == U("стоп") || a == QLatin1String("off") || a == QLatin1String("0") ? U("3D-параллакс выкл")
                                                                                                                     : U("3D-параллакс от мыши");
            continue;
        }
        if (cmd == QLatin1String("creditsroll")) {
            st.cardKind = QStringLiteral("credits");
            QString t = pipes(rest).value(0, rest);
            if (t.size() >= 2 && (t.front() == QLatin1Char('"') || t.front() == QLatin1Char('\'')) && t.back() == t.front()) t = t.mid(1, t.size() - 2);
            st.cardText = (t.isEmpty() ? U("КОНЕЦ") : t).replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            continue;
        }
        if (cmd == QLatin1String("note") || cmd == QLatin1String("monologue") || cmd == QLatin1String("diary") || cmd == QLatin1String("memorynote") ||
            cmd == QLatin1String("bigtext")) {
            QString t = rest;
            if (t.size() >= 2 && (t.front() == QLatin1Char('"') || t.front() == QLatin1Char('\'')) && t.back() == t.front()) t = t.mid(1, t.size() - 2);
            t.replace(QStringLiteral("\\n"), QStringLiteral("\n"));       // a line break the writer made
            if (cmd == QLatin1String("diary")) t = U("Дневник\n") + t;
            st.nvlText = t.isEmpty() ? QStringLiteral("...") : t;
            continue;
        }
        if (cmd == QLatin1String("notify") || cmd == QLatin1String("itemget") || cmd == QLatin1String("replayunlock") ||
            cmd == QLatin1String("unlockachievement")) {
            const QStringList p = pipes(rest);
            if (cmd == QLatin1String("notify")) { st.notifyText = p.value(0, rest); st.notifyTitle = p.value(1, QStringLiteral("Genry")); }
            else if (cmd == QLatin1String("itemget")) { st.notifyTitle = U("Инвентарь"); st.notifyText = U("Предмет: ") + p.value(1, p.value(0)); }
            else if (cmd == QLatin1String("replayunlock")) { st.notifyTitle = U("Реплей"); st.notifyText = U("Сцена открыта: ") + p.value(1, p.value(0)); }
            else { st.notifyTitle = U("Достижение"); st.notifyText = U("Достижение: ") + p.value(1, p.value(0)); }
            continue;
        }
        if (cmd == QLatin1String("floatingthought")) {
            st.floating = rest.section(QLatin1Char('|'), 0, 0).trimmed().replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            if (st.floating.isEmpty()) st.floating = U("Мысль.");
            continue;
        }
        if (cmd == QLatin1String("phonestart") && QStringList{U("дом"), U("рабочийстол"), U("рабочий стол"), U("меню"), QStringLiteral("home")}.contains(rest.toLower())) {
            st.phoneHome = true;
            continue;
        }
        if (cmd == QLatin1String("phonestart")) {
            st.phoneOpen = true;
            st.phoneContact = rest.isEmpty() ? U("СМС") : rest;
            st.phone.clear();
            st.phoneTyping.clear();
            continue;
        }
        if (cmd == QLatin1String("sms")) {
            QString who, text = rest;
            if (rest.contains(QLatin1Char(':'))) { who = pyStrip(rest.section(QLatin1Char(':'), 0, 0)); text = pyStrip(rest.section(QLatin1Char(':'), 1)); }
            const QString wl = who.toLower();
            const bool me = wl == U("я") || wl == QLatin1String("me") || wl == QLatin1String("+") || wl == QLatin1String("i");
            const int base = 14 * 60 + 25 + int(st.phone.size()) + 1;
            st.phone.push_back({me ? QStringLiteral("me") : QStringLiteral("them"), me ? U("Я") : (who.isEmpty() ? QStringLiteral("???") : who), text,
                                QStringLiteral("%1:%2").arg((base / 60) % 24, 2, 10, QLatin1Char('0')).arg(base % 60, 2, 10, QLatin1Char('0')),
                                me ? U("√√") : QString()});      // as the V1 game shows it (calibri has no ✓)
            continue;
        }
        if (cmd == QLatin1String("phoneend")) { st.phoneOpen = false; continue; }
        // Телефон 2.0 (the compiler's compileV1Phone grammar)
        if (cmd == QLatin1String("phonephoto") || cmd == QLatin1String("phonevoice") || cmd == QLatin1String("phonepost")) {
            QString who, body = rest;
            if (rest.contains(QLatin1Char(':'))) { who = pyStrip(rest.section(QLatin1Char(':'), 0, 0)); body = pyStrip(rest.section(QLatin1Char(':'), 1)); }
            const QStringList p = pipes(body);
            const QString wl = who.toLower();
            const bool me = wl == U("я") || wl == QLatin1String("me") || wl == QLatin1String("+") || wl == QLatin1String("i");
            const QString name = me ? U("Я") : (who.isEmpty() ? QStringLiteral("???") : who);
            const int base = 14 * 60 + 25 + int(st.phone.size()) + 1;
            const QString t = QStringLiteral("%1:%2").arg((base / 60) % 24, 2, 10, QLatin1Char('0')).arg(base % 60, 2, 10, QLatin1Char('0'));
            if (cmd == QLatin1String("phonepost")) {
                QString text, img, likes = QStringLiteral("0");
                for (int i = 0; i < p.size(); ++i) {
                    const QString k = p[i].section(QLatin1Char('='), 0, 0).trimmed().toLower();
                    if (p[i].contains(QLatin1Char('=')) && (k == U("лайки") || k == QLatin1String("likes"))) likes = p[i].section(QLatin1Char('='), 1).trimmed();
                    else if (i == 0) text = p[i];
                    else if (img.isEmpty()) img = p[i];
                }
                st.feed.prepend({name, text, img, likes});
                st.moment = U("новый пост: ") + name;
                continue;
            }
            if (cmd == QLatin1String("phonevoice")) {
                st.phone.push_back({(me ? QStringLiteral("me") : QStringLiteral("them")) + QStringLiteral("_voice"), name, p.value(0), t,
                                    p.value(1).isEmpty() ? QStringLiteral("0:05") : p.value(1)});
                continue;
            }
            bool adult = false, once = false;
            QString caption;
            for (int i = 1; i < p.size(); ++i) {
                const QString xl = p[i].toLower();
                if (p[i] == QLatin1String("18+") || p[i] == QLatin1String("18") || xl == U("интим") || xl == U("скрыто")) adult = true;
                else if (xl == U("1раз") || xl == U("1 раз") || xl == U("одноразовое") || xl == U("одноразовая") || xl == QLatin1String("once")) once = true;
                else if (caption.isEmpty()) caption = p[i];
            }
            st.phone.push_back({(me ? QStringLiteral("me") : QStringLiteral("them")) +
                                    (once ? QStringLiteral("_photo1") : adult ? QStringLiteral("_photo18") : QStringLiteral("_photo")),
                                name, p.value(0), t, caption});
            continue;
        }
        if (cmd == QLatin1String("phonepush") || cmd == QLatin1String("phonecomment")) {
            QString who, body = rest;
            if (rest.contains(QLatin1Char(':'))) { who = pyStrip(rest.section(QLatin1Char(':'), 0, 0)); body = pyStrip(rest.section(QLatin1Char(':'), 1)); }
            if (cmd == QLatin1String("phonepush")) { st.pushWho = who.isEmpty() ? U("Сообщение") : who; st.pushText = body; }
            else if (!st.feed.isEmpty()) st.feed[0] << QStringLiteral("c:") + who + QLatin1Char('|') + body;
            continue;
        }
        if (cmd == QLatin1String("phonehome") ||
            (cmd == QLatin1String("phonestart") && QStringList{U("дом"), U("рабочийстол"), U("рабочий стол"), U("меню"), QStringLiteral("home")}.contains(rest.toLower()))) {
            st.phoneHome = true;
            continue;
        }
        if (cmd == QLatin1String("phonecall")) {
            const QStringList p = pipes(rest);
            st.phoneCall = p.value(0).isEmpty() ? QStringLiteral("???") : p.value(0);
            for (const QString& x : p)
                if (x.startsWith(U("лицо=")) || x.startsWith(QLatin1String("face="))) st.phoneCallFace = x.section(QLatin1Char('='), 1).trimmed();
            continue;
        }
        if (cmd == QLatin1String("phonefeed")) {
            st.feedOpen = true;
            st.feedTitle = rest.isEmpty() ? U("Лента лагеря") : rest;
            continue;
        }
        if (cmd == QLatin1String("screenmenu")) {
            const QStringList p = pipes(rest);
            st.screenMenu = true;
            st.menuTitle = p.value(0);
            for (const QString& it : p.mid(1)) {
                if (it.contains(QLatin1String("->"))) {
                    const QString cap = it.section(QStringLiteral("->"), 0, 0).trimmed();
                    if (!cap.isEmpty()) st.choices << cap;
                } else if (normalizeCommand(firstWord(it)) == QLatin1String("bg")) {
                    const QString b = pyStrip(it.mid(firstWord(it).size()));
                    st.bg = (b.startsWith(QLatin1String("bg ")) || b.startsWith(QLatin1String("cg "))) ? b : QStringLiteral("bg ") + b;
                }
            }
            if (st.choices.isEmpty()) st.choices << U("Продолжить");
            continue;
        }
        if (cmd == QLatin1String("map")) {
            // «карта [обход] площадь: сцена @sl, beach: сцена2» - the compiler's grammar (parseMapSpec)
            st.map = true;
            st.mapZones.clear();
            for (const MapEntry& p : parseMapSpec(rest).places)
                if (!p.zone.isEmpty()) st.mapZones << p.zone + QLatin1Char('|') + p.target + QLatin1Char('|') + p.chibi;
            continue;
        }
        if (cmd == QLatin1String("windowhide")) { st.windowHidden = true; continue; }
        if (cmd == QLatin1String("windowshow")) { st.windowHidden = false; continue; }
        if (cmd == QLatin1String("music")) {
            QStringList mw;
            for (int k = 0; k < w.size(); ++k) {
                const QString x = w[k].toLower();
                if (x == QLatin1String("fadein") || x == QLatin1String("fadeout")) { ++k; continue; }
                if (x == QLatin1String("loop") || x == QLatin1String("noloop")) continue;
                mw << w[k];
            }
            st.music = mw.join(QLatin1Char(' '));
            continue;
        }
        if (cmd == QLatin1String("musicfile")) { st.music = pyBasename(w.value(0)); continue; }
        if (cmd == QLatin1String("stopmusic")) { st.music.clear(); continue; }
        if (cmd == QLatin1String("stopallaudio") || cmd == QLatin1String("endgame")) { st.music.clear(); st.ambience.clear(); continue; }
        if (cmd == QLatin1String("videobg")) {
            popEffect();
            st.bg = QStringLiteral("black");
            st.sprites.clear();
            st.videoBg = pyBasename(w.join(QLatin1Char(' ')));
            continue;
        }
        if (cmd == QLatin1String("stopvideobg")) { st.videoBg.clear(); continue; }
        if (cmd == QLatin1String("video")) { st.videoCard = pyBasename(rest); continue; }
        // NVL: nvlначать / заметканvl start a page, nvlконец / заметкаadv / закрытьзаметку close it
        if (cmd == QLatin1String("nvlstart") || cmd == QLatin1String("notenvl")) {
            st.nvlMode = true;
            st.nvlPage.clear();
            clearSay(st);
            if (cmd == QLatin1String("notenvl")) st.noteDim = 0.6;
            continue;
        }
        if (cmd == QLatin1String("nvlend") || cmd == QLatin1String("noteadv") || cmd == QLatin1String("closenote")) {
            st.nvlMode = false;
            st.nvlPage.clear();
            if (cmd != QLatin1String("nvlend")) st.noteDim = 0;
            continue;
        }
        if (cmd == QLatin1String("notebg") && !w.isEmpty()) {
            popEffect();
            const QString kind = w.takeFirst().toLower();
            const QString image = w.join(QLatin1Char(' '));
            if (kind == QLatin1String("cg")) st.bg = QStringLiteral("cg ") + image;
            else if (kind == QLatin1String("black") || kind == QLatin1String("white")) st.bg = kind;
            else if (kind == QLatin1String("bg")) st.bg = (image.toLower() == QLatin1String("black") || image.toLower() == QLatin1String("white")) ? image.toLower() : QStringLiteral("bg ") + image;
            else st.bg = (kind + QLatin1Char(' ') + image).trimmed();
            st.videoBg.clear();
            continue;
        }
        if (cmd == QLatin1String("musicplayer")) {
            const QStringList items = rest.split(QLatin1Char('|'));
            for (int k = 0; k + 1 < items.size(); k += 2)
                if (!pyStrip(items[k]).isEmpty()) st.musicPlayer << pyStrip(items[k]);
            continue;
        }
        if (cmd == QLatin1String("achievement")) { st.achievement = w.value(0); continue; }
        // moments: they happen on this line only, the preview names them
        if (cmd == QLatin1String("shake")) { st.moment = U("тряска"); continue; }
        if (cmd == QLatin1String("pixelfx")) { st.moment = U("пиксели"); continue; }
        if (cmd == QLatin1String("eyesblink")) { st.moment = U("моргание"); continue; }
        if (cmd == QLatin1String("effect")) { st.moment = U("переход ") + (rest.isEmpty() ? QStringLiteral("dissolve") : rest); continue; }
        if (cmd == QLatin1String("ambience")) { st.ambience = pyBasename(w.value(0)); continue; }
        if (cmd == QLatin1String("stopambience")) { st.ambience.clear(); continue; }
        if (cmd == QLatin1String("sound") || cmd == QLatin1String("soundfile") || cmd == QLatin1String("voice") || cmd == QLatin1String("voicefile")) {
            st.sound = pyBasename(pyStrip(rest));
            continue;
        }
    }
    if (menu.on) {
        st.choices = menu.choices;
        st.choiceStyle = menu.style;
        st.choiceSeconds = menu.seconds;
        st.choiceImages = menu.images;
        st.choiceKinds = menu.kinds;
        st.choiceHints = menu.hints;
        st.choiceHover = menu.hover;
        // images: the question is a line before the menu in the game - the menu covers it
        if (menu.style == QLatin1String("images") || menu.style == QLatin1String("timed") || menu.style == QLatin1String("phone")) menu.asked = false;
        st.choiceAsked = menu.asked && !st.text.isEmpty();
    }
    return st;
}

} // namespace gb
