#include "Forms.h"

#include "Compiler.h"
#include "Text.h"

#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace gb {
namespace {

QString U(const char* s) { return QString::fromUtf8(s); }
QString S(const char* s) { return QString::fromLatin1(s); }

// ------------------------------------------------------------------ templates

struct Node {
    enum Kind { Text, Ph, Group };
    Kind kind = Text;
    QString s;              // text, or the placeholder's field key
    QVector<Node> kids;     // a group's contents
};

QVector<Node> parseTemplate(const QString& t, int& i, bool inGroup)
{
    QVector<Node> out;
    QString text;
    auto flush = [&] {
        if (text.isEmpty()) return;
        Node n;
        n.s = text;
        out.push_back(n);
        text.clear();
    };
    while (i < t.size()) {
        const QChar c = t.at(i);
        if (c == QLatin1Char('{')) {
            const int end = int(t.indexOf(QLatin1Char('}'), i));
            if (end < 0) { text += t.mid(i); i = int(t.size()); break; }
            flush();
            Node n;
            n.kind = Node::Ph;
            n.s = t.mid(i + 1, end - i - 1);
            out.push_back(n);
            i = end + 1;
        } else if (c == QLatin1Char('[')) {
            flush();
            ++i;
            Node g;
            g.kind = Node::Group;
            g.kids = parseTemplate(t, i, true);
            out.push_back(g);
        } else if (c == QLatin1Char(']') && inGroup) {
            ++i;
            break;
        } else {
            text += c;
            ++i;
        }
    }
    flush();
    return out;
}

QVector<Node> parseTemplate(const QString& t)
{
    int i = 0;
    return parseTemplate(t, i, false);
}

// ------------------------------------------------------------------ fields

using Fields = QHash<QString, QVariantMap>;

Fields fieldsOf(const QVariantList& list)
{
    Fields f;
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        f.insert(m.value(S("k")).toString(), m);
    }
    return f;
}

QString defOf(const QVariantMap& f, const QString& variant)
{
    const QVariantMap defs = f.value(S("defs")).toMap();
    if (!variant.isEmpty() && defs.contains(variant)) return defs.value(variant).toString();
    return f.value(S("def")).toString();
}

QVariant defaultValue(const QVariantMap& f, const QString& variant)
{
    const QString t = f.value(S("t")).toString();
    if (t == QLatin1String("bool")) return f.value(S("def")).toBool();
    if (t == QLatin1String("list")) return f.value(S("def")).toList();
    return defOf(f, variant);
}

QString templateOf(const QVariantMap& form, const QString& variant)
{
    const QString by = form.value(S("by")).toString();
    if (by.isEmpty()) return form.value(S("build")).toString();
    const QVariantMap builds = form.value(S("builds")).toMap();
    if (builds.contains(variant)) return builds.value(variant).toString();
    return QString();
}

QStringList variantsOf(const QVariantMap& form)
{
    const QString by = form.value(S("by")).toString();
    if (by.isEmpty()) return {QString()};
    QStringList out;
    for (const QVariant& f : form.value(S("fields")).toList()) {
        const QVariantMap m = f.toMap();
        if (m.value(S("k")).toString() != by) continue;
        for (const QVariant& o : m.value(S("opts")).toList()) out << o.toList().value(0).toString();
    }
    return out;
}

QString defaultVariant(const QVariantMap& form)
{
    const QString by = form.value(S("by")).toString();
    if (by.isEmpty()) return {};
    for (const QVariant& f : form.value(S("fields")).toList())
        if (f.toMap().value(S("k")).toString() == by) return f.toMap().value(S("def")).toString();
    return variantsOf(form).value(0);
}

// ------------------------------------------------------------------ build

struct Scope {
    const Fields& fields;
    const QVariantMap& values;
    QString variant;

    QVariant value(const QString& k) const
    {
        if (values.contains(k)) return values.value(k);
        return defaultValue(fields.value(k), variant);
    }
    bool isSet(const QString& k) const
    {
        const QVariantMap f = fields.value(k);
        const QString t = f.value(S("t")).toString();
        const QVariant v = value(k);
        if (t == QLatin1String("bool")) return v.toBool();
        if (t == QLatin1String("list")) return !v.toList().isEmpty();
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) return false;
        if (f.value(S("keep")).toBool()) return true;
        return s != defOf(f, variant);
    }
    QString text(const QString& k) const;
};

QString render(const QVector<Node>& nodes, const Scope& sc);

