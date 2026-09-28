// GenryBL V1 - minimal Python unpickler (protocols 0-5 subset).
// Enough for Ren'Py .rpa indexes: dict[str -> list[tuple(int, int[, bytes])]]
// written by Ren'Py 6.99 (Python 2, protocol 2) or by modern tools (protocol 4).
#pragma once
#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>
#include <memory>

namespace gb {

struct PyObj;
using PyRef = std::shared_ptr<PyObj>;

struct PyObj {
    enum Kind { None, Bool, Int, Bytes, Str, List, Tuple, Dict };
    Kind kind = None;
    qint64 i = 0;                      // Int / Bool
    QByteArray bytes;                  // Bytes (raw) or Str (UTF-8)
    QVector<PyRef> items;              // List / Tuple
    QVector<QPair<PyRef, PyRef>> dict; // Dict, insertion order

    QString text() const { return QString::fromUtf8(bytes); }
};

// Returns nullptr and fills *error on malformed/unsupported input.
PyRef unpickle(const QByteArray& data, QString* error);

} // namespace gb
