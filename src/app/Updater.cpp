// GenryBL V1 - «Обновить». The newest GenryBL is either the latest GitHub release or the one inside the Steam
// Workshop item (Steam downloads a new version of a subscribed item by itself - then nothing is downloaded here).
// Its installer runs over this very folder: GenryBL_Setup.exe --silent-to <root> --no-shortcuts --update --relaunch.
// It waits until this program has closed, copies the new files (projects/ and work/ are never touched) and starts
// the new GenryBL. The developer build (built from the sources) never updates itself.
#include "Engine.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }
const char* const kLatest = "https://api.github.com/repos/GenryTheFox0/GenryBL/releases/latest";
const char* const kReleasePage = "https://github.com/GenryTheFox0/GenryBL/releases/latest";
const char* const kWorkshopItem = "3809725763";

QList<int> versionParts(const QString& v)
{
    QList<int> out;
    static const QRegularExpression num(QStringLiteral("\\d+"));
    for (auto it = num.globalMatch(v); it.hasNext();) out << it.next().captured().toInt();
    while (out.size() < 4) out << 0;
    return out;
}

bool newer(const QString& a, const QString& b)          // a is a later version than b
{
    const QList<int> x = versionParts(a), y = versionParts(b);
    for (int i = 0; i < qMin(x.size(), y.size()); ++i)
        if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

QString pretty(const QString& v)
{
    QString t = v.trimmed();
    if (t.startsWith(QLatin1Char('v'))) t[0] = QLatin1Char('V');
    if (!t.startsWith(QLatin1Char('V'))) t.prepend(QLatin1Char('V'));
    return t;
}

// <library>/steamapps/common/Everlasting Summer -> <library>/steamapps/workshop/content/331470/<GenryBL item>
QString workshopSetup(const QString& esRoot, QString* version)
{
    if (esRoot.isEmpty()) return {};
    const QString dir = QDir::cleanPath(esRoot + QStringLiteral("/../../workshop/content/331470/") + QLatin1String(kWorkshopItem) +
                                        QStringLiteral("/mods/genrybl_workshop"));
    const QString exe = dir + QStringLiteral("/GenryBL_Setup.exe");
    QFile f(dir + QStringLiteral("/GenryBL_version.txt"));
    if (!QFileInfo::exists(exe) || !f.open(QIODevice::ReadOnly)) return {};
    *version = QString::fromUtf8(f.readAll()).trimmed();
    return version->isEmpty() ? QString() : exe;
}

// the version to compare with: this build's own; GENRYBL_UPDATE_AS lets the self-check play an older one
QString mine()
{
    return qEnvironmentVariableIsEmpty("GENRYBL_UPDATE_AS") ? Engine::instance()->version() : qEnvironmentVariable("GENRYBL_UPDATE_AS");
}

QNetworkRequest request(const QString& url, const QString& version)
{
    QNetworkRequest rq{QUrl(url)};
    rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GenryBL/") + version);
    rq.setRawHeader("Accept", "application/vnd.github+json");
    rq.setTransferTimeout(20000);
    return rq;
}

}   // namespace

void Engine::setUpdate(const QString& state, const QString& text, double progress)
{
    m_update.insert(QStringLiteral("state"), state);
    m_update.insert(QStringLiteral("text"), text);
    m_update.insert(QStringLiteral("progress"), progress);
    if (state != QLatin1String("downloading")) qInfo().noquote() << "update:" << state << text;     // work/genrybl.log
    emit updateChanged();
}

