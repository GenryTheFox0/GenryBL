// GenryBL V1 - the status in Discord («Играет в GenryBL · Пишет мод «Лето с последствиями» · сцена «пляж»»): Discord's
// own local pipe (\\.\pipe\discord-ipc-0…9), no SDK. Frames are {uint32 op, uint32 length} little-endian + JSON:
// HANDSHAKE {v:1, client_id} -> READY, then SET_ACTIVITY. Discord allows about one update in 15 s, so the newest one
// waits for its turn and the ones between are dropped. No Discord - it quietly tries again later; a wrong app id -
// it stops until the id changes.
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

namespace gb {

class DiscordPresence : public QObject {
    Q_OBJECT
public:
    explicit DiscordPresence(QObject* parent = nullptr);
    // the app id from discord.com/developers ("" = off); pipe: "" = discord-ipc-0…9 (a test gives its own server's name)
    void setClientId(const QString& id, const QString& pipe = {});
    // the activity to show ({} = nothing): details, state, timestamps, assets, buttons as Discord has them
    void setActivity(const QJsonObject& activity);
    bool isReady() const { return m_ready; }
    QString error() const { return m_error; }
    static constexpr int kMinGapMs = 15000;
    void setMinGap(int ms) { m_gap = ms; }   // a test waits less

signals:
    void readyChanged();
    void sent(const QJsonObject& frame);      // what went to Discord (the tests read it)

private:
    void start();
    void tryPipe();
    void onConnected();
    void onRead();
    void onGone();
    void write(quint32 op, const QJsonObject& body);
    void flush();

    QLocalSocket m_sock;
    QByteArray m_buf;
    QTimer m_retry, m_send;
    QString m_clientId, m_pipeOverride, m_error;
    int m_pipe = 0, m_nonce = 0, m_gap = kMinGapMs, m_backoff = 5000;
    bool m_ready = false, m_dirty = false, m_dead = false;
    qint64 m_lastSent = 0;
    QJsonObject m_activity;
};

} // namespace gb
