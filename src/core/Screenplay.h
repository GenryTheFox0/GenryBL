// GenryBL V1 - «пиши как сценарий». A story may be written like a play:
//     НАТ. ПЛЯЖ — ДЕНЬ                      -> время день + фон ext_beach_day fade
//     Алиса (злая, слева): Ну и чего ты встал? -> показать dv angry pioneer left dissolve + Алиса: ...
//     Славя (уходит направо)                 -> убрать sl moveoutright
//     — Привет, — улыбнулась Славя.          -> Славя (улыбнулась): Привет.
//     Солнце садилось за лес.                -> текст Солнце садилось за лес.
// The expander turns such lines into ordinary commands right before the compiler and the preview
// read the story; every other line passes through untouched. It keeps track of who stands where and
// in what look, so emotions stick, the auto layout re-slots the cast and a missing ES sprite falls
// back to the nearest one the game really has.
#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct ScreenplayNote {
    int line = -1;        // index in the input lines
    bool warn = false;    // false = a quiet info («нет dress у mi - оставил pioneer»)
    QString text;
};

// srcOf[i] = the input line out[i] came from
// «фон» and the time of day: "ext_beach_day" -> "day" ("" = a timeless place); the same place at another time
// ("ext_beach_night") or "" when the game has no such picture
QString bgTimeOf(const QString& bg);
QString bgAtTime(const QString& bg, const QString& time);
QStringList expandScreenplay(const QStringList& lines, QVector<int>* srcOf = nullptr, QVector<ScreenplayNote>* notes = nullptr);
// the commands a single line becomes, with the stage set up by the lines before it ({} = not a play line)
QStringList expandScreenplayLine(const QStringList& lines, int index, QVector<ScreenplayNote>* notes = nullptr);
// Pasted text - a screenplay (cues in capitals), a chat log, a prose chapter with «— реплика, — сказала
// Алиса» - to story lines in the play form (or fully expanded commands).
QStringList convertToStory(const QString& text, bool expand = false, QVector<ScreenplayNote>* notes = nullptr);
// kind of a play line for the highlighter: "heading", "say", "direction", "prose" or "" (not one)
QString screenplayKind(const QString& line);

} // namespace gb