bool anySet(const QVector<Node>& nodes, const Scope& sc)
{
    for (const Node& n : nodes) {
        if (n.kind == Node::Ph && sc.isSet(n.s)) return true;
        if (n.kind == Node::Group && anySet(n.kids, sc)) return true;
    }
    return false;
}

QString Scope::text(const QString& k) const
{
    const QVariantMap f = fields.value(k);
    const QString t = f.value(S("t")).toString();
    const QVariant v = value(k);
    if (t == QLatin1String("bool")) return v.toBool() ? f.value(S("word")).toString() : QString();
    if (t == QLatin1String("list")) {
        const Fields sub = fieldsOf(f.value(S("fields")).toList());
        const QVector<Node> item = parseTemplate(f.value(S("item")).toString());
        QStringList parts;
        for (const QVariant& it : v.toList()) {
            const QVariantMap iv = it.toMap();
            const Scope isc{sub, iv, QString()};
            const QString s = render(item, isc).trimmed();
            if (!s.isEmpty()) parts << s;
        }
        return parts.join(f.value(S("sep")).toString());
    }
    QString s = v.toString().trimmed();
    if (s.isEmpty()) s = defOf(f, variant);
    if (f.value(S("oneline")).toBool()) {
        s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        if (f.value(S("nl")).toBool()) s.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        else s.replace(QRegularExpression(QStringLiteral("\\s*\\n\\s*")), QStringLiteral(" "));
    }
    return s;
}

QString render(const QVector<Node>& nodes, const Scope& sc)
{
    QString out;
    for (const Node& n : nodes) {
        if (n.kind == Node::Text) out += n.s;
        else if (n.kind == Node::Ph) out += sc.text(n.s);
        else if (anySet(n.kids, sc)) out += render(n.kids, sc);
    }
    return out;
}

// ------------------------------------------------------------------ parse

