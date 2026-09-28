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
};

struct LintContext {
    const EsAssets* es = nullptr;
    QSet<QString> customImages;     // project images as Ren'Py names
    QSet<QString> customAudio;      // "audio/x.ogg"
    CompileOptions opt;
};

QVector<LintIssue> lintStory(const QString& text, const LintContext& ctx);

} // namespace gb