void Engine::checkUpdates(bool manual)
{
    // «GenryBL.exe --update-now»: check and install without a click (a shortcut / the release self-check)
    const bool now = QCoreApplication::arguments().contains(QStringLiteral("--update-now"));
    manual = manual || now;
    const QString st = m_update.value(QStringLiteral("state")).toString();
    if (st == QLatin1String("checking") || st == QLatin1String("downloading") || st == QLatin1String("starting")) return;
    if (!isRelease()) {
        if (manual) setUpdate(QStringLiteral("dev"), gbTr("Это сборка разработчика — она собирается из исходников, обновлять её нечем"));
        return;
    }
    static bool autoDone = false;                     // the quiet check on the menu: once a session
    if (!manual) {
        if (autoDone) return;
        autoDone = true;
    }
    m_updLocal.clear();
    m_updUrl.clear();
    m_updSums.clear();
    m_updSize = 0;
    m_update.remove(QStringLiteral("version"));
    m_update.remove(QStringLiteral("source"));
    QString steamVer;
    const QString steamExe = workshopSetup(esRoot(), &steamVer);
    setUpdate(QStringLiteral("checking"), gbTr("Проверяю обновления…"));
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // GENRYBL_UPDATE_API: another «releases/latest» (the updater's own test serves one locally)
    const QString api = qEnvironmentVariableIsEmpty("GENRYBL_UPDATE_API") ? QString::fromLatin1(kLatest) : qEnvironmentVariable("GENRYBL_UPDATE_API");
    // what to do with what GitHub said (or failed to say)
    auto decide = [this, manual, now, steamExe, steamVer](const QString& ghVer, const QString& url, const QString& sums, qint64 size,
                                                          const QString& netError) {
        const bool steamNew = !steamExe.isEmpty() && newer(steamVer, mine());
        const bool ghNew = !url.isEmpty() && newer(ghVer, mine());
        if (steamNew && (!ghNew || !newer(ghVer, steamVer))) {
            // Steam has already brought it: nothing to download
            m_updLocal = steamExe;
            m_update.insert(QStringLiteral("version"), pretty(steamVer));
            m_update.insert(QStringLiteral("source"), QStringLiteral("steam"));
            setUpdate(QStringLiteral("available"), gbTr("Вышла %1 — Steam уже скачал её из Мастерской").arg(pretty(steamVer)));
        } else if (ghNew) {
            m_updUrl = url;
            m_updSums = sums;
            m_updSize = size;
            m_update.insert(QStringLiteral("version"), pretty(ghVer));
            m_update.insert(QStringLiteral("source"), QStringLiteral("github"));
            setUpdate(QStringLiteral("available"), size > 0 ? gbTr("Вышла %1 на GitHub, %2 МБ").arg(pretty(ghVer)).arg(qMax<qint64>(1, size / (1024 * 1024)))
                                                            : gbTr("Вышла %1 на GitHub").arg(pretty(ghVer)));
        } else if (!netError.isEmpty()) {
            if (manual) setUpdate(QStringLiteral("error"), gbTr("Не достучался до GitHub (%1). Проверь интернет и нажми «Проверить» ещё раз").arg(netError));
            else setUpdate(QString(), QString());
        } else {
            setUpdate(manual ? QStringLiteral("latest") : QString(), manual ? gbTr("У тебя последняя версия — %1").arg(mine()) : QString());
        }
        if (now && m_update.value(QStringLiteral("state")).toString() == QLatin1String("available")) startUpdate();
    };
    // the plan B: GitHub's API lets ~60 checks an hour per address (a café, a dorm, a day of testing ran it dry:
    // «server replied:» with nothing). The page «releases/latest» has no such limit and redirects to
    // «…/releases/tag/<version>» - the version is in that address, the files are at «…/releases/download/<version>/…»
    auto planB = [this, decide](const QString& apiError) {
        QNetworkRequest rq{QUrl(QString::fromLatin1(kReleasePage))};
        rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GenryBL/") + version());
        rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        rq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        rq.setTransferTimeout(20000);
        QNetworkReply* p = m_net->get(rq);
        connect(p, &QNetworkReply::finished, this, [p, decide, apiError] {
            p->deleteLater();
            QString target = p->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().toString();
            if (target.isEmpty()) target = QString::fromUtf8(p->rawHeader("Location"));
            static const QRegularExpression tagRe(QStringLiteral("/releases/tag/([^/?#]+)"));
            const auto m = tagRe.match(target);
            if (!m.hasMatch()) {
                decide(QString(), QString(), QString(), 0, p->error() != QNetworkReply::NoError ? p->errorString() : apiError);
                return;
            }
            const QString tag = m.captured(1);
            const QString base = QString::fromLatin1(kReleasePage).section(QStringLiteral("/releases/"), 0, 0) + QStringLiteral("/releases/download/") + tag;
            decide(tag, base + QStringLiteral("/GenryBL_Setup.exe"), base + QStringLiteral("/SHA256SUMS.txt"), 0, QString());
        });
    };
    QNetworkReply* r = m_net->get(request(api, version()));
    const bool testApi = !qEnvironmentVariableIsEmpty("GENRYBL_UPDATE_API");
    connect(r, &QNetworkReply::finished, this, [r, decide, planB, testApi] {
        r->deleteLater();
        QString ghVer, url, sums;
        qint64 size = 0;
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            ghVer = o.value(QStringLiteral("tag_name")).toString();
            for (const QJsonValue& a : o.value(QStringLiteral("assets")).toArray()) {
                const QJsonObject ao = a.toObject();
                const QString n = ao.value(QStringLiteral("name")).toString();
                if (n == QLatin1String("GenryBL_Setup.exe")) {
                    url = ao.value(QStringLiteral("browser_download_url")).toString();
                    size = ao.value(QStringLiteral("size")).toInteger();
                } else if (n == QLatin1String("SHA256SUMS.txt")) {
                    sums = ao.value(QStringLiteral("browser_download_url")).toString();
                }
            }
        }
        if (!ghVer.isEmpty() && !url.isEmpty()) { decide(ghVer, url, sums, size, QString()); return; }
        const QString why = code ? QStringLiteral("HTTP %1").arg(code) : r->errorString();
        if (testApi) { decide(QString(), QString(), QString(), 0, why); return; }      // the self-check's own server
        planB(why);
    });
}