QString alternation(const QVariantList& opts, bool withNone)
{
    QStringList vals;
    for (const QVariant& o : opts) {
        const QString v = o.toList().value(0).toString();
        if (!v.isEmpty()) vals << v;
    }
    if (withNone) vals << S("none");
    std::sort(vals.begin(), vals.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (QString& v : vals) v = QRegularExpression::escape(v);
    return vals.isEmpty() ? S("(?!)") : vals.join(QLatin1Char('|'));
}

QString pattern(const QVector<Node>& nodes, const Fields& fields, bool named = true);

QString placeholderPattern(const QVariantMap& f)
{
    const QString t = f.value(S("t")).toString();
    if (t == QLatin1String("list")) {           // item (sep item)*: every item must have the item's own shape
        const QString item = S("(?:") + pattern(parseTemplate(f.value(S("item")).toString()), fieldsOf(f.value(S("fields")).toList()), false) + QLatin1Char(')');
        return item + S("(?:") + QRegularExpression::escape(f.value(S("sep")).toString()) + item + S(")*");
    }
    if (t == QLatin1String("bool")) return QRegularExpression::escape(f.value(S("word")).toString());
    if (t == QLatin1String("choice") || t == QLatin1String("pos") || t == QLatin1String("zone") || t == QLatin1String("chibi"))
        return alternation(f.value(S("opts")).toList(), false);
    if (t == QLatin1String("effect")) return alternation(f.value(S("opts")).toList(), true);
    if (t == QLatin1String("num")) return S("-?\\d+(?:[.,]\\d+)?");
    if (t == QLatin1String("int")) return S("-?\\d+");
    if (t == QLatin1String("color")) return S("#[0-9A-Fa-f]{3,8}");
    return S("[^|]+?");
}

QString pattern(const QVector<Node>& nodes, const Fields& fields, bool named)
{
    QString out;
    for (const Node& n : nodes) {
        if (n.kind == Node::Text) out += QRegularExpression::escape(n.s);
        else if (n.kind == Node::Ph)
            out += (named ? S("(?<f_") + n.s + QLatin1Char('>') : S("(?:")) + placeholderPattern(fields.value(n.s)) + QLatin1Char(')');
        else out += S("(?:") + pattern(n.kids, fields, named) + S(")??");      // lazy: «запомнит Алиса | флаг=x» is a flag, not the text
    }
    return out;
}

void placeholders(const QVector<Node>& nodes, QStringList* keys)
{
    for (const Node& n : nodes) {
        if (n.kind == Node::Ph) *keys << n.s;
        else if (n.kind == Node::Group) placeholders(n.kids, keys);
    }
}

// canonical spacing, so «a|b», «a  ->b» match the templates' « | » and « -> »
QString canonicalLine(QString s)
{
    s = s.trimmed();
    static const QRegularExpression pipe(QStringLiteral("\\s*\\|\\s*")), arrow(QStringLiteral("\\s*->\\s*")), ws(QStringLiteral("[ \\t]+"));
    s.replace(pipe, QStringLiteral(" | "));
    s.replace(arrow, QStringLiteral(" -> "));
    s.replace(ws, QStringLiteral(" "));
    return s;
}

bool matchTemplate(const QString& text, const QString& templ, const Fields& fields, const QString& variant, QVariantMap* values);

bool parseList(const QString& captured, const QVariantMap& f, QVariantList* out)
{
    const QString sep = f.value(S("sep")).toString();
    const QString itemT = f.value(S("item")).toString();
    const Fields sub = fieldsOf(f.value(S("fields")).toList());
    const int per = int(itemT.count(sep)) + 1;
    const QStringList chunks = sep.isEmpty() ? QStringList{captured} : captured.split(sep);
    if (chunks.size() % per) return false;
    for (int i = 0; i < chunks.size(); i += per) {
        QVariantMap iv;
        if (!matchTemplate(chunks.mid(i, per).join(sep), itemT, sub, QString(), &iv)) return false;
        *out << iv;
    }
    return true;
}

bool matchTemplate(const QString& text, const QString& templ, const Fields& fields, const QString& variant, QVariantMap* values)
{
    const QVector<Node> nodes = parseTemplate(templ);
    const QRegularExpression re(QLatin1Char('^') + pattern(nodes, fields) + QLatin1Char('$'));
    if (!re.isValid()) return false;
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return false;
    QStringList keys;
    placeholders(nodes, &keys);
    for (const QString& k : keys) {
        const QVariantMap f = fields.value(k);
        const QString t = f.value(S("t")).toString();
        const QString cap = m.captured(S("f_") + k);
        const bool got = m.capturedStart(S("f_") + k) >= 0;
        if (t == QLatin1String("bool")) { values->insert(k, got); continue; }
        if (t == QLatin1String("list")) {
            QVariantList items;
            if (got && !parseList(cap.trimmed(), f, &items)) return false;
            values->insert(k, items);
            continue;
        }
        if (!got) {
            values->insert(k, f.value(S("keep")).toBool() ? QString() : defOf(f, variant));
            continue;
        }
        QString v = cap.trimmed();
        if (t == QLatin1String("effect") && v == QLatin1String("none")) v.clear();
        if (f.value(S("nl")).toBool()) v.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
        values->insert(k, v);
    }
    return true;
}

// the template's command word: «фон {bg}» -> «фон», «стопвидеофон[ {fx}]» -> «стопвидеофон», «{who}: {text}» -> ""
QString templateWord(const QString& templ)
{
    int i = 0;
    while (i < templ.size() && templ.at(i) != QLatin1Char(' ') && templ.at(i) != QLatin1Char('[') && templ.at(i) != QLatin1Char('{')) ++i;
    return templ.left(i);
}

}   // namespace

// ================================================================== Forms

QVariantList Forms::builtinOptions(const QString& type)
{
    auto list = [](std::initializer_list<std::pair<const char*, const char*>> items) {
        QVariantList out;
        for (const auto& it : items) out << QVariant(QVariantList{U(it.first), U(it.second)});
        return out;
    };
    if (type == QLatin1String("effect"))
        return list({{"dissolve", "Растворение"}, {"fade", "Через чёрное"}, {"dspr", "Быстрое растворение"}, {"dissolve2", "Медленное растворение"},
                     {"fade2", "Через чёрное, дольше"}, {"fade3", "Через чёрное, долго"}, {"pixellate", "Пиксели"}, {"hpunch", "Удар вбок"},
                     {"vpunch", "Удар вверх-вниз"}, {"moveinleft", "Въезд слева"}, {"moveinright", "Въезд справа"},
                     {"moveoutleft", "Уезд влево"}, {"moveoutright", "Уезд вправо"}, {"move", "Сдвиг"}});
    if (type == QLatin1String("pos"))
        return list({{"fleft", "Край слева"}, {"left", "Слева"}, {"cleft", "Левее центра"}, {"center", "Центр"},
                     {"cright", "Правее центра"}, {"right", "Справа"}, {"fright", "Край справа"}});
    if (type == QLatin1String("zone"))       // ES media.rpy store.map_zones
        return list({{"square", "Площадь"}, {"dining_hall", "Столовая"}, {"beach", "Пляж"}, {"boat_station", "Лодочный причал"},
                     {"sport_area", "Спорткомплекс"}, {"music_club", "Музклуб"}, {"clubs", "Клубы"}, {"library", "Библиотека"},
                     {"medic_house", "Медпункт"}, {"me_mt_house", "Мой домик"}, {"estrade", "Эстрада"},
                     {"camp_entrance", "Ворота в лагерь"}, {"forest", "Лес"}});
    if (type == QLatin1String("chibi"))      // ES store.map_chibi
        return list({{"", "Никого"}, {"sl", "Славя"}, {"dv", "Алиса"}, {"un", "Лена"}, {"us", "Ульяна"}, {"mi", "Мику"},
                     {"mt", "Ольга Дмитриевна"}, {"el", "Электроник"}, {"sh", "Шурик"}, {"mz", "Женя"}, {"uv", "Юля"},
                     {"cs", "Виола"}, {"me", "Семён"}, {"?", "Незнакомец"}});
    if (type == QLatin1String("menubutton"))
        return list({{"Начать", "Начать"}, {"Продолжить", "Продолжить (загрузка)"}, {"Главы", "Главы"}, {"Галерея", "Галерея"},
                     {"Достижения", "Достижения"}, {"Отношения", "Отношения"}, {"Настройки", "Настройки"}, {"Выход", "Выход"}});
    return {};
}

