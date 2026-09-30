#include "I18n.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace i18n {

const QList<Language>& languages()
{
    // where visual novels are loved: the CIS (Everlasting Summer's home), Europe and the Americas, Asia
    static const QList<Language> list = {
        { "ru",    "Русский",          "Russian" },
        { "en",    "English",          "English" },
        { "uk",    "Українська",       "Ukrainian" },
        { "be",    "Беларуская",       "Belarusian" },
        { "kk",    "Қазақша",          "Kazakh" },
        { "pl",    "Polski",           "Polish" },
        { "cs",    "Čeština",          "Czech" },
        { "de",    "Deutsch",          "German" },
        { "fr",    "Français",         "French" },
        { "es",    "Español",          "Spanish" },
        { "pt_BR", "Português (Brasil)", "Portuguese (Brazil)" },
        { "it",    "Italiano",         "Italian" },
        { "tr",    "Türkçe",           "Turkish" },
        { "ja",    "日本語",            "Japanese" },
        { "zh_CN", "简体中文",          "Chinese (Simplified)" },
        { "zh_TW", "繁體中文",          "Chinese (Traditional)" },
        { "ko",    "한국어",            "Korean" },
        { "vi",    "Tiếng Việt",       "Vietnamese" },
        { "id",    "Bahasa Indonesia", "Indonesian" },
        { "th",    "ไทย",              "Thai" },
    };
    return list;
}

static bool known(const QString& code)
{
    for (const Language& l : languages()) if (code == QLatin1String(l.code)) return true;
    return false;
}

QString systemLanguage()
{
    // uiLanguages() = the display language(s) of Windows, best first ("de-DE", "zh-Hant-TW", "pt-BR" …)
    for (QString tag : QLocale::system().uiLanguages()) {
        tag.replace(QLatin1Char('-'), QLatin1Char('_'));
        const QString lang = tag.section(QLatin1Char('_'), 0, 0).toLower();
        if (lang == QLatin1String("zh")) {
            const bool trad = tag.contains(QLatin1String("Hant"), Qt::CaseInsensitive) || tag.endsWith(QLatin1String("_TW"))
                           || tag.endsWith(QLatin1String("_HK")) || tag.endsWith(QLatin1String("_MO"));
            return trad ? QStringLiteral("zh_TW") : QStringLiteral("zh_CN");
        }
        if (lang == QLatin1String("pt")) return QStringLiteral("pt_BR");
        if (lang == QLatin1String("nb") || lang == QLatin1String("no")) continue;
        if (known(lang)) return lang;
    }
    return QStringLiteral("en");
}

QString resolve(const QString& setting)
{
    if (setting.isEmpty() || setting == QLatin1String("auto")) return systemLanguage();
    return known(setting) ? setting : QStringLiteral("en");
}

bool JsonTranslator::loadJson(const QString& path)
{
    m_map.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString t = it.value().toString();
        if (!t.isEmpty() && !it.key().startsWith(QLatin1Char('@'))) m_map.insert(it.key(), t);   // "@meta": notes, not strings
    }
    return !m_map.isEmpty();
}

QString JsonTranslator::translate(const char*, const char* sourceText, const char*, int n) const
{
    if (!sourceText) return {};
    const auto it = m_map.constFind(QString::fromUtf8(sourceText));
    if (it == m_map.cend()) return {};                 // empty = not translated: Qt shows the Russian source
    QString t = *it;
    if (n >= 0) t.replace(QLatin1String("%n"), QString::number(n));
    return t;
}

} // namespace i18n