void Engine::startUpdate()
{
    if (m_update.value(QStringLiteral("state")).toString() != QLatin1String("available")) return;
    const QString ver = m_update.value(QStringLiteral("version")).toString();
    const QString tmp = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/GenryBL_update"));
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    const QString setup = tmp + QStringLiteral("/GenryBL_Setup.exe");
    if (!m_updLocal.isEmpty()) {
        // a copy: Steam may refresh the Workshop folder while the installer runs
        if (!QFile::copy(m_updLocal, setup)) {
            setUpdate(QStringLiteral("error"), gbTr("Не смог взять установщик из папки Мастерской"));
            return;
        }
        runUpdater(setup);
        return;
    }
    // GitHub: 110 MB over whatever line the player has. V1.0.2-1.0.3 put a 60 s cap on the whole transfer and let
    // HTTP/2 run it - a slow line was cut at a minute («Operation canceled»), GitHub's HTTP/2 now and then dropped
    // the stream («Server stopped accepting new streams»). Now: HTTP/1.1, a watchdog that only fires after 30 s
    // WITHOUT a byte, and a broken download goes on from where it stopped (Range), up to 6 times.
    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_dlPath = setup;
    m_dlUrl = m_updUrl;
    m_dlTries = 0;
    QFile::remove(setup);
    setUpdate(QStringLiteral("downloading"), gbTr("Качаю %1…").arg(ver), 0);
    downloadChunk();
}

