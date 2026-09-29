#pragma once
// GenryBL V1 - the crash catcher. When GenryBL falls it writes, into <root>/work/crash:
//   GenryBL_<time>.txt  - what fell and where: the stack of the crashing thread AND of every other thread (a race
//                         shows as two threads on the same data), function names from GenryBL.pdb next to the exe,
//                         Qt's own exported names for Qt, then the tail of genrybl.log
//   GenryBL_<time>.dmp  - a minidump for the hard cases
// Covers access violations and the like (SEH), abort()/qFatal/std::terminate and the CRT's invalid-parameter kill.
#include <QString>

namespace crash {
void install(const QString& crashDir, const QString& logPath, const QString& version);
// the newest report in crashDir, "" = none
QString newestReport(const QString& crashDir);
}
