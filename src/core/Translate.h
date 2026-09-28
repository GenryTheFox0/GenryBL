// GenryBL V1 - «перевод мода»: the mod in 30+ languages.
// What is translated = every Russian string the compiled mod shows (dialogue, choices, notes, cards,
// the phone, popups, the V1 screens' own buttons) + the names of the ES cast it uses. A neural
// network does it: any OpenAI-compatible server (llama-server, LM Studio, Ollama, a cloud proxy).
// Translations live in the project: translations/<code>.json {"lang", "model", "items": {src: dst}}.
// In the game the mod asks the language when it starts (default = the game's own language when the
// mod has it) and swaps its OWN strings only: config.say_menu_text_filter for dialogue and choices,
// config.replace_text for every other text, both only while a statement of the mod's file runs.
// Ren'Py 7.4 (the ES engine) has no text shaping: no Korean, Arabic, Hebrew, Hindi, Thai.
#pragma once
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace gb {

class EsAssets;

struct TrLang {
    QString code, name, native, english;   // "de", "Немецкий", "Deutsch", "German"
    bool cjk = false;                      // needs the game's Chinese font (fonts/STZHONGS.ttf)
};
const QVector<TrLang>& translationLanguages();
const TrLang* translationLanguage(const QString& code);

// the strings to translate, in story order (from the compiled .rpy: Russian literals + the ES cast names spoken)
QStringList translationUnits(const QString& rpy, const EsAssets* es);

QHash<QString, QString> loadTranslation(const QString& dir, const QString& code, QString* model = nullptr);
bool saveTranslation(const QString& dir, const QString& code, const QHash<QString, QString>& items, const QString& model);
QMap<QString, QHash<QString, QString>> loadTranslations(const QString& dir);   // every language the project has

// the neural network (OpenAI-compatible /chat/completions); blocking - worker threads only
struct TrServer {
    QString url = QStringLiteral("http://127.0.0.1:8080/v1");
    QString key;          // "" = none (local)
    QString model;        // "" = the first the server lists
};
QStringList trProbe(const TrServer& s, QString* error, int timeoutMs = 2500);        // the models it serves
// one batch: sources -> translations (missing ones = failed); error = what went wrong with the request
QHash<QString, QString> trBatch(const TrServer& s, const TrLang& lang, const QStringList& sources, QString* error);
bool trKeepsMarkup(const QString& src, const QString& dst);   // {tags} and [vars] survive

// the whole job: every language, batches of ~25 lines, saved after each batch; cancelled() stops it
using TrProgress = std::function<void(const QString& code, int done, int total, const QString& message)>;
void translateAll(const TrServer& s, const QStringList& codes, const QStringList& units, const QString& dir,
                  const TrProgress& progress, const std::function<bool()>& cancelled);

// the runtime the compiled mod gets (only languages with translations)
QStringList translationRuntime(const QString& modId, const QMap<QString, QHash<QString, QString>>& tr);

} // namespace gb
