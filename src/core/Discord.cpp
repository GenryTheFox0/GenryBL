#include "Discord.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QtEndian>

namespace gb {

DiscordPresence::DiscordPresence(QObject* parent)
    : QObject(parent)
{
    m_retry.setSingleShot(true);
    m_send.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { m_pipe = 0; tryPipe(); });
    connect(&m_send, &QTimer::timeout, this, &DiscordPresence::flush);
    connect(&m_sock, &QLocalSocket::connected, this, &DiscordPresence::onConnected);
    connect(&m_sock, &QLocalSocket::readyRead, this, &DiscordPresence::onRead);
    connect(&m_sock, &QLocalSocket::disconnected, this, &DiscordPresence::onGone);
    // on Windows the socket connects inside connectToServer: the next pipe is tried from the event loop, not from here
    connect(&m_sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError e) {
        if (m_sock.state() == QLocalSocket::ConnectedState) return;
        if (e == QLocalSocket::ServerNotFoundError && m_pipeOverride.isEmpty() && m_pipe < 9) {
            ++m_pipe;
            QTimer::singleShot(0, this, &DiscordPresence::tryPipe);
            return;
        }
        m_retry.start(m_backoff);                   // Discord is not running (yet): later
        m_backoff = qMin(60000, m_backoff * 2);
    });
}

void DiscordPresence::setClientId(const QString& id, const QString& pipe)
{
    if (id == m_clientId && pipe == m_pipeOverride) return;
    m_clientId = id;
    m_pipeOverride = pipe;
    m_dead = false;
    m_error.clear();
    m_retry.stop();
    m_send.stop();
    m_buf.clear();
    const bool was = m_ready;
    m_ready = false;
    m_sock.abort();
    if (was) emit readyChanged();
    m_backoff = 5000;
    m_dirty = !m_activity.isEmpty();
    if (!m_clientId.isEmpty()) start();
}

void DiscordPresence::setActivity(const QJsonObject& activity)
{
    if (activity == m_activity) return;
    m_activity = activity;
    m_dirty = true;
    flush();
}

void DiscordPresence::start()
{
    m_pipe = 0;
    tryPipe();
}

void DiscordPresence::tryPipe()
{
    if (m_clientId.isEmpty() || m_dead || m_sock.state() != QLocalSocket::UnconnectedState) return;
    m_buf.clear();
    m_sock.connectToServer(m_pipeOverride.isEmpty() ? QStringLiteral("discord-ipc-%1").arg(m_pipe) : m_pipeOverride);
}

void DiscordPresence::onConnected()
{
    write(0, {{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), m_clientId}});
}

void DiscordPresence::write(quint32 op, const QJsonObject& body)
{
    const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QByteArray frame(8, '\0');
    qToLittleEndian<quint32>(op, frame.data());
    qToLittleEndian<quint32>(quint32(json.size()), frame.data() + 4);
    m_sock.write(frame + json);                     // one write per frame: split ones break the pipe
    m_sock.flush();
}

void DiscordPresence::onRead()
{
    m_buf += m_sock.readAll();
    while (m_buf.size() >= 8) {
        const quint32 op = qFromLittleEndian<quint32>(m_buf.constData());
        const quint32 len = qFromLittleEndian<quint32>(m_buf.constData() + 4);
        if (len > 1 << 20) { m_sock.abort(); return; }
        if (quint32(m_buf.size()) < 8 + len) return;
        const QJsonObject o = QJsonDocument::fromJson(m_buf.mid(8, int(len))).object();
        m_buf.remove(0, int(8 + len));
        if (op == 1) {
            const QString evt = o.value(QStringLiteral("evt")).toString();
            if (o.value(QStringLiteral("cmd")).toString() == QLatin1String("DISPATCH") && evt == QLatin1String("READY")) {
                m_ready = true;
                m_backoff = 5000;
                m_error.clear();
                emit readyChanged();
                m_dirty = true;
                m_lastSent = 0;
                flush();
            } else if (evt == QLatin1String("ERROR")) {
                m_error = o.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString();
            }
        } else if (op == 2) {                       // CLOSE: {code, message}; 4000 = a wrong app id - no retries with it
            m_error = o.value(QStringLiteral("message")).toString();
            if (o.value(QStringLiteral("code")).toInt() == 4000) m_dead = true;
            m_sock.disconnectFromServer();
            return;
        } else if (op == 3) {
            write(4, o);                            // PING -> PONG with the same words
        }
    }
}

void DiscordPresence::onGone()
{
    const bool was = m_ready;
    m_ready = false;
    m_send.stop();
    if (was) emit readyChanged();
    if (m_clientId.isEmpty() || m_dead) return;
    m_retry.start(m_backoff);
    m_backoff = qMin(60000, m_backoff * 2);
}

void DiscordPresence::flush()
{
    if (!m_ready || !m_dirty) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 wait = m_lastSent ? m_lastSent + m_gap - now : 0;
    if (wait > 0) {
        if (!m_send.isActive()) m_send.start(int(wait));
        return;
    }
    QJsonObject args{{QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())}};
    if (!m_activity.isEmpty()) args.insert(QStringLiteral("activity"), m_activity);   // no «activity»: Discord clears it
    const QJsonObject frame{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")}, {QStringLiteral("args"), args},
                            {QStringLiteral("nonce"), QString::number(++m_nonce)}};
    write(1, frame);
    m_dirty = false;
    m_lastSent = now;
    emit sent(frame);
}

} // namespace gb
