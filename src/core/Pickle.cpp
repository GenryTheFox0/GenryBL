#include "Pickle.h"

#include <QHash>
#include <stdexcept>

namespace gb {
namespace {

struct Reader {
    const uchar* p;
    qsizetype n;
    qsizetype pos = 0;

    void need(qsizetype k) const
    {
        if (k < 0 || pos + k > n)
            throw std::runtime_error("pickle truncated");
    }
    quint8 u8() { need(1); return p[pos++]; }
    quint16 u16() { need(2); quint16 v = quint16(p[pos] | (p[pos + 1] << 8)); pos += 2; return v; }
    quint32 u32()
    {
        need(4);
        quint32 v = quint32(p[pos]) | (quint32(p[pos + 1]) << 8) | (quint32(p[pos + 2]) << 16) | (quint32(p[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    quint64 u64()
    {
        const quint64 lo = u32();
        const quint64 hi = u32();
        return lo | (hi << 32);
    }
    QByteArray take(qsizetype k)
    {
        need(k);
        QByteArray b(reinterpret_cast<const char*>(p + pos), k);
        pos += k;
        return b;
    }
    QByteArray line()
    {
        qsizetype e = pos;
        while (e < n && p[e] != '\n') ++e;
        if (e >= n) throw std::runtime_error("pickle: unterminated text opcode");
        QByteArray b(reinterpret_cast<const char*>(p + pos), e - pos);
        pos = e + 1;
        return b;
    }
};

PyRef make(PyObj::Kind k)
{
    auto o = std::make_shared<PyObj>();
    o->kind = k;
    return o;
}

PyRef makeInt(qint64 v)
{
    auto o = make(PyObj::Int);
    o->i = v;
    return o;
}

qint64 littleSigned(const QByteArray& b)
{
    if (b.isEmpty()) return 0;
    if (b.size() > 8) throw std::runtime_error("pickle: integer too large");
    quint64 v = 0;
    for (int k = int(b.size()) - 1; k >= 0; --k)
        v = (v << 8) | quint8(b[k]);
    if (b.size() < 8 && (quint8(b.back()) & 0x80))
        v |= ~quint64(0) << (8 * b.size());   // sign extend
    return qint64(v);
}

} // namespace

PyRef unpickle(const QByteArray& data, QString* error)
{
    Reader r{reinterpret_cast<const uchar*>(data.constData()), data.size()};
    QVector<PyRef> stack;
    QVector<int> marks;
    QHash<qint64, PyRef> memo;

    auto pop = [&]() -> PyRef {
        if (stack.isEmpty()) throw std::runtime_error("pickle: stack underflow");
        return stack.takeLast();
    };
    auto top = [&]() -> PyRef {
        if (stack.isEmpty()) throw std::runtime_error("pickle: empty stack");
        return stack.last();
    };
    auto popMark = [&]() -> QVector<PyRef> {
        if (marks.isEmpty()) throw std::runtime_error("pickle: missing MARK");
        const int m = marks.takeLast();
        QVector<PyRef> v = stack.mid(m);
        stack.resize(m);
        return v;
    };
    auto pushStr = [&](PyObj::Kind k, const QByteArray& b) {
        auto o = make(k);
        o->bytes = b;
        stack.push_back(o);
    };

    try {
        for (;;) {
            const quint8 op = r.u8();
            switch (op) {
            case 0x80: r.u8(); break;                         // PROTO
            case 0x95: r.u64(); break;                        // FRAME
            case '}': stack.push_back(make(PyObj::Dict)); break;
            case ']': stack.push_back(make(PyObj::List)); break;
            case ')': stack.push_back(make(PyObj::Tuple)); break;
            case '(': marks.push_back(int(stack.size())); break;
            case 'q': memo[r.u8()] = top(); break;            // BINPUT
            case 'r': memo[r.u32()] = top(); break;           // LONG_BINPUT
            case 0x94: memo[memo.size()] = top(); break;      // MEMOIZE
            case 'p': memo[r.line().toLongLong()] = top(); break;
            case 'h': stack.push_back(memo.value(r.u8())); break;
            case 'j': stack.push_back(memo.value(r.u32())); break;
            case 'g': stack.push_back(memo.value(r.line().toLongLong())); break;
            case 'X': { const quint32 k = r.u32(); pushStr(PyObj::Str, r.take(k)); break; }
            case 0x8c: { const quint8 k = r.u8(); pushStr(PyObj::Str, r.take(k)); break; }
            case 0x8d: { const quint64 k = r.u64(); pushStr(PyObj::Str, r.take(qsizetype(k))); break; }
            case 'T': { const quint32 k = r.u32(); pushStr(PyObj::Bytes, r.take(k)); break; }
            case 'U': { const quint8 k = r.u8(); pushStr(PyObj::Bytes, r.take(k)); break; }
            case 'B': { const quint32 k = r.u32(); pushStr(PyObj::Bytes, r.take(k)); break; }
            case 'C': { const quint8 k = r.u8(); pushStr(PyObj::Bytes, r.take(k)); break; }
            case 0x8e: { const quint64 k = r.u64(); pushStr(PyObj::Bytes, r.take(qsizetype(k))); break; }
            case 'V': pushStr(PyObj::Str, r.line()); break;   // raw-unicode (ascii in practice)
            case 'S': {                                       // STRING 'repr'
                QByteArray b = r.line();
                if (b.size() >= 2 && (b.front() == '\'' || b.front() == '"')) b = b.mid(1, b.size() - 2);
                pushStr(PyObj::Bytes, b);
                break;
            }
            case 'J': stack.push_back(makeInt(qint32(r.u32()))); break;
            case 'K': stack.push_back(makeInt(r.u8())); break;
            case 'M': stack.push_back(makeInt(r.u16())); break;
            case 0x8a: { const quint8 k = r.u8(); stack.push_back(makeInt(littleSigned(r.take(k)))); break; }
            case 0x8b: { const quint32 k = r.u32(); stack.push_back(makeInt(littleSigned(r.take(k)))); break; }
            case 'I': {
                QByteArray b = r.line();
                if (b == "00" || b == "01") { auto o = make(PyObj::Bool); o->i = (b == "01"); stack.push_back(o); }
                else stack.push_back(makeInt(b.toLongLong()));
                break;
            }
            case 'L': { QByteArray b = r.line(); if (b.endsWith('L')) b.chop(1); stack.push_back(makeInt(b.toLongLong())); break; }
            case 'N': stack.push_back(make(PyObj::None)); break;
            case 0x88: { auto o = make(PyObj::Bool); o->i = 1; stack.push_back(o); break; }
            case 0x89: stack.push_back(make(PyObj::Bool)); break;
            case 'a': { PyRef v = pop(); top()->items.push_back(v); break; }
            case 'e': { const QVector<PyRef> v = popMark(); top()->items += v; break; }
            case 's': { PyRef v = pop(); PyRef k = pop(); top()->dict.push_back({k, v}); break; }
            case 'u': {
                const QVector<PyRef> v = popMark();
                if (v.size() % 2) throw std::runtime_error("pickle: odd SETITEMS");
                PyRef d = top();
                for (int k = 0; k + 1 < v.size(); k += 2) d->dict.push_back({v[k], v[k + 1]});
                break;
            }
            case 't': { auto t = make(PyObj::Tuple); t->items = popMark(); stack.push_back(t); break; }
            case 0x85: { auto t = make(PyObj::Tuple); t->items = {pop()}; stack.push_back(t); break; }
            case 0x86: { auto t = make(PyObj::Tuple); PyRef b = pop(); PyRef a = pop(); t->items = {a, b}; stack.push_back(t); break; }
            case 0x87: { auto t = make(PyObj::Tuple); PyRef c = pop(); PyRef b = pop(); PyRef a = pop(); t->items = {a, b, c}; stack.push_back(t); break; }
            case 'l': { auto l = make(PyObj::List); l->items = popMark(); stack.push_back(l); break; }
            case 'd': {
                auto d = make(PyObj::Dict);
                const QVector<PyRef> v = popMark();
                for (int k = 0; k + 1 < v.size(); k += 2) d->dict.push_back({v[k], v[k + 1]});
                stack.push_back(d);
                break;
            }
            case '0': pop(); break;
            case '1': popMark(); break;
            case '2': stack.push_back(top()); break;
            case '.': return top();                           // STOP
            default:
                throw std::runtime_error(QStringLiteral("pickle: unsupported opcode 0x%1")
                                             .arg(op, 2, 16, QLatin1Char('0')).toStdString());
            }
        }
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return nullptr;
    }
}

} // namespace gb