static QVariantList withBuiltins(const QVariantList& fields)
{
    QVariantList out;
    for (const QVariant& v : fields) {
        QVariantMap f = v.toMap();
        const QString t = f.value(S("t")).toString();
        QVariantList opts = Forms::builtinOptions(t);
        if (!opts.isEmpty()) {
            if (f.contains(S("none"))) opts.prepend(QVariant(QVariantList{QString(), f.value(S("none")).toString()}));
            f.insert(S("opts"), opts);
        }
        if (t == QLatin1String("list")) f.insert(S("fields"), withBuiltins(f.value(S("fields")).toList()));
        out << f;
    }
    return out;
}

bool Forms::load(const QString& jsonPath, QString* error)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("нет файла форм: ") + jsonPath;
        return false;
    }
    return loadJson(f.readAll(), error);
}

bool Forms::loadJson(const QByteArray& json, QString* error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull()) {
        if (error) *error = QStringLiteral("forms.json: ") + pe.errorString() + QStringLiteral(" @") + QString::number(pe.offset);
        return false;
    }
    m_forms.clear();
    m_byId.clear();
    for (const QVariant& v : doc.toVariant().toMap().value(S("forms")).toList()) {
        QVariantMap form = v.toMap();
        form.insert(S("fields"), withBuiltins(form.value(S("fields")).toList()));
        m_forms << form;
        m_byId.insert(form.value(S("id")).toString(), form);
    }
    return !m_forms.isEmpty();
}

QVariantMap Forms::defaults(const QString& id, const QVariantMap& preset) const
{
    const QVariantMap form = this->form(id);
    const QString by = form.value(S("by")).toString();
    const QString variant = !by.isEmpty() && preset.contains(by) ? preset.value(by).toString() : defaultVariant(form);
    QVariantMap values;
    for (const QVariant& v : form.value(S("fields")).toList()) {
        const QVariantMap f = v.toMap();
        values.insert(f.value(S("k")).toString(), defaultValue(f, variant));
    }
    if (!by.isEmpty()) values.insert(by, variant);
    for (auto it = preset.begin(); it != preset.end(); ++it) values.insert(it.key(), it.value());
    return values;
}

QString Forms::build(const QString& id, const QVariantMap& values) const
{
    const QVariantMap form = this->form(id);
    if (form.isEmpty()) return {};
    const QString by = form.value(S("by")).toString();
    QString variant = by.isEmpty() ? QString() : values.value(by).toString();
    QString templ = templateOf(form, variant);
    if (templ.isEmpty() && !by.isEmpty()) {
        variant = defaultVariant(form);
        templ = templateOf(form, variant);
    }
    const Fields fields = fieldsOf(form.value(S("fields")).toList());
    const Scope sc{fields, values, variant};
    QStringList lines = render(parseTemplate(templ), sc).split(QLatin1Char('\n'));
    QStringList out;
    for (QString& l : lines) {
        while (!l.isEmpty() && l.back().isSpace()) l.chop(1);
        out << l;
    }
    return out.join(QLatin1Char('\n'));
}

