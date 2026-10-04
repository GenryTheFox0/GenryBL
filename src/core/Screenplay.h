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
// V2.1.2 the story's own decision, the same for the compiler, the preview, the checks and the highlighter: is the line
// a command? («Музыка - смысл моей жизни.» opens with a command word and is a sentence: no.) storyCommandWord = the
// line's first word when it is a command word at all ("" otherwise) - the editor's «Это текст / Это команда»
bool storyCommandLine(const QString& line);
QString storyCommandWord(const QString& line);
// kind of a play line for the highlighter: "heading", "say", "direction", "prose" or "" (not one)
QString screenplayKind(const QString& line);
// the hero's name in a case: 0 им, 1 рд, 2 дт, 3 вн, 4 тв, 5 пр (Семён -> Семёна / Семёну / Семёна / Семёном / Семёне);
// the same rules the mod runs (kV1Hero genry_name_case)
QString declineName(const QString& name, int grammaticalCase, bool she);
// «[имя кому]» -> 2, «[имя]» -> 0, not a case word -> -1
int nameCaseOf(const QString& word);
// the same, with the words before the name: a bare «кого» is родительный after «у / без / для / нет…»
// («у Кати») and винительный after a verb («искала Катю»)
int nameCaseIn(const QString& word, const QString& before);
// a line too long for ES's dialogue box -> the boxes it is shown in, like any novel does: cut after a sentence, else
// after a comma, else at a space; never inside a {tag}…{/tag} or a [substitution] (Ren'Py would fail on the halves)
QStringList splitForBox(const QString& text, int maxVisible = 250);

} // namespace gb
