#include "Lint.h"
#include "Tr.h"
#include "EsAssets.h"
#include "Graph.h"
#include "Overlays.h"
#include "Py.h"
#include "Screenplay.h"
#include "Text.h"
#include "Wardrobe.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace gb {

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

struct Ref {
    int line;
    QString name;
    int idx = -1;             // the line in the lint's own list (for the fix)
};

// a slip of the pen, not another word: close enough to swap without asking twice
bool nearMiss(const QString& a, const QString& b)
{
    if (a.isEmpty() || b.isEmpty() || a == b) return false;
    const int d = [&] {
        QVector<int> prev(b.size() + 1), cur(b.size() + 1);
        for (int j = 0; j <= b.size(); ++j) prev[j] = j;
        for (int i = 1; i <= a.size(); ++i) {
            cur[0] = i;
            for (int j = 1; j <= b.size(); ++j)
                cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1].toLower() == b[j - 1].toLower() ? 0 : 1)});
            std::swap(prev, cur);
        }
        return prev[b.size()];
    }();
    return d <= (a.size() >= 7 ? 2 : 1) || (a.size() >= 5 && d <= a.size() / 3);
}

// Levenshtein for "did you mean"
int dist(const QString& a, const QString& b)
{
    QVector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

QString closest(const QString& want, const QStringList& options, int limit = 3)
{
    QVector<QPair<int, QString>> scored;
    for (const QString& o : options) scored.push_back({dist(want, o), o});
    std::sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    QStringList out;
    for (int i = 0; i < scored.size() && i < limit; ++i) out << scored[i].second;
    return out.join(QStringLiteral(", "));
}

} // namespace

