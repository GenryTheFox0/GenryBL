#include "Lint.h"
#include "EsAssets.h"
#include "Overlays.h"
#include "Py.h"
#include "Screenplay.h"
#include "Text.h"
#include "Wardrobe.h"

#include <QHash>
#include <algorithm>

namespace gb {

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

struct Ref {
    int line;
    QString name;
};

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
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        const QString w0 = firstWord(s);
        if (normalizeCommand(w0) == QLatin1String("meter"))
            meterVars.insert(slugOf(pyStrip(pyStrip(s.mid(w0.size())).section(QLatin1Char('|'), 0, 0)), QStringLiteral("value"), ctx.opt));
    }
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
            add(ln, LintIssue::Info, U("CG из 18+ патча ляжет прямо в мод (images/genry_patch) — игроки увидят его и без патча"));
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
            add(ln, LintIssue::Warning, U("«%1» объявлена в БЛ, но файла в игре нет — у игрока будет ошибка").arg(n));
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
            add(ln, LintIssue::Warning, U("У %1 нет «%2» — похожие: %3").arg(es->characterName(tag).isEmpty() ? tag : es->characterName(tag), rest,
                                                                       closest(rest, es->spriteNames(tag))));
            return;
        }
        add(ln, LintIssue::Warning, U("Нет картинки «%1» — это не персонаж БЛ и не свой PNG проекта").arg(n));
    };

    for (int i = 0; i < lines.size(); ++i) {
        const int ln = srcOf[i] + 1;
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
                static const QSet<QString> special{U("галерея"), U("достижения"), U("шкалы"), U("главы"), U("настройки"), U("загрузить"), U("выход")};
                if (rest.contains(QLatin1String("->"))) {
                    const QString t = pyStrip(rest.section(QStringLiteral("->"), 1));
                    if (!special.contains(menuButtonKind(t))) targets.push_back({ln, t});
                } else if (rest.isEmpty()) {
                    add(ln, LintIssue::Warning, U("Кнопка без текста: «кнопка Начать -> start» или «кнопка Галерея»"));
                } else if (!special.contains(menuButtonKind(rest)) && ++menuStarts > 1) {
                    // a second button with no target and no known word: it starts the mod too - most likely not meant
                    add(ln, LintIssue::Info, U("Кнопка «%1» тоже начинает мод с начала. Другое нужно — «кнопка %1 -> главы» "
                                               "(или галерея, настройки, загрузить, выход, имя сцены)").arg(rest));
                }
                continue;
            }
        }
        if (cmd == QLatin1String("modmenu")) { menuOpen = ln; menuStarts = 0; continue; }
        if (cmd == QLatin1String("meter")) {
            const QString v = pyStrip(rest.section(QLatin1Char('|'), 0, 0));
            if (v.isEmpty()) add(ln, LintIssue::Error, U("Шкала: «шкала симпатия_алисы | Алиса | #ff7a00 | 0 | 10»"));
            continue;
        }
        if (cmd == QLatin1String("meters") && meterVars.isEmpty())
            add(ln, LintIssue::Warning, U("Нет ни одной «шкала …» — экран шкал будет пустым"));
        if (cmd == QLatin1String("bestmeter")) {
            for (const QString& part : rest.split(QLatin1Char('|'))) {
                if (!part.contains(QLatin1String("->"))) continue;
                const QString left = pyStrip(part.section(QStringLiteral("->"), 0, 0));
                targets.push_back({ln, pyStrip(part.section(QStringLiteral("->"), 1))});
                const QString l = left.toLower();
                if (l != U("иначе") && l != QLatin1String("else") && l != U("ничья") && !meterVars.contains(slugOf(left, QStringLiteral("value"), ctx.opt)))
                    add(ln, LintIssue::Warning, U("«%1» — не шкала: объяви её «шкала %1 | Имя | #цвет | 0 | 10»").arg(left));
            }
            continue;
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
                    if (raw.isEmpty()) add(ln, LintIssue::Warning, U("После «|» нужна картинка: sl smile pioneer, bg ext_beach_day или cg …"));
                    else if (kind == QLatin1String("sprite")) checkSprite(ln, img);
                    else if (!patchCg(ln, img) && !imageKnown(img)) {
                        const bool cg = img.startsWith(QLatin1String("cg "));
                        add(ln, LintIssue::Warning, U("Нет картинки «%1» для варианта — похожие: %2")
                                                         .arg(raw, closest(img.mid(3), cg ? (es ? es->cgs() : QStringList()) : (es ? es->backgrounds() : QStringList()))));
                    }
                }
                if (pyStrip(s.mid(1).section(QStringLiteral("->"), 0, 0)).isEmpty()) add(ln, LintIssue::Warning, U("Пустой текст варианта"));
                targets.push_back({ln, t});
                continue;
            }
            if (!ctx.opt.legacy && s.contains(QLatin1String("->")) && isTimeoutWord(s.section(QStringLiteral("->"), 0, 0))) {
                if (!timedChoice) add(ln, LintIssue::Warning, U("«время вышло» работает только в «выбор на время N»"));
                targets.push_back({ln, pyStrip(s.section(QStringLiteral("->"), 1))});
                continue;
            }
            add(ln, LintIssue::Error, U("Внутри «выбор» только строки «- Текст -> сцена» — иначе сборка упадёт"));
            continue;
        }
        if (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) {
            QString l = sl(s.startsWith(QLatin1Char(':')) ? pyStrip(s.mid(1)) : rest);
            if (l == QLatin1String("start")) l = meta.modId;
            if (labels.value(l, -1) > 0) add(ln, LintIssue::Error, U("Сцена «%1» уже есть на строке %2 — Ren'Py не запустится").arg(l).arg(labels[l]));
            labels.insert(l, ln);
            continue;
        }
        if (s.contains(QLatin1Char(':')) && !isCommandName(cmd) && !s.toLower().startsWith(QLatin1String("http"))) {
            const QString who = pyStrip(s.section(QLatin1Char(':'), 0, 0));
            const QString id = speakerId(who, declared, ctx.opt);
            if (id.startsWith(QLatin1String("genry_sp_")) && !newSpeakers.contains(id)) {
                newSpeakers.insert(id);
                add(ln, LintIssue::Info, U("Новый говорящий «%1» — объявлю его сам (или «персонаж id Имя #цвет»)").arg(who));
            }
            continue;
        }
        if (!isCommandName(cmd)) {
            add(ln, LintIssue::Warning, U("«%1» — не команда: в мод уйдёт комментарием").arg(first));
            continue;
        }
        auto popEffect = [&] { if (!w.isEmpty() && isEffect(w.last())) w.removeLast(); };
        if (cmd == QLatin1String("choice")) {
            choiceOpen = ln;
            timedChoice = !ctx.opt.legacy && choiceStyleOf(rest) == QLatin1String("timed");
            continue;
        }
        if (cmd == QLatin1String("endchoice")) { add(ln, LintIssue::Info, U("«конецвыбора» без «выбор»")); continue; }
        if (cmd == QLatin1String("jump") || cmd == QLatin1String("callscene")) {
            if (rest.isEmpty()) add(ln, LintIssue::Error, U("Куда? переход <сцена>"));
            else targets.push_back({ln, rest});
            continue;
        }
        if (cmd == QLatin1String("ifjump") || cmd == QLatin1String("ifpersistent") || cmd == QLatin1String("ifitem")) {
            if (!rest.contains(QLatin1String("->"))) add(ln, LintIssue::Error, U("Формат: условие -> сцена"));
            else targets.push_back({ln, pyStrip(rest.section(QStringLiteral("->"), 1))});
            continue;
        }
        if (cmd == QLatin1String("screenmenu")) {
            for (const QString& item : rest.split(QLatin1Char('|')))
                if (item.contains(QLatin1String("->")) && !pyStrip(item.section(QStringLiteral("->"), 1)).isEmpty())
                    targets.push_back({ln, pyStrip(item.section(QStringLiteral("->"), 1))});
            continue;
        }
        if (cmd == QLatin1String("map") && !ctx.opt.legacy) {
            const MapSpec m = parseMapSpec(rest);
            if (m.places.isEmpty()) add(ln, LintIssue::Error, U("На карте нет мест: карта площадь: сцена @sl, пляж: сцена2"));
            for (const MapEntry& p : m.places) {
                if (!p.target.isEmpty()) targets.push_back({ln, p.target});
                else add(ln, LintIssue::Error, U("Место «%1» никуда не ведёт: «%1: сцена»").arg(p.raw));
                if (p.zone.isEmpty()) {
                    QStringList known;
                    for (const EsMapZone& z : esMapZones()) known << z.title.toLower();
                    add(ln, LintIssue::Error, U("На карте игры нет места «%1» — есть: %2").arg(p.raw, known.join(QStringLiteral(", "))));
                }
            }
            if (!m.done.isEmpty()) targets.push_back({ln, m.done});
            else if (m.tour) add(ln, LintIssue::Info, U("Обход без «готово: сцена» — когда все места пройдены, карта начнётся заново"));
            for (const QString& e : rest.split(QLatin1Char(','))) {
                if (!e.contains(QLatin1Char('@'))) continue;
                const QString who = pyStrip(e.section(QLatin1Char('@'), 1)).section(QLatin1Char(' '), 0, 0);
                if (esChibiId(who).isEmpty()) add(ln, LintIssue::Warning, U("Нет мордочки «%1» для карты — пиши sl, dv, un, mi, us, mt… или имя: @Славя").arg(who));
            }
            continue;
        }
        if (cmd == QLatin1String("map")) {
            for (const QString& e : rest.split(QLatin1Char(','))) {
                QString entry = e.section(QLatin1Char('@'), 0, 0);
                const QString t = entry.contains(QLatin1Char(':')) ? entry.section(QLatin1Char(':'), 1) : entry.section(QStringLiteral("->"), 1);
                if (!pyStrip(t).isEmpty()) targets.push_back({ln, pyStrip(t)});
            }
            continue;
        }
        if (cmd == QLatin1String("show")) {
            popEffect();
            if (w.size() >= 2 && w.at(w.size() - 2) == QLatin1String("at")) { w.removeLast(); w.removeLast(); }
            else if (!w.isEmpty() && isPosition(w.last())) w.removeLast();
            if (w.isEmpty()) add(ln, LintIssue::Error, U("Кого показать? показать dv smile pioneer center"));
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
            if (name.isEmpty()) { add(ln, LintIssue::Error, U("Какой фон?")); continue; }
            const QString low = name.toLower();
            if (low == QLatin1String("black") || low == QLatin1String("white")) continue;
            const QString full = (cmd == QLatin1String("cg") ? QStringLiteral("cg ") : QStringLiteral("bg ")) + name;
            if (patchCg(ln, full)) {
            } else if (!imageKnown(full) && es && es->missingImages.contains(full)) {
                add(ln, LintIssue::Warning, U("«%1» объявлена в БЛ, но файла в игре нет — у игрока будет ошибка").arg(name));
            } else if (!imageKnown(full)) {
                QStringList opts = cmd == QLatin1String("cg") ? (es ? es->cgs() : QStringList()) : (es ? es->backgrounds() : QStringList());
                add(ln, LintIssue::Warning, U("Нет %1 «%2» — похожие: %3").arg(cmd == QLatin1String("cg") ? U("CG") : U("фона"), name, closest(name, opts)));
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
                add(ln, LintIssue::Warning, U("Трек «%1» объявлен в БЛ, но файла в Steam-версии нет — музыка не заиграет").arg(name));
            else if (!name.isEmpty() && !es->music.contains(name) && !alias)
                add(ln, LintIssue::Warning, U("Нет трека «%1» в БЛ — похожие: %2").arg(name, closest(name, es->music.keys())));
            continue;
        }
        if ((cmd == QLatin1String("sound") || cmd == QLatin1String("voice")) && es) {
            if (rest.isEmpty() || rest.startsWith(QLatin1Char('"')) || rest.startsWith(QLatin1Char('\'')) || rest.startsWith(QLatin1String("u\""))) continue;
            if (es->missingAudio.contains(rest))
                add(ln, LintIssue::Warning, U("Звук «%1» объявлен в БЛ, но файла в Steam-версии нет").arg(rest));
            else if (!es->sounds.contains(rest) && !es->ambience.contains(rest))
                add(ln, LintIssue::Warning, U("Нет звука «%1» в БЛ — похожие: %2").arg(rest, closest(rest, es->sounds.keys())));
            continue;
        }
        if (cmd == QLatin1String("ambience") && es && !w.isEmpty()) {
            const QString v = w[0];
            if (!v.contains(QLatin1Char('/')) && !v.contains(QLatin1Char('.')) && !es->ambience.contains(v))
                add(ln, LintIssue::Warning, U("Нет атмосферы «%1» — похожие: %2").arg(v, closest(v, es->ambience.keys())));
            continue;
        }
        if (cmd == QLatin1String("musicfile") || cmd == QLatin1String("soundfile") || cmd == QLatin1String("voicefile")) {
            const QString p = w.value(0);
            if (!p.isEmpty() && !p.startsWith(QLatin1String("mods/")) && !ctx.customAudio.contains(p))
                add(ln, LintIssue::Warning, U("Файла «%1» нет в проекте — добавь его во вкладке «Звук»").arg(p));
            continue;
        }
        if (cmd == QLatin1String("phonestart")) { phoneOpen = ln; continue; }
        if (cmd == QLatin1String("sms") && !phoneOpen) { add(ln, LintIssue::Warning, U("«смс» без «телефонначать» — сообщение не будет видно")); continue; }
        if (cmd == QLatin1String("phoneend")) { phoneOpen = 0; continue; }
        if ((cmd == QLatin1String("weather") && !w.isEmpty() && weatherKey(w[0]).isEmpty() &&
             !QStringList{U("стоп"), U("нет"), U("выключить"), U("убрать"), QStringLiteral("off"), QStringLiteral("none"), QStringLiteral("clear"), U("чисто")}.contains(w[0].toLower()) &&
             !isEffect(w[0])))
            add(ln, LintIssue::Warning, U("Погода «%1» неизвестна: снег, дождь, листья, сердца, искры, пыль, стоп").arg(w[0]));
        if (cmd == QLatin1String("colorfilter") && !w.isEmpty() && filterKey(w[0]).isEmpty() && !isEffect(w[0]) &&
            !QStringList{U("нет"), U("стоп"), U("выкл"), U("выключить"), QStringLiteral("off"), QStringLiteral("none"), U("убрать"), U("чисто")}.contains(w[0].toLower()))
            add(ln, LintIssue::Warning, U("Фильтр «%1» неизвестен: сепия, чб, ночь, тепло, холод, сон, выцвет, хоррор, нет").arg(w[0]));
    }
    if (choiceOpen) add(choiceOpen, LintIssue::Warning, U("Выбор не закрыт «конецвыбора» — закрою сам в конце"));
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
        static const QSet<QString> ends{QStringLiteral("jump"), QStringLiteral("return"), QStringLiteral("endgame"), QStringLiteral("endchoice"),
                                        QStringLiteral("bestmeter"), QStringLiteral("map"), QStringLiteral("screenmenu")};
        for (int k = 0; k + 1 < heads.size(); ++k) {
            int last = -1;
            for (int i = heads[k + 1].line - 1; i > heads[k].line; --i) {
                const QString s = pyStrip(all[i]);
                if (!s.isEmpty() && !s.startsWith(QLatin1Char('#'))) { last = i; break; }
            }
            if (last < 0) continue;                                       // an empty scene: said elsewhere
            const QString s = pyStrip(all[last]);
            const QString word = normalizeCommand(s.section(QLatin1Char(' '), 0, 0));
            if (ends.contains(word) || s.startsWith(QLatin1Char('-')) || s.toLower().startsWith(U("конецменюмода"))) continue;
            if (word == QLatin1String("renpy") && (s.contains(QLatin1String("jump ")) || s.endsWith(QLatin1String("return")))) continue;
            add(last + 1, LintIssue::Warning,
                U("Сцена «%1» кончается без перехода — в игре мод тут и закончится. Нужна следующая «%2»? Допиши «переход %2»")
                    .arg(heads[k].name, heads[k + 1].name));
        }
    }
    for (const Ref& t : targets) {
        QString l = sl(t.name);
        if (l == QLatin1String("start")) l = meta.modId;
        if (!labels.contains(l))
            add(t.line, LintIssue::Warning, U("Нет сцены «%1» — будет пустая сцена с мгновенным выходом").arg(t.name));
    }
    std::sort(out.begin(), out.end(), [](const LintIssue& a, const LintIssue& b) { return a.line < b.line; });
    return out;
}

} // namespace gb