void Engine::downloadChunk()
{
    const QString ver = m_update.value(QStringLiteral("version")).toString();
    auto* file = new QFile(m_dlPath, this);
    if (!file->open(QIODevice::Append)) {
        file->deleteLater();
        setUpdate(QStringLiteral("error"), gbTr("Не могу записать во временную папку: %1").arg(QDir::toNativeSeparators(QFileInfo(m_dlPath).absolutePath())));
        return;
    }
    const qint64 have = file->size();
    QNetworkRequest rq{QUrl(m_dlUrl)};
    rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GenryBL/") + version());
    rq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (have > 0) rq.setRawHeader("Range", "bytes=" + QByteArray::number(have) + "-");
    QNetworkReply* r = m_net->get(rq);
    auto* watch = new QTimer(r);                // 30 s without a single byte = the line is dead: cut it, go on again
    watch->setSingleShot(true);
    watch->setInterval(30000);
    connect(watch, &QTimer::timeout, r, &QNetworkReply::abort);
    watch->start();
    auto first = std::make_shared<bool>(true);
    auto write = [this, r, file, watch, have, first, ver] {
        if (*first) {
            *first = false;
            // asked from the middle, got the whole file (200): the server ignores Range - start the file over
            if (have > 0 && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) file->resize(0);
            if (m_updSize <= 0) {           // the plan B knew no size: the answer tells it
                const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QString range = QString::fromLatin1(r->rawHeader("Content-Range"));      // «bytes 100-199/5000»
                if (code == 206 && range.contains(QLatin1Char('/'))) m_updSize = range.section(QLatin1Char('/'), 1).toLongLong();
                else if (code == 200) m_updSize = r->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            }
        }
        const QByteArray chunk = r->readAll();
        if (chunk.isEmpty()) return;
        file->write(chunk);
        watch->start();
        const qint64 got = file->size();
        setUpdate(QStringLiteral("downloading"),
                  gbTr("Качаю %1: %2 из %3 МБ").arg(ver).arg(got / (1024 * 1024)).arg(qMax<qint64>(1, m_updSize / (1024 * 1024))) +
                      (m_dlTries ? gbTr(" (докачиваю после обрыва)") : QString()),
                  m_updSize > 0 ? double(got) / double(m_updSize) : 0);
    };
    connect(r, &QNetworkReply::readyRead, this, write);
    connect(r, &QNetworkReply::finished, this, [this, r, file, write] {
        write();
        r->deleteLater();
        file->close();
        file->deleteLater();
        if (r->url().isValid() && r->url().scheme().startsWith(QLatin1String("http"))) m_dlUrl = r->url().toString();   // after the redirects
        const qint64 got = QFileInfo(m_dlPath).size();
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool whole = m_updSize > 0 ? got == m_updSize : r->error() == QNetworkReply::NoError;
        if (whole && (r->error() == QNetworkReply::NoError || code == 416)) { downloadDone(); return; }
        if (m_updSize > 0 && got > m_updSize) QFile::remove(m_dlPath);     // something went wrong in the middle: anew
        if (code == 403 || code == 404 || code == 410) m_dlUrl = m_updUrl;  // the signed CDN address lives an hour: ask GitHub again
        if (++m_dlTries <= 6) {
            setUpdate(QStringLiteral("downloading"), gbTr("Связь оборвалась (%1) — докачиваю, попытка %2 из 6…").arg(r->errorString()).arg(m_dlTries),
                      m_updSize > 0 ? double(qMin(got, m_updSize)) / double(m_updSize) : 0);
            QTimer::singleShot(2500, this, [this] { downloadChunk(); });
            return;
        }
        setUpdate(QStringLiteral("error"), gbTr("Скачать не вышло: %1. Проверь интернет и нажми ещё раз").arg(r->errorString()));
    });
}

void Engine::downloadDone()
{
    const QString setup = m_dlPath;
    if (m_updSums.isEmpty()) {
        runUpdater(setup);
        return;
    }
    // the release's SHA256SUMS.txt: the file is exactly the one on GitHub
    QNetworkReply* s = m_net->get(request(m_updSums, version()));
    connect(s, &QNetworkReply::finished, this, [this, s, setup] {
        s->deleteLater();
        const QString sums = QString::fromUtf8(s->readAll());
        QFile f(setup);
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (f.open(QIODevice::ReadOnly)) h.addData(&f);
        f.close();
        const QString hex = QString::fromLatin1(h.result().toHex());
        if (s->error() == QNetworkReply::NoError && sums.contains(QLatin1String("GenryBL_Setup.exe")) && !sums.contains(hex, Qt::CaseInsensitive)) {
            QFile::remove(setup);
            setUpdate(QStringLiteral("error"), gbTr("Файл не сошёлся с контрольной суммой релиза — нажми ещё раз, скачаю заново"));
            return;
        }
        runUpdater(setup);
    });
}

void Engine::runUpdater(const QString& setup)
{
    setUpdate(QStringLiteral("starting"), gbTr("Ставлю %1 — GenryBL закроется и откроется уже новым").arg(m_update.value(QStringLiteral("version")).toString()), 1);
    const QStringList args{QStringLiteral("--silent-to"), QDir::toNativeSeparators(m_root), QStringLiteral("--no-shortcuts"),
                           QStringLiteral("--update"), QStringLiteral("--relaunch")};
    if (!QProcess::startDetached(setup, args, QFileInfo(setup).absolutePath())) {
        setUpdate(QStringLiteral("error"), gbTr("Не смог запустить установщик %1").arg(QDir::toNativeSeparators(setup)));
        return;
    }
    QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
}