QVector<LintIssue> lintStory(const QString& text, const LintContext& ctx)
{
    QVector<LintIssue> out;
    auto add = [&out](int line, int level, const QString& msg) { out.push_back({line, level, msg}); };
    // V1 «пиши как сценарий»: check what the play lines become, report on the line as written
    const QStringList rawLines = pySplitLines(stripBom(text));
    QVector<int> srcOf;
    QVector<ScreenplayNote> scrNotes;
    const QStringList lines = ctx.opt.legacy ? rawLines : expandScreenplay(rawLines, &srcOf, &scrNotes);
    if (ctx.opt.legacy) for (int i = 0; i < lines.size(); ++i) srcOf << i;
    for (const ScreenplayNote& n : scrNotes) add(n.line + 1, n.warn ? LintIssue::Warning : LintIssue::Info, n.text);
    QStringList body;
    const ModMeta meta = parseMeta(lines, &body, ctx.opt);
    const auto sl = [&](const QString& s) { return slugOf(s, QStringLiteral("label"), ctx.opt); };

    QHash<QString, int> labels;
    labels.insert(meta.modId, 0);
    QVector<Ref> targets;
    int choiceOpen = 0, phoneOpen = 0, menuOpen = 0;
    int menuStarts = 0;                     // «менюмода»: buttons that start the mod
    bool timedChoice = false;
    QSet<QString> meterVars;              // «шкала» declarations (anywhere in the story)
    QSet<QString> setVars;                // everything the story gives a value to (a lock on anything else never opens)
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        const QString w0 = firstWord(s);
        const QString c0 = normalizeCommand(w0);
        const QString r0 = pyStrip(s.mid(w0.size()));
        if (c0 == QLatin1String("meter"))
            meterVars.insert(slugOf(pyStrip(r0.section(QLatin1Char('|'), 0, 0)), QStringLiteral("value"), ctx.opt));
        if (c0 == QLatin1String("meter") || c0 == QLatin1String("setvar") || c0 == QLatin1String("addvar") || c0 == QLatin1String("variable"))
            setVars.insert(slugOf(pySplit(r0.section(QLatin1Char('|'), 0, 0)).value(0), QStringLiteral("value"), ctx.opt));
        if (c0 == QLatin1String("remember")) {
            const QString flag = parseV1Opts(r0).get(QStringLiteral("flag"));
            if (!flag.isEmpty()) setVars.insert(slugOf(flag, QStringLiteral("value"), ctx.opt));
        }
    }
    QSet<QString> achKeys;                // Достижения 2.0: every key a «ачивка» line names
    int achPlain = 0;
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        const QString w0 = firstWord(s);
        if (normalizeCommand(w0) != QLatin1String("unlockachievement")) continue;
        const AchSpec a = parseAchievement(pyStrip(s.mid(w0.size())));
        if (a.key.isEmpty()) continue;
        achKeys.insert(a.key.toLower());
        if (!a.plat) ++achPlain;
    }
    // the doctor: a fix offered with the issue - only where the line is written the way the lint read it
    int curI = -1;
    const auto rawAt = [&](int i) { return rawLines.value(srcOf.value(i)); };
    const auto swapIn = [&](int i, const QString& from, const QString& to) -> QString {
        if (i < 0 || from.isEmpty() || lines.value(i) != rawAt(i)) return {};
        const QString raw = rawAt(i);
        const int at = int(raw.indexOf(from));
        if (at < 0) return {};
        return raw.left(at) + to + raw.mid(at + from.size());
    };
    const auto withFix = [&](LintIssue x, int mode, const QString& fix, const QString& label) {
        if (mode && !fix.isNull()) { x.fixMode = mode; x.fix = fix; x.fixLabel = label; }
        out.push_back(x);
    };
    // the swap of a misspelt name for the closest right one, if it is a slip
    const auto swapFix = [&](int ln, int level, const QString& msg, const QString& wrong, const QStringList& right) {
        LintIssue x{ln, level, msg};
        const QString best = closest(wrong, right, 1);
        if (nearMiss(wrong, best)) {
            const QString fixed = swapIn(curI, wrong, best);
            if (!fixed.isEmpty()) { x.fixMode = 1; x.fix = fixed; x.fixLabel = best; }
        }
        out.push_back(x);
    };
    QStringList sceneTitles;               // the scenes as written after «:» (the fix of «нет сцены»)
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        if (s.startsWith(QLatin1Char(':'))) sceneTitles << pyStrip(s.mid(1));
    }
    QVector<int> choiceItems;             // options of each open «выбор» (a choice under an option nests)
    QVector<bool> choiceTimed;
    QSet<QString> newSpeakers;
    const EsAssets* es = ctx.es;
    // speakers declared anywhere with «персонаж id Имя» are not "new"
    CompileState declared;
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        const QString w = firstWord(s);
        if (normalizeCommand(w) != QLatin1String("character")) continue;
        QStringList v = pySplit(pyStrip(s.mid(w.size())));
        if (v.isEmpty()) continue;
        declared.speakers.insert(v[0].toLower(), v[0]);
        if (v.size() > 1 && pyIsHexColor(v.last())) v.removeLast();
        if (v.size() > 1) declared.speakers.insert(v.mid(1).join(QLatin1Char(' ')).toLower(), v[0]);
    }

    auto imageKnown = [&](const QString& name) {
        const QString n = name.simplified();
        if (n.isEmpty()) return true;
        const QString low = n.toLower();
        if (ctx.customImages.contains(low)) return true;
        if (!es) return true;
        return es->sprites.contains(n) || es->images.contains(n) || low == QLatin1String("black") || low == QLatin1String("white");
    };
    // CGs of the 18+ patch: the mod carries them itself (images/genry_patch), players need no patch
    bool patchNoted = false;
    auto patchCg = [&](int ln, const QString& full) {
        const EsPatchImage* p = esPatchImage(full);
        if (!p) return false;                        // the Steam-declared CGs, the uncensored ones, the old cards
        if ((es && es->missingImages.contains(full)) || !esPatchInFolder(*p))
            add(ln, LintIssue::Warning, U("«%1» — CG из 18+ патча, которого в GenryBL нет: в игре будет тёмная заглушка "
                                          "у всех, кто не подписан на патч").arg(full.mid(3)));
        else if (!patchNoted)
            add(ln, LintIssue::Info, gbTr("CG из 18+ патча ляжет прямо в мод (images/genry_patch) — игроки увидят его и без патча"));
        patchNoted = true;
        return true;
    };
    const Wardrobe* wr = wardrobe();
    bool wardrobeNoted = false;
    auto checkSprite = [&](int ln, const QString& image) {
        QString n = image.simplified();
        if (n.isEmpty() || !es) return;
        if (!ctx.opt.legacy) splitOverlays(withOverlays(n), &n, nullptr);     // «румянец пот» ride on the sprite
        if (patchCg(ln, n) || imageKnown(n)) return;
        if (es->missingImages.contains(n)) {
            add(ln, LintIssue::Warning, gbTr("«%1» объявлена в БЛ, но файла в игре нет — у игрока будет ошибка").arg(n));
            return;
        }
        const QString tag = n.section(QLatin1Char(' '), 0, 0);
        if (tag.startsWith(QLatin1String("genry_")) || tag == QLatin1String("prologue_dream") || tag == QLatin1String("blink")) return;
        // «гардероб мастерской»: workshop outfits / faces on ES's own bodies
        QString why;
        WardrobeLook look;
        if (wr && !ctx.opt.legacy && wr->resolve(n, &look, &why)) {
            if (look.workshop && !wardrobeNoted)
                add(ln, LintIssue::Info, U("Гардероб мастерской: слои возьмутся из Мастерской Steam и лягут в мод (images/genry_wardrobe) — "
                                           "игрокам ничего подписывать не надо"));
            wardrobeNoted = wardrobeNoted || look.workshop;
            return;
        }
        // every word exists but they do not go together (pose, distance, no face): say why;
        // a word nobody has (a typo) gets the "similar names" hint below
        if (wr && !ctx.opt.legacy && !why.isEmpty() && !why.startsWith(U("в гардеробе нет")) && (es->spriteTags().contains(tag) || wr->hasTag(tag))) {
            add(ln, LintIssue::Warning, U("%1 «%2»: %3").arg(es->characterName(tag).isEmpty() ? tag : es->characterName(tag), n.mid(tag.size() + 1), why));
            return;
        }
        if (es->spriteTags().contains(tag)) {
            QString rest = n.mid(tag.size() + 1);
            QString dist;
            if (rest.endsWith(QLatin1String(" close")) || rest.endsWith(QLatin1String(" far"))) {
                dist = rest.section(QLatin1Char(' '), -1);
                rest = rest.section(QLatin1Char(' '), 0, -2);
            }
            const QStringList names = es->spriteNames(tag);
            LintIssue x{ln, LintIssue::Warning, gbTr("У %1 нет «%2» — похожие: %3").arg(es->characterName(tag).isEmpty() ? tag : es->characterName(tag), rest,
                                                                                  closest(rest, names))};
            const QString best = closest(rest, names, 1);
            if (nearMiss(rest, best)) {
                const QString fixed = swapIn(curI, tag + QLatin1Char(' ') + rest, tag + QLatin1Char(' ') + best);
                withFix(x, fixed.isEmpty() ? 0 : 1, fixed, best);
            } else {
                out.push_back(x);
            }
            return;
        }
        // «алиса smile pioneer», «dvv smile»: the heroine's name or a slip of her tag
        QString goodTag;
        for (const QString& t : es->spriteTags())
            if (!es->characterName(t).isEmpty() && es->characterName(t).compare(tag, Qt::CaseInsensitive) == 0) { goodTag = t; break; }
        if (goodTag.isEmpty() && tag.size() >= 2) {
            const QString c = closest(tag, es->spriteTags(), 1);
            if (dist(tag, c) <= 1) goodTag = c;
        }
        LintIssue x{ln, LintIssue::Warning, gbTr("Нет картинки «%1» — это не персонаж БЛ и не свой PNG проекта").arg(n)};
        const QString fixed = goodTag.isEmpty() ? QString() : swapIn(curI, n, goodTag + n.mid(tag.size()));
        withFix(x, fixed.isEmpty() ? 0 : 1, fixed, goodTag + n.mid(tag.size()));
    };

    for (int i = 0; i < lines.size(); ++i) {
        const int ln = srcOf[i] + 1;
        curI = i;
        const QString s = pyStrip(lines[i]);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@'))) continue;
        const QString first = firstWord(s);
        const QString cmd = normalizeCommand(first);
        const QString rest = pyStrip(s.mid(first.size()));
        QStringList w = pySplit(rest);

        // V1 «менюмода … конецменюмода»: its own words, the rest are ordinary frame commands
        if (menuOpen) {
            if (cmd == QLatin1String("endmodmenu") || cmd == QLatin1String("endchoice")) { menuOpen = 0; continue; }
            const QString lw = first.toLower();
            {
                // «менюмода свой»: its parts; a word it does not know says what it does know
                MenuParts mp;
                if (menuPartLine(lw, rest, &mp)) {
                    static const QHash<QString, QString> known{
                        {U("кнопки"), U("слева, справа, по центру, снизу")}, {U("раскладка"), U("слева, справа, по центру, снизу")},
                        {U("вид"), U("текст, таблички, бл, неон")}, {U("цвет"), U("#ffd27d — цвет в виде #rrggbb")},
                        {U("частицы"), U("пыль, листья, светлячки, дождь, снег, сердца, по часам, нет")},
                        {U("появление"), U("выезд, проявление, снизу, печать")}, {U("вход"), U("выезд, проявление, снизу, печать")}};
                    const bool got = !mp.layout.isEmpty() || !mp.look.isEmpty() || !mp.accent.isEmpty() || !mp.fx.isEmpty() || !mp.enter.isEmpty();
                    if (!got) swapFix(ln, LintIssue::Warning, gbTr("«%1 %2» не знаю — можно: %3").arg(first, rest, known.value(lw)), rest,
                                      known.value(lw).split(QStringLiteral(", ")));
                    continue;
                }
            }
            if (lw == U("заголовок") || lw == QLatin1String("title") || lw == U("лого") || lw == U("логотип") || lw == QLatin1String("logo") ||
                lw == U("стиль") || lw == QLatin1String("style") || lw == U("автор") || lw == QLatin1String("author"))
                continue;
            if (lw == U("герои") || lw == U("героини") || lw == U("вайфу") || lw == QLatin1String("heroes")) {
                for (const QString& h : rest.split(QLatin1Char('|'))) {
                    QString kind;
                    const QString img = choiceImage(pyStrip(h), &kind, ctx.customImages);
                    if (kind == QLatin1String("sprite")) checkSprite(ln, img);
                }
                continue;
            }
            if (lw == U("кнопка") || lw == QLatin1String("button")) {
                // the kinds the menu knows by word (menuButtonKind: «Дни» = главы, «Выселиться» = выход…)
                static const QSet<QString> special{U("галерея"), U("достижения"), U("шкалы"), U("главы"), U("настройки"), U("загрузить"), U("выход"), U("имя"), U("статистика"), U("стример")};
                if (rest.contains(QLatin1String("->"))) {
                    const QString t = pyStrip(rest.section(QStringLiteral("->"), 1));
                    if (!special.contains(menuButtonKind(t))) targets.push_back({ln, t, curI});
                } else if (rest.isEmpty()) {
                    add(ln, LintIssue::Warning, gbTr("Кнопка без текста: «кнопка Начать -> start» или «кнопка Галерея»"));
                } else if (!special.contains(menuButtonKind(rest)) && ++menuStarts > 1) {
                    // a second button with no target and no known word: it starts the mod too - most likely not meant
                    add(ln, LintIssue::Info, U("Кнопка «%1» тоже начинает мод с начала. Другое нужно — «кнопка %1 -> главы» "
                                               "(или галерея, настройки, загрузить, имя, выход, имя сцены)").arg(rest));
                }
                continue;
            }
        }
        if (cmd == QLatin1String("modmenu")) { menuOpen = ln; menuStarts = 0; continue; }
        if (cmd == QLatin1String("meter")) {
            const QString v = pyStrip(rest.section(QLatin1Char('|'), 0, 0));
            if (v.isEmpty()) add(ln, LintIssue::Error, gbTr("Шкала: «шкала симпатия_алисы | Алиса | #ff7a00 | 0 | 10»"));
            continue;
        }
        if (cmd == QLatin1String("meters") && meterVars.isEmpty())
            add(ln, LintIssue::Warning, gbTr("Нет ни одной «шкала …» — экран шкал будет пустым"));
        if (cmd == QLatin1String("bestmeter")) {
            for (const QString& part : rest.split(QLatin1Char('|'))) {
                if (!part.contains(QLatin1String("->"))) continue;
                const QString left = pyStrip(part.section(QStringLiteral("->"), 0, 0));
                targets.push_back({ln, pyStrip(part.section(QStringLiteral("->"), 1)), curI});
                const QString l = left.toLower();
                if (l != U("иначе") && l != QLatin1String("else") && l != U("ничья") && !meterVars.contains(slugOf(left, QStringLiteral("value"), ctx.opt)))
                    add(ln, LintIssue::Warning, gbTr("«%1» — не шкала: объяви её «шкала %1 | Имя | #цвет | 0 | 10»").arg(left));
            }
            continue;
        }

        // V1 «Выбор 2.0»: options, the lines under them (ordinary lines, checked below as any), choices inside choices
        if (!ctx.opt.legacy && !choiceItems.isEmpty()) {
            if (cmd == QLatin1String("endchoice")) {
                if (choiceItems.last() == 0)
                    add(ln, LintIssue::Warning, gbTr("В «выбор» нет ни одного варианта «- Текст» — меню не покажется"));
                choiceItems.removeLast();
                choiceTimed.removeLast();
                continue;
            }
            if (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) {
                withFix({ln, LintIssue::Info, gbTr("«выбор» без «конецвыбора» — закрою его сам перед этой сценой")}, 5,
                        QString(choiceItems.size() - 1, QLatin1Char(' ')).replace(QLatin1Char(' '), QStringLiteral("    ")) + QString::fromUtf8("конецвыбора"),
                        QString::fromUtf8("конецвыбора"));
                choiceItems.clear();
                choiceTimed.clear();
            } else if (isChoiceItemLine(s)) {
                ++choiceItems.last();
                const ChoiceItemSpec it = parseChoiceItem(s);
                // the option's brackets as written: a lock without a number, «запомнит» without who, a word that
                // almost is a command (it would stay text of the option) - under the very words, not the whole line
                const QString rawLine = rawLines.value(srcOf.value(i));
                static const QRegularExpression br(QStringLiteral("\\[([^\\[\\]]*)\\]"));
                static const QRegularExpression numTail(QStringLiteral("^(.+?)\\s+(-?\\d+(?:[.,]\\d+)?)\\+?$"));
                static const QRegularExpression opRe(QStringLiteral("(>=|<=|==|!=|=|>|<)"));
                bool badNeed = false;             // a lock already said wrong: no second word about its points
                auto at = [&](int col, int len, int level, const QString& msg) {
                    LintIssue x{ln, level, msg, QString(), col, len};
                    out.push_back(x);
                };
                for (auto m = br.globalMatch(rawLine); m.hasNext();) {
                    const auto b = m.next();
                    const QString inner = b.captured(1);
                    const QString t = pyStrip(inner);
                    const int innerAt = int(b.capturedStart(1)) + int(inner.indexOf(t));
                    const QString w = firstWord(t).toLower();
                    const QString tail = pyStrip(t.mid(firstWord(t).size()));
                    const int tailAt = innerAt + int(t.indexOf(tail, int(firstWord(t).size())));
                    if (w == U("нужно") || w == U("нужна") || w == U("нужен") || w == U("надо")) {
                        const QString cond = pyStrip(tail.section(QLatin1Char('|'), 0, 0));
                        const QString c0 = firstWord(cond).toLower();
                        if (cond.isEmpty())
                            at(int(b.capturedStart()), int(b.capturedLength()), LintIssue::Error, gbTr("Чего нужно? [нужно Славя 3]"));
                        else if (c0 != U("предмет") && !opRe.match(cond).hasMatch() && !numTail.match(cond).hasMatch() &&
                                 !cond.contains(U(" и ")) && !cond.contains(U(" или ")) && cond.contains(QLatin1Char(' '))) {
                            const QString last = cond.section(QLatin1Char(' '), -1);
                            badNeed = true;
                            at(tailAt + int(cond.lastIndexOf(last)), int(last.size()), LintIssue::Error,
                               U("Здесь нужно число: [нужно %1 3]").arg(cond.section(QLatin1Char(' '), 0, -2)));
                        }
                    } else if ((w == U("запомнит") || w == U("флаг") || w == U("если")) && tail.isEmpty()) {
                        at(int(b.capturedStart()), int(b.capturedLength()), LintIssue::Error,
                           w == U("запомнит") ? U("Кто запомнит? [запомнит Славя]") : w == U("флаг") ? U("Какой флаг? [флаг помог_славе]") : U("Если что? [если ключ]"));
                    } else if (!w.isEmpty() && w.size() >= 4 && t.count(QLatin1Char('/')) == 0) {
                        // «[нужн Славя 3]», «[запомнил Алиса]»: almost a command - it would be shown as text of the option
                        static const QStringList words{U("нужно"), U("если"), U("запомнит"), U("флаг"), U("выход"), U("всегда")};
                        for (const QString& kw : words)
                            if (w != kw && dist(w, kw) <= 2 && !(w == U("запомнят") && kw == U("запомнит"))) {
                                at(innerAt, int(firstWord(t).size()), LintIssue::Warning,
                                   U("«%1» — не команда, останется текстом варианта. Может, «%2»?").arg(firstWord(t), kw));
                                break;
                            }
                    }
                }
                if (it.caption.isEmpty()) add(ln, LintIssue::Warning, gbTr("Пустой текст варианта"));
                if (!it.target.isEmpty()) targets.push_back({ln, it.target, curI});
                else if (s.contains(QLatin1String("->"))) {
                    QString fixed = swapIn(i, QStringLiteral("->"), QString());
                    while (fixed.endsWith(QLatin1Char(' '))) fixed.chop(1);
                    withFix({ln, LintIssue::Warning, gbTr("После «->» нужна сцена — или убери стрелку, и история пойдёт дальше")},
                            fixed.isEmpty() ? 0 : 1, fixed, gbTr("убрать «->»"));
                }
                if (!it.image.isEmpty()) {
                    QString kind;
                    const QString img = choiceImage(it.image, &kind, ctx.customImages);
                    if (kind == QLatin1String("sprite")) checkSprite(ln, img);
                    else if (!patchCg(ln, img) && !imageKnown(img)) {
                        const bool cg = img.startsWith(QLatin1String("cg "));
                        add(ln, LintIssue::Warning, gbTr("Нет картинки «%1» для варианта — похожие: %2")
                                                         .arg(it.image, closest(img.mid(3), cg ? (es ? es->cgs() : QStringList()) : (es ? es->backgrounds() : QStringList()))));
                    }
                }
                // a lock on points nothing ever gives never opens; a condition on them never shows the option
                for (const QString& v : badNeed ? QStringList() : choiceConditionVars(it.need))
                    if (!setVars.contains(slugOf(v, QStringLiteral("value"), ctx.opt)))
                        add(ln, LintIssue::Warning, gbTr("«%1» нигде не прибавляется — этот вариант всегда будет закрыт. Дай очки: «[+1 %1]» у другого варианта или «прибавить %1 1»").arg(v));
                for (const QString& v : choiceConditionVars(it.cond))
                    if (!setVars.contains(slugOf(v, QStringLiteral("value"), ctx.opt)))
                        add(ln, LintIssue::Warning, gbTr("«%1» нигде не задаётся — этот вариант никогда не появится").arg(v));
                continue;
            } else if (s.contains(QLatin1String("->")) && isTimeoutWord(s.section(QStringLiteral("->"), 0, 0))) {
                if (!choiceTimed.last()) add(ln, LintIssue::Warning, gbTr("«время вышло» работает только в «выбор на время N»"));
                targets.push_back({ln, pyStrip(s.section(QStringLiteral("->"), 1)), curI});
                continue;
            }
            // any other line: the question before the options or a line under one - checked like everywhere
        }
        if (choiceOpen) {
            if (cmd == QLatin1String("endchoice")) { choiceOpen = 0; continue; }
            if (s.startsWith(QLatin1Char('-')) && s.contains(QLatin1String("->"))) {
                QString t = pyStrip(s.mid(1).section(QStringLiteral("->"), 1));
                if (!ctx.opt.legacy && t.contains(QLatin1Char('|'))) {         // «-> сцена | картинка»
                    const QString raw = pyStrip(t.section(QLatin1Char('|'), 1));
                    t = pyStrip(t.section(QLatin1Char('|'), 0, 0));
                    QString kind;
                    const QString img = choiceImage(raw, &kind, ctx.customImages);
                    if (raw.isEmpty()) add(ln, LintIssue::Warning, gbTr("После «|» нужна картинка: sl smile pioneer, bg ext_beach_day или cg …"));
                    else if (kind == QLatin1String("sprite")) checkSprite(ln, img);
                    else if (!patchCg(ln, img) && !imageKnown(img)) {
                        const bool cg = img.startsWith(QLatin1String("cg "));
                        add(ln, LintIssue::Warning, gbTr("Нет картинки «%1» для варианта — похожие: %2")
                                                         .arg(raw, closest(img.mid(3), cg ? (es ? es->cgs() : QStringList()) : (es ? es->backgrounds() : QStringList()))));
                    }
                }
                if (pyStrip(s.mid(1).section(QStringLiteral("->"), 0, 0)).isEmpty()) add(ln, LintIssue::Warning, gbTr("Пустой текст варианта"));
                targets.push_back({ln, t, curI});
                continue;
            }
            if (!ctx.opt.legacy && s.contains(QLatin1String("->")) && isTimeoutWord(s.section(QStringLiteral("->"), 0, 0))) {
                if (!timedChoice) add(ln, LintIssue::Warning, gbTr("«время вышло» работает только в «выбор на время N»"));
                targets.push_back({ln, pyStrip(s.section(QStringLiteral("->"), 1)), curI});
                continue;
            }
            add(ln, LintIssue::Error, gbTr("Внутри «выбор» только строки «- Текст -> сцена» — иначе сборка упадёт"));
            continue;
        }
        if (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) {
            QString l = sl(s.startsWith(QLatin1Char(':')) ? pyStrip(s.mid(1)) : rest);
            if (l == QLatin1String("start")) l = meta.modId;
            if (labels.value(l, -1) > 0) {
                const QString name = s.startsWith(QLatin1Char(':')) ? pyStrip(s.mid(1)) : rest;
                QString fresh = name + QStringLiteral("_2");
                for (int n = 3; sceneTitles.contains(fresh); ++n) fresh = name + QStringLiteral("_%1").arg(n);
                const QString fixed = swapIn(i, name, fresh);
                withFix({ln, LintIssue::Error, gbTr("Сцена «%1» уже есть на строке %2 — Ren'Py не запустится").arg(l).arg(labels[l])},
                        fixed.isEmpty() ? 0 : 1, fixed, fresh);
                continue;
            }
            labels.insert(l, ln);
            continue;
        }
        if (s.contains(QLatin1Char(':')) && !isCommandName(cmd) && !s.toLower().startsWith(QLatin1String("http"))) {
            const QString who = pyStrip(s.section(QLatin1Char(':'), 0, 0));
            const QString id = speakerId(who, declared, ctx.opt);
            if (id.startsWith(QLatin1String("genry_sp_")) && !newSpeakers.contains(id)) {
                newSpeakers.insert(id);
                // «Слава», «Алса»: a slip of a heroine the game has - she, not a stranger without a colour
                QStringList known;
                if (es) for (auto it = es->characters.begin(); it != es->characters.end(); ++it) if (!it->name.isEmpty()) known << it->name;
                for (auto it = declared.speakers.begin(); it != declared.speakers.end(); ++it) known << it.key();
                const QString best = closest(who, known, 1);
                if (who.size() >= 3 && nearMiss(who, best) && best.compare(who, Qt::CaseInsensitive) != 0) {
                    const QString fixed = swapIn(i, who + QLatin1Char(':'), best + QLatin1Char(':'));
                    withFix({ln, LintIssue::Warning, gbTr("«%1» — может, «%2»? Сейчас это новый говорящий, отдельный от «%2»").arg(who, best)},
                            fixed.isEmpty() ? 0 : 1, fixed, best);
                } else {
                    add(ln, LintIssue::Info, gbTr("Новый говорящий «%1» — объявлю его сам (или «персонаж id Имя #цвет»)").arg(who));
                }
            }
            continue;
        }
        if (!isCommandName(cmd)) {
            swapFix(ln, LintIssue::Warning, gbTr("«%1» — не команда: в мод уйдёт комментарием").arg(first), first, renames().keys());
            continue;
        }
        // «убрать» with nobody named
        if (cmd == QLatin1String("hide") && !ctx.opt.legacy) {
            QStringList hw = w;
            if (!hw.isEmpty() && isEffect(hw.last())) hw.removeLast();
            if (hw.isEmpty()) {
                const QString raw = rawAt(i);
                withFix({ln, LintIssue::Error, gbTr("Кого убрать? «убрать dv» — или «убратьвсех», чтобы убрать всех")},
                        lines.value(i) == raw ? 1 : 0, raw.left(raw.size() - pyLStrip(raw).size()) + U("убратьвсех"), U("убратьвсех"));
                continue;
            }
        }
        // «звук sunny_day»: music written as a sound (and the other way round) - it plays as what it is anyway,
        // the fix only makes the line say so
        if (!ctx.opt.legacy && lines.value(i) == rawAt(i)) {
            QString kind;
            const QString right = rightAudioKind(rawAt(i), ctx.opt, &kind);
            if (right != rawAt(i)) {
                const QString what = kind == QLatin1String("music") ? gbTr("музыка") : kind == QLatin1String("ambience") ? gbTr("атмосфера") : gbTr("звук");
                withFix({ln, LintIssue::Info, gbTr("«%1» — это %2: так и сыграю. Поправить строку, чтобы было видно?").arg(firstWord(rest), what)},
                        1, right, firstWord(pyStrip(right)));
                continue;
            }
        }
        auto popEffect = [&] { if (!w.isEmpty() && isEffect(w.last())) w.removeLast(); };
        // V2.1 - the 7ДЛ mechanics
        if (!ctx.opt.legacy && cmd == QLatin1String("stranger")) {
            if (pyStrip(rest.section(QLatin1Char('|'), 1)).isEmpty())
                add(ln, LintIssue::Error, gbTr("Как её зовут, пока не познакомились? «прозвище Славя | Блондинка»"));
            continue;
        }
        if (!ctx.opt.legacy && cmd == QLatin1String("terminal")) {
            bool any = false;
            for (const QString& x : rest.split(QLatin1Char('|')).mid(1)) {
                if (x.contains(QLatin1Char(':')) || x.contains(QLatin1String("->"))) any = true;
                if (!x.contains(QLatin1String("->"))) continue;
                const QString t = pyStrip(x.section(QStringLiteral("->"), 1)), tl = t.toLower();
                if (!t.isEmpty() && tl != U("выход") && tl != QLatin1String("exit") && tl != U("дальше")) targets.push_back({ln, t, curI});
            }
            if (!any) add(ln, LintIssue::Error, gbTr("Терминалу нужны команды: «терминал Компьютер | Введи help | help: ответ | open -> сцена»"));
            continue;
        }
        if (!ctx.opt.legacy && cmd == QLatin1String("rps")) {
            for (const QString& x : rest.split(QLatin1Char('|')).mid(1))
                if (x.contains(QLatin1String("->")) && !pyStrip(x.section(QStringLiteral("->"), 1)).isEmpty())
                    targets.push_back({ln, pyStrip(x.section(QStringLiteral("->"), 1)), curI});
            continue;
        }
        if (!ctx.opt.legacy && (cmd == QLatin1String("afk") || cmd == QLatin1String("farewell"))) {
            const QString img = pyStrip(rest.section(QLatin1Char('|'), cmd == QLatin1String("afk") ? 1 : 0, cmd == QLatin1String("afk") ? 1 : 0));
            if (!img.isEmpty() && img.toLower() != U("выкл")) {
                QString kind;
                const QString full = choiceImage(img, &kind, ctx.customImages);
                if (kind == QLatin1String("sprite")) checkSprite(ln, full);
                else if (!imageKnown(full) && !patchCg(ln, full))
                    add(ln, LintIssue::Warning, gbTr("Нет картинки «%1» — похожие: %2").arg(img, closest(full.mid(3), full.startsWith(QLatin1String("cg ")) ? (es ? es->cgs() : QStringList()) : (es ? es->backgrounds() : QStringList()))));
            }
            continue;
        }
        if (cmd == QLatin1String("codelock") && !ctx.opt.legacy) {
            const CodeLockSpec k = parseCodeLock(rest);
            const QString rawLine = rawLines.value(srcOf.value(i));
            static const QRegularExpression digits(QStringLiteral("^\\d+$"));
            if (k.code.isEmpty()) {
                add(ln, LintIssue::Error, gbTr("Какой код? «кодовыйзамок 1968 | Подсказка | верно -> сцена | неверно -> сцена»"));
            } else if (!digits.match(k.code).hasMatch()) {
                LintIssue x{ln, LintIssue::Error, U("На замке только цифры — «%1» так не набрать").arg(k.code), QString(), int(rawLine.indexOf(k.code)), int(k.code.size())};
                out.push_back(x);
            } else if (k.code.size() > 10) {
                add(ln, LintIssue::Warning, gbTr("Код из %1 цифр — игрок замучается; обычно 3–6").arg(k.code.size()));
            }
            if (!k.okTarget.isEmpty()) targets.push_back({ln, k.okTarget, curI});
            if (!k.badTarget.isEmpty()) targets.push_back({ln, k.badTarget, curI});
            if (k.okTarget.isEmpty() && k.badTarget.isEmpty())
                add(ln, LintIssue::Info, gbTr("Замок без «верно -> сцена»: после него история просто идёт дальше, что бы игрок ни набрал"));
            continue;
        }
        if (cmd == QLatin1String("unlockachievement") && !ctx.opt.legacy) {
            const AchSpec a = parseAchievement(rest);
            const QString rawLine = rawLines.value(srcOf.value(i));
            if (a.key.isEmpty()) add(ln, LintIssue::Error, gbTr("Какое достижение? «ачивка ключ | Название»"));
            for (const QString& n : a.needs)
                if (!achKeys.contains(n.toLower())) {
                    LintIssue x{ln, LintIssue::Warning, U("Достижения «%1» нигде нет — в «нужно=» пиши ключ другой «ачивки»").arg(n), QString(),
                                int(rawLine.indexOf(n)), int(n.size())};
                    out.push_back(x);
                }
            if (a.plat && achPlain == 0) add(ln, LintIssue::Warning, gbTr("«платина» приходит, когда собраны остальные — а других достижений нет"));
            continue;
        }
        if (cmd == QLatin1String("choice") && !ctx.opt.legacy) {
            choiceItems << 0;
            choiceTimed << (parseChoiceHead(rest).style == QLatin1String("timed"));
            continue;
        }
        if (cmd == QLatin1String("choice")) {
            choiceOpen = ln;
            timedChoice = !ctx.opt.legacy && choiceStyleOf(rest) == QLatin1String("timed");
            continue;
        }
        if (cmd == QLatin1String("endchoice")) { withFix({ln, LintIssue::Info, gbTr("«конецвыбора» без «выбор»")}, 3, QString(""), gbTr("убрать строку")); continue; }
        if (cmd == QLatin1String("jump") || cmd == QLatin1String("callscene")) {
            if (rest.isEmpty()) add(ln, LintIssue::Error, gbTr("Куда? переход <сцена>"));
            else targets.push_back({ln, rest, curI});
            continue;
        }
        if (cmd == QLatin1String("ifjump") || cmd == QLatin1String("ifpersistent") || cmd == QLatin1String("ifitem")) {
            if (!rest.contains(QLatin1String("->"))) add(ln, LintIssue::Error, gbTr("Формат: условие -> сцена"));
            else targets.push_back({ln, pyStrip(rest.section(QStringLiteral("->"), 1)), curI});
            continue;
        }
        if (cmd == QLatin1String("screenmenu")) {
            for (const QString& item : rest.split(QLatin1Char('|')))
                if (item.contains(QLatin1String("->")) && !pyStrip(item.section(QStringLiteral("->"), 1)).isEmpty())
                    targets.push_back({ln, pyStrip(item.section(QStringLiteral("->"), 1)), curI});
            continue;
        }
        if (cmd == QLatin1String("map") && !ctx.opt.legacy) {
            const MapSpec m = parseMapSpec(rest);
            if (m.places.isEmpty()) add(ln, LintIssue::Error, gbTr("На карте нет мест: карта площадь: сцена @sl, пляж: сцена2"));
            for (const MapEntry& p : m.places) {
                if (!p.target.isEmpty()) targets.push_back({ln, p.target, curI});
                else add(ln, LintIssue::Error, gbTr("Место «%1» никуда не ведёт: «%1: сцена»").arg(p.raw));
                if (p.zone.isEmpty()) {
                    QStringList known;
                    for (const EsMapZone& z : esMapZones()) known << z.title.toLower();
                    add(ln, LintIssue::Error, gbTr("На карте игры нет места «%1» — есть: %2").arg(p.raw, known.join(QStringLiteral(", "))));
                }
            }
            if (!m.done.isEmpty()) targets.push_back({ln, m.done, curI});
            else if (m.tour) add(ln, LintIssue::Info, gbTr("Обход без «готово: сцена» — когда все места пройдены, карта начнётся заново"));
            for (const QString& e : rest.split(QLatin1Char(','))) {
                if (!e.contains(QLatin1Char('@'))) continue;
                const QString who = pyStrip(e.section(QLatin1Char('@'), 1)).section(QLatin1Char(' '), 0, 0);
                if (esChibiId(who).isEmpty()) add(ln, LintIssue::Warning, gbTr("Нет мордочки «%1» для карты — пиши sl, dv, un, mi, us, mt… или имя: @Славя").arg(who));
            }
            continue;
        }
        if (cmd == QLatin1String("map")) {
            for (const QString& e : rest.split(QLatin1Char(','))) {
                QString entry = e.section(QLatin1Char('@'), 0, 0);
                const QString t = entry.contains(QLatin1Char(':')) ? entry.section(QLatin1Char(':'), 1) : entry.section(QStringLiteral("->"), 1);
                if (!pyStrip(t).isEmpty()) targets.push_back({ln, pyStrip(t), curI});
            }
            continue;
        }
        if (cmd == QLatin1String("show")) {
            popEffect();
            if (w.size() >= 2 && w.at(w.size() - 2) == QLatin1String("at")) { w.removeLast(); w.removeLast(); }
            else if (!w.isEmpty() && isPosition(w.last())) w.removeLast();
            if (w.isEmpty()) add(ln, LintIssue::Error, gbTr("Кого показать? показать dv smile pioneer center"));
            else checkSprite(ln, w.join(QLatin1Char(' ')));
            continue;
        }
        if (cmd == QLatin1String("mirror") || cmd == QLatin1String("mirrorbig") || cmd == QLatin1String("bigshow") || cmd == QLatin1String("pulse")) {
            popEffect();
            while (!w.isEmpty() && (pyIsNumber(w.last()) || isWalkPosition(w.last()))) w.removeLast();
            checkSprite(ln, w.join(QLatin1Char(' ')));
            continue;
        }
        if (cmd == QLatin1String("bg") || cmd == QLatin1String("showbg") || cmd == QLatin1String("cg")) {
            popEffect();
            const QString name = w.join(QLatin1Char(' '));
            if (name.isEmpty()) { add(ln, LintIssue::Error, gbTr("Какой фон?")); continue; }
            const QString low = name.toLower();
            if (low == QLatin1String("black") || low == QLatin1String("white")) continue;
            const QString full = (cmd == QLatin1String("cg") ? QStringLiteral("cg ") : QStringLiteral("bg ")) + name;
            if (patchCg(ln, full)) {
            } else if (!imageKnown(full) && es && es->missingImages.contains(full)) {
                add(ln, LintIssue::Warning, gbTr("«%1» объявлена в БЛ, но файла в игре нет — у игрока будет ошибка").arg(name));
            } else if (!imageKnown(full)) {
                QStringList opts = cmd == QLatin1String("cg") ? (es ? es->cgs() : QStringList()) : (es ? es->backgrounds() : QStringList());
                swapFix(ln, LintIssue::Warning, gbTr("Нет %1 «%2» — похожие: %3").arg(cmd == QLatin1String("cg") ? U("CG") : gbTr("фона"), name, closest(name, opts)),
                        name, opts);
            }
            continue;
        }
        if (cmd == QLatin1String("music") && es) {
            QStringList mw;
            for (int k = 0; k < w.size(); ++k) {
                const QString x = w[k].toLower();
                if (x == QLatin1String("fadein") || x == QLatin1String("fadeout")) { ++k; continue; }
                if (x == QLatin1String("loop") || x == QLatin1String("noloop")) continue;
                mw << w[k];
            }
            const QString name = mw.join(QLatin1Char(' '));
            QString key = name.toLower();
            key.replace(QLatin1Char('_'), QLatin1Char(' ')).replace(QLatin1Char('-'), QLatin1Char(' '));
            const bool alias = key.startsWith(QLatin1String("lightness"));
            if (!name.isEmpty() && es->missingAudio.contains(name))
                add(ln, LintIssue::Warning, gbTr("Трек «%1» объявлен в БЛ, но файла в Steam-версии нет — музыка не заиграет").arg(name));
            else if (!name.isEmpty() && !es->music.contains(name) && !alias)
                swapFix(ln, LintIssue::Warning, gbTr("Нет трека «%1» в БЛ — похожие: %2").arg(name, closest(name, es->music.keys())), name, es->music.keys());
            continue;
        }
        if ((cmd == QLatin1String("sound") || cmd == QLatin1String("voice")) && es) {
            if (rest.isEmpty() || rest.startsWith(QLatin1Char('"')) || rest.startsWith(QLatin1Char('\'')) || rest.startsWith(QLatin1String("u\""))) continue;
            if (es->missingAudio.contains(rest))
                add(ln, LintIssue::Warning, gbTr("Звук «%1» объявлен в БЛ, но файла в Steam-версии нет").arg(rest));
            else if (!es->sounds.contains(rest) && !es->ambience.contains(rest))
                swapFix(ln, LintIssue::Warning, gbTr("Нет звука «%1» в БЛ — похожие: %2").arg(rest, closest(rest, es->sounds.keys())), rest, es->sounds.keys());
            continue;
        }
        if (cmd == QLatin1String("ambience") && es && !w.isEmpty()) {
            const QString v = w[0];
            if (!v.contains(QLatin1Char('/')) && !v.contains(QLatin1Char('.')) && !es->ambience.contains(v))
                swapFix(ln, LintIssue::Warning, gbTr("Нет атмосферы «%1» — похожие: %2").arg(v, closest(v, es->ambience.keys())), v, es->ambience.keys());
            continue;
        }
        if (cmd == QLatin1String("musicfile") || cmd == QLatin1String("soundfile") || cmd == QLatin1String("voicefile")) {
            const QString p = w.value(0);
            if (!p.isEmpty() && !p.startsWith(QLatin1String("mods/")) && !ctx.customAudio.contains(p))
                add(ln, LintIssue::Warning, gbTr("Файла «%1» нет в проекте — добавь его во вкладке «Звук»").arg(p));
            continue;
        }
        if (cmd == QLatin1String("phonestart")) { phoneOpen = ln; continue; }
        if (cmd == QLatin1String("sms") && !phoneOpen) { add(ln, LintIssue::Warning, gbTr("«смс» без «телефонначать» — сообщение не будет видно")); continue; }
        if (cmd == QLatin1String("phoneend")) { phoneOpen = 0; continue; }
        if ((cmd == QLatin1String("weather") && !w.isEmpty() && weatherKey(w[0]).isEmpty() &&
             !QStringList{U("стоп"), U("нет"), U("выключить"), U("убрать"), QStringLiteral("off"), QStringLiteral("none"), QStringLiteral("clear"), U("чисто")}.contains(w[0].toLower()) &&
             !isEffect(w[0])))
            swapFix(ln, LintIssue::Warning, gbTr("Погода «%1» неизвестна: снег, дождь, листья, сердца, искры, пыль, стоп").arg(w[0]), w[0],
                    {U("снег"), U("дождь"), U("листья"), U("сердца"), U("искры"), U("пыль"), U("стоп")});
        if (cmd == QLatin1String("colorfilter") && !w.isEmpty() && filterKey(w[0]).isEmpty() && !isEffect(w[0]) &&
            !QStringList{U("нет"), U("стоп"), U("выкл"), U("выключить"), QStringLiteral("off"), QStringLiteral("none"), U("убрать"), U("чисто")}.contains(w[0].toLower()))
            swapFix(ln, LintIssue::Warning, gbTr("Фильтр «%1» неизвестен: сепия, чб, ночь, тепло, холод, сон, выцвет, хоррор, нет").arg(w[0]), w[0],
                    {U("сепия"), U("чб"), U("ночь"), U("тепло"), U("холод"), U("сон"), U("выцвет"), U("хоррор"), U("нет")});
    }
    if (choiceOpen) withFix({choiceOpen, LintIssue::Warning, gbTr("Выбор не закрыт «конецвыбора» — закрою сам в конце")}, 4, U("конецвыбора"), U("конецвыбора"));
    if (!choiceItems.isEmpty()) {
        withFix({srcOf.isEmpty() ? 1 : srcOf.last() + 1, LintIssue::Info, gbTr("«выбор» без «конецвыбора» — закрою сам в конце")}, 4,
                U("конецвыбора"), U("конецвыбора"));
        if (choiceItems.last() == 0) add(srcOf.isEmpty() ? 1 : srcOf.last() + 1, LintIssue::Warning, gbTr("В «выбор» нет ни одного варианта «- Текст» — меню не покажется"));
    }
    // a scene runs to its end and the mod ends there (V1 closes every scene): the next scene in the text is not
    // «the next one» - it needs a «переход». Said at the scene's last line, with the fix.
    if (!ctx.opt.legacy) {
        const QStringList& all = rawLines;
        struct Head { int line; QString name; };
        QVector<Head> heads;
        for (int i = 0; i < all.size(); ++i) {
            const QString s = pyStrip(all[i]);
            if (s.startsWith(QLatin1Char(':'))) heads.push_back({i, pyStrip(s.mid(1))});
        }
        static const QSet<QString> ends{QStringLiteral("jump"), QStringLiteral("return"), QStringLiteral("endgame"),
                                        QStringLiteral("bestmeter"), QStringLiteral("map"), QStringLiteral("screenmenu")};
        // Выбор 2.0: a choice closes the scene only when every one of its options leads to a scene (an option without
        // «-> сцена» goes on after the choice - and the scene is over there)
        // -1: the line is not the end of a choice (nor inside one); 1: every option of that choice leads away; 0: not
        auto choiceLeadsAway = [&](int at) {
            int depth = 0;
            bool every = true, any = false, header = false;
            for (int i = at; i >= 0; --i) {
                const QString t = pyStrip(all[i]);
                const QString c = normalizeCommand(firstWord(t));
                if (t.startsWith(QLatin1Char(':'))) break;
                if (c == QLatin1String("endchoice")) {
                    if (i != at) ++depth;
                    continue;
                }
                if (c == QLatin1String("choice")) {
                    if (depth == 0) { header = true; break; }
                    --depth;
                    continue;
                }
                if (depth == 0 && isChoiceItemLine(t)) {
                    any = true;
                    if (parseChoiceItem(t).target.isEmpty()) every = false;
                }
            }
            if (!header) return -1;
            return any && every ? 1 : 0;
        };
        for (int k = 0; k + 1 < heads.size(); ++k) {
            int last = -1;
            for (int i = heads[k + 1].line - 1; i > heads[k].line; --i) {
                const QString s = pyStrip(all[i]);
                if (!s.isEmpty() && !s.startsWith(QLatin1Char('#'))) { last = i; break; }
            }
            if (last < 0) continue;                                       // an empty scene: said elsewhere
            const QString s = pyStrip(all[last]);
            const QString word = normalizeCommand(s.section(QLatin1Char(' '), 0, 0));
            const int ch = choiceLeadsAway(last);
            if (ch == 1) continue;                                        // a choice whose every option leads to a scene
            if (ch == -1 && (ends.contains(word) || s.toLower().startsWith(U("конецменюмода")))) continue;
            if (ch == -1 && word == QLatin1String("codelock")) {
                const CodeLockSpec k = parseCodeLock(pyStrip(s.mid(s.section(QLatin1Char(' '), 0, 0).size())));
                if (!k.okTarget.isEmpty() && !k.badTarget.isEmpty()) continue;
            }
            // «звонок … | принять -> a | сбросить -> b»: both answers lead away
            if (ch == -1 && word == QLatin1String("phonecall")) {
                bool accept = false, decline = false;
                for (const QString& x : s.split(QLatin1Char('|'))) {
                    if (!x.contains(QLatin1String("->"))) continue;
                    const QString k = pyStrip(x.section(QStringLiteral("->"), 0, 0)).toLower();
                    if (k.startsWith(U("прин")) || k.startsWith(U("ответ")) || k == QLatin1String("accept")) accept = true;
                    else if (k.startsWith(U("сброс")) || k.startsWith(U("откл")) || k == QLatin1String("decline")) decline = true;
                }
                if (accept && decline) continue;
            }
            if (word == QLatin1String("renpy") && (s.contains(QLatin1String("jump ")) || s.endsWith(QLatin1String("return")))) continue;
            const QString jump = U("переход ") + heads[k + 1].name;
            if (ch == 0)
                withFix({last + 1, LintIssue::Warning,
                         U("После выбора сцена «%1» кончается — варианты без «-> сцена» в игре закончат мод. Допиши после «конецвыбора» «переход %2» "
                           "(или дай каждому варианту свою сцену)").arg(heads[k].name, heads[k + 1].name)},
                        pyStrip(all[last]).startsWith(U("конецвыбора")) ? 2 : 0, jump, jump);
            else
                withFix({last + 1, LintIssue::Warning,
                         U("Сцена «%1» кончается без перехода — в игре мод тут и закончится. Нужна следующая «%2»? Допиши «переход %2»")
                             .arg(heads[k].name, heads[k + 1].name)},
                        all[last].startsWith(QLatin1Char(' ')) || all[last].startsWith(QLatin1Char('\t')) ? 0 : 2, jump, jump);
        }
    }
    // a scene no way leads to: the player never sees it
    if (!ctx.opt.legacy)
        for (const auto& u : unreachableScenes(text, ctx.opt))
            add(u.first, LintIssue::Warning, gbTr("Сцену «%1» игрок не увидит: в неё не ведёт ни переход, ни выбор, ни кнопка. Посмотри «Карту сюжета» (Ctrl+M)").arg(u.second));
    for (const Ref& t : targets) {
        QString l = sl(t.name);
        if (l == QLatin1String("start")) l = meta.modId;
        if (labels.contains(l)) continue;
        LintIssue x{t.line, LintIssue::Warning, gbTr("Нет сцены «%1» — будет пустая сцена с мгновенным выходом").arg(t.name)};
        // «переход finsh»: the scene that is there; a name nothing is like: that scene, at the end of the story
        const QString best = closest(t.name, sceneTitles, 1);
        const QString fixed = nearMiss(t.name, best) ? swapIn(t.idx, t.name, best) : QString();
        if (!fixed.isEmpty()) withFix(x, 1, fixed, best);
        else withFix(x, 4, U(": %1\nтекст ").arg(t.name), gbTr("создать «%1»").arg(t.name));
    }
    std::sort(out.begin(), out.end(), [](const LintIssue& a, const LintIssue& b) { return a.line < b.line; });
    return out;
}

