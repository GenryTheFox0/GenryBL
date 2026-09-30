// GenryBL V1 - story checks before Everlasting Summer ever starts: the writer sees
// "У Алисы нет эмоции «smile swim»" on line 12 instead of a Ren'Py traceback.
#pragma once
#include "Compiler.h"

#include <QSet>
#include <QString>
#include <QVector>

namespace gb {

class EsAssets;

struct LintIssue {
    enum Level { Info = 0, Warning = 1, Error = 2 };
    int line = 0;
    int level = Warning;
    QString msg;
    QString file;         // a project file the issue is about (line 0)
    int col = -1;         // the words the issue is about, in the line as written (-1: the whole line)
    int len = 0;
    // «Починить» (the doctor): what one click does - 1 the line becomes `fix`, 2 `fix` goes under the line,
    // 3 the line goes away, 4 `fix` goes to the end of the story, 5 `fix` goes above the line
    int fixMode = 0;
    QString fix;
    QString fixLabel;     // what the button shows: «smile pioneer», «переход lake»
};

// the story with every fix of `issues` made (bottom up, one per line); the lint is run again by the caller
QString applyFixes(const QString& text, const QVector<LintIssue>& issues);

struct LintContext {
    const EsAssets* es = nullptr;
    QSet<QString> customImages;     // project images as Ren'Py names
    QSet<QString> customAudio;      // "audio/x.ogg"
    CompileOptions opt;
};

QVector<LintIssue> lintStory(const QString& text, const LintContext& ctx);

} // namespace gb