QVariantMap Forms::parse(const QString& lineIn) const
{
    const QString line = canonicalLine(lineIn);
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('@'))) return {};
    const QString lw = firstWord(line);
    const QString lcanon = normalizeCommand(lw);
    const bool lineIsCommand = isCommandName(lcanon);
    for (const QVariant& fv : m_forms) {
        const QVariantMap form = fv.toMap();
        if (form.value(S("block")).toBool() || form.contains(S("special"))) continue;
        const Fields fields = fieldsOf(form.value(S("fields")).toList());
        const QString by = form.value(S("by")).toString();
        for (const QString& variant : variantsOf(form)) {
            const QString templ = templateOf(form, variant);
            if (templ.isEmpty() || templ.contains(QLatin1Char('\n'))) continue;
            const QString tw = templateWord(templ);
            QString rest, restT;
            if (tw.isEmpty()) {                              // «{who}: {text}» - a speaker, never a command
                if (lineIsCommand) continue;
                rest = line;
                restT = templ;
            } else if (tw == QLatin1String(":")) {
                if (!line.startsWith(QLatin1Char(':'))) continue;
                rest = line.mid(1);
                restT = templ.mid(1);
            } else {
                if (normalizeCommand(tw) != lcanon) continue;
                rest = line.mid(lw.size());
                restT = templ.mid(tw.size());
            }
            QVariantMap values;
            if (!matchTemplate(rest, restT, fields, variant, &values)) continue;
            if (!by.isEmpty()) values.insert(by, variant);
            // fields this variant's template doesn't use keep their defaults
            for (auto it = fields.begin(); it != fields.end(); ++it)
                if (!values.contains(it.key())) values.insert(it.key(), defaultValue(it.value(), variant));
            return {{S("id"), form.value(S("id"))}, {S("values"), values}};
        }
    }
    return {};
}

QVariantList Forms::paletteRows() const
{
    QStringList cats;
    QVector<QPair<QString, QVariantMap>> rows;
    auto add = [&](const QString& cat, const QVariantMap& row) {
        if (!cats.contains(cat)) cats << cat;
        rows.push_back({cat, row});
    };
    for (const QVariant& fv : m_forms) {
        const QVariantMap form = fv.toMap();
        const QString id = form.value(S("id")).toString();
        auto row = [&](const QString& title, const QString& help, const QVariantMap& preset) {
            return QVariantMap{{S("id"), id}, {S("key"), id}, {S("category"), form.value(S("cat"))}, {S("title"), title}, {S("help"), help},
                               {S("templ"), build(id, defaults(id, preset))}, {S("preset"), preset},
                               {S("special"), form.value(S("special")).toString()}, {S("hasFields"), !form.value(S("fields")).toList().isEmpty()}};
        };
        QVariantMap r = row(form.value(S("title")).toString(), form.value(S("help")).toString(), {});
        // the variants of a merged command, each findable by its own name / command word
        // («вспышка» -> «Тряска и вспышка · Вспышка»); key = "<form>/<variant>"
        const QString by = form.value(S("by")).toString();
        if (!by.isEmpty()) {
            QVariantList vs;
            for (const QVariant& fv2 : form.value(S("fields")).toList()) {
                const QVariantMap f = fv2.toMap();
                if (f.value(S("k")).toString() != by) continue;
                for (const QVariant& o : f.value(S("opts")).toList()) {
                    const QString v = o.toList().value(0).toString(), label = o.toList().value(1).toString();
                    QVariantMap vr = row(form.value(S("title")).toString() + S(" · ") + label, form.value(S("help")).toString(), {{by, v}});
                    vr.insert(S("key"), id + QLatin1Char('/') + v);
                    vr.insert(S("label"), label);
                    vr.insert(S("short"), label);
                    vr.insert(S("word"), templateWord(templateOf(form, v)));
                    vs << vr;
                }
            }
            r.insert(S("variants"), vs);
        }
        add(form.value(S("cat")).toString(), r);
        for (const QVariant& av : form.value(S("also")).toList()) {
            const QVariantMap a = av.toMap();
            QVariantMap ar = row(a.value(S("title")).toString(), a.value(S("help"), form.value(S("help"))).toString(), a.value(S("preset")).toMap());
            ar.insert(S("category"), a.value(S("cat")));
            ar.insert(S("also"), true);
            QString key = id;
            for (auto it = a.value(S("preset")).toMap().begin(); it != a.value(S("preset")).toMap().end(); ++it) key += QLatin1Char('/') + it.value().toString();
            ar.insert(S("key"), key);
            add(a.value(S("cat")).toString(), ar);
        }
    }
    QVariantList out;
    for (const QString& c : cats)
        for (const auto& r : rows)
            if (r.first == c) out << r.second;
    return out;
}

} // namespace gb