QString applyFixes(const QString& text, const QVector<LintIssue>& issues)
{
    QVector<LintIssue> fixes;
    for (const LintIssue& i : issues)
        if (i.fixMode > 0 && (i.line > 0 || i.fixMode == 4)) fixes.push_back(i);
    // bottom up (the lines above keep their numbers); on one line: under it, the line itself, away, above it
    static const int rank[] = {9, 1, 0, 2, 9, 3};
    std::stable_sort(fixes.begin(), fixes.end(), [](const LintIssue& a, const LintIssue& b) {
        if (a.line != b.line) return a.line > b.line;
        return rank[qBound(0, a.fixMode, 5)] < rank[qBound(0, b.fixMode, 5)];
    });
    QStringList lines = text.split(QLatin1Char('\n'));
    QSet<int> touched;
    QStringList tail;
    for (const LintIssue& f : fixes) {
        const int at = f.line - 1;
        if (f.fixMode == 4) {
            if (!tail.contains(f.fix)) tail << f.fix;
            continue;
        }
        if (at < 0 || at >= lines.size()) continue;
        if ((f.fixMode == 1 || f.fixMode == 3) && touched.contains(f.line)) continue;
        if (f.fixMode == 1) { lines[at] = f.fix; touched.insert(f.line); }
        else if (f.fixMode == 2) { const QStringList add = f.fix.split(QLatin1Char('\n')); for (int k = add.size() - 1; k >= 0; --k) lines.insert(at + 1, add[k]); }
        else if (f.fixMode == 3) { lines.removeAt(at); touched.insert(f.line); }
        else if (f.fixMode == 5) { const QStringList add = f.fix.split(QLatin1Char('\n')); for (int k = add.size() - 1; k >= 0; --k) lines.insert(at, add[k]); }
    }
    QString out = lines.join(QLatin1Char('\n'));
    for (const QString& t : tail) {
        if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
        if (!out.isEmpty() && !out.endsWith(QStringLiteral("\n\n"))) out += QLatin1Char('\n');
        out += t + QLatin1Char('\n');
    }
    return out;
}

} // namespace gb
