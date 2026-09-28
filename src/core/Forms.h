// GenryBL V1 - command forms (data/forms.json). Every palette command is a form: fields with
// pickers, a build template ("фон {bg}[ {fx}]") and optional variants ("Персонаж": обычно,
// зеркально, крупно, ...). build() turns the values into one story line (or a block);
// parse() goes back: an existing story line -> its form and values, so any line can be
// reopened and fine-tuned.
#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace gb {

class Forms {
public:
    bool load(const QString& jsonPath, QString* error);
    bool loadJson(const QByteArray& json, QString* error);
    bool ready() const { return !m_forms.isEmpty(); }

    // the forms in palette order, fields' built-in option lists filled in (effect, pos, zone, chibi)
    QVariantList forms() const { return m_forms; }
    // palette rows: every form once + its «also» rows ({id, category, title, help, templ, preset, special})
    QVariantList paletteRows() const;
    QVariantMap form(const QString& id) const { return m_byId.value(id).toMap(); }

    // default values of a form for a variant ("" = the form's default variant), preset applied on top
    QVariantMap defaults(const QString& id, const QVariantMap& preset = {}) const;
    QString build(const QString& id, const QVariantMap& values) const;
    // {id, values} of the first form one of whose templates matches the line; {} if none
    QVariantMap parse(const QString& line) const;

    static QVariantList builtinOptions(const QString& type);    // [[value, label], ...]

private:
    QVariantList m_forms;
    QHash<QString, QVariant> m_byId;
};

} // namespace gb
