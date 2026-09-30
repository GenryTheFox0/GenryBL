#pragma once
// GenryBL in 20 languages. The source language is Russian: every UI string is written in Russian
// (qsTr("…") in QML, tr / QCoreApplication::translate in C++), and data/i18n/<code>.json maps it to the
// language: {"Новый мод": "New mod", …}. A plain JSON per language - easy to fix for anyone who speaks it.
// Missing strings stay Russian (never an empty label).
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVariantList>

namespace i18n {

struct Language {
    const char* code;      // ru, en, pt_BR, zh_CN …
    const char* native;    // its own name: «Deutsch», «日本語»
    const char* english;
};

// the 20 languages, in the order of the chooser
const QList<Language>& languages();

// the language of Windows (its UI language, not the keyboard layout) -> one of ours; English if none fits
QString systemLanguage();

// "auto" / "" -> the system one; an unknown code -> English
QString resolve(const QString& setting);

class JsonTranslator : public QTranslator
{
public:
    bool loadJson(const QString& path);
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return m_map.isEmpty(); }
    int size() const { return int(m_map.size()); }
private:
    QHash<QString, QString> m_map;
};

} // namespace i18n
