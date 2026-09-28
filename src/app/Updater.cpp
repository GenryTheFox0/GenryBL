// GenryBL V1 - «Обновить». The newest GenryBL is either the latest GitHub release or the one inside the Steam
// Workshop item (Steam downloads a new version of a subscribed item by itself - then nothing is downloaded here).
// Its installer runs over this very folder: GenryBL_Setup.exe --silent-to <root> --no-shortcuts --update --relaunch.
// It waits until this program has closed, copies the new files (projects/ and work/ are never touched) and starts
// the new GenryBL. The developer build (built from the sources) never updates itself.
#include "Engine.h"

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

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }
const char* const kLatest = "https://api.github.com/repos/GenryTheFox0/GenryBL/releases/latest";
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
        if (manual) setUpdate(QStringLiteral("dev"), U("Это сборка разработчика — она собирается из исходников, обновлять её нечем"));
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
    setUpdate(QStringLiteral("checking"), U("Проверяю обновления…"));
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // GENRYBL_UPDATE_API: another «releases/latest» (the updater's own test serves one locally)
    const QString api = qEnvironmentVariableIsEmpty("GENRYBL_UPDATE_API") ? QString::fromLatin1(kLatest) : qEnvironmentVariable("GENRYBL_UPDATE_API");
    QNetworkReply* r = m_net->get(request(api, version()));
    connect(r, &QNetworkReply::finished, this, [this, r, manual, now, steamExe, steamVer] {
        r->deleteLater();
        QString ghVer, url, sums;
        qint64 size = 0;
        const bool netOk = r->error() == QNetworkReply::NoError;
        if (netOk) {
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
        const bool steamNew = !steamExe.isEmpty() && newer(steamVer, version());
        const bool ghNew = !url.isEmpty() && newer(ghVer, version());
        if (steamNew && (!ghNew || !newer(ghVer, steamVer))) {
            // Steam has already brought it: nothing to download
            m_updLocal = steamExe;
            m_update.insert(QStringLiteral("version"), pretty(steamVer));
            m_update.insert(QStringLiteral("source"), QStringLiteral("steam"));
            setUpdate(QStringLiteral("available"), U("Вышла %1 — Steam уже скачал её из Мастерской").arg(pretty(steamVer)));
        } else if (ghNew) {
            m_updUrl = url;
            m_updSums = sums;
            m_updSize = size;
            m_update.insert(QStringLiteral("version"), pretty(ghVer));
            m_update.insert(QStringLiteral("source"), QStringLiteral("github"));
            setUpdate(QStringLiteral("available"), U("Вышла %1 на GitHub, %2 МБ").arg(pretty(ghVer)).arg(qMax<qint64>(1, size / (1024 * 1024))));
        } else if (!netOk) {
            if (manual) setUpdate(QStringLiteral("error"), U("Не достучался до GitHub (%1). Стоишь из Мастерской Steam — Steam обновит сам").arg(r->errorString()));
            else setUpdate(QString(), QString());
        } else {
            setUpdate(manual ? QStringLiteral("latest") : QString(), manual ? U("У тебя последняя версия — %1").arg(version()) : QString());
        }
        if (now && m_update.value(QStringLiteral("state")).toString() == QLatin1String("available")) startUpdate();
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
            setUpdate(QStringLiteral("error"), U("Не смог взять установщик из папки Мастерской"));
            return;
        }
        runUpdater(setup);
        return;
    }
    if (!m_net) m_net = new QNetworkAccessManager(this);
    auto* file = new QFile(setup, this);
    if (!file->open(QIODevice::WriteOnly)) {
        file->deleteLater();
        setUpdate(QStringLiteral("error"), U("Не могу записать во временную папку: %1").arg(QDir::toNativeSeparators(tmp)));
        return;
    }
    setUpdate(QStringLiteral("downloading"), U("Качаю %1…").arg(ver), 0);
    QNetworkRequest rq = request(m_updUrl, version());
    rq.setTransferTimeout(60000);
    QNetworkReply* r = m_net->get(rq);
    connect(r, &QNetworkReply::readyRead, this, [r, file] { file->write(r->readAll()); });
    connect(r, &QNetworkReply::downloadProgress, this, [this, ver](qint64 got, qint64 total) {
        const qint64 all = total > 0 ? total : m_updSize;
        setUpdate(QStringLiteral("downloading"), U("Качаю %1: %2 из %3 МБ").arg(ver).arg(got / (1024 * 1024)).arg(qMax<qint64>(1, all / (1024 * 1024))),
                  all > 0 ? double(got) / double(all) : 0);
    });
    connect(r, &QNetworkReply::finished, this, [this, r, file, setup] {
        r->deleteLater();
        file->write(r->readAll());
        file->close();
        file->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            setUpdate(QStringLiteral("error"), U("Скачать не вышло: %1").arg(r->errorString()));
            return;
        }
        if (m_updSize > 0 && QFileInfo(setup).size() != m_updSize) {
            setUpdate(QStringLiteral("error"), U("Скачалось не целиком — нажми ещё раз"));
            return;
        }
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
            const QString hex = QString::fromLatin1(h.result().toHex());
            if (s->error() == QNetworkReply::NoError && sums.contains(QLatin1String("GenryBL_Setup.exe")) && !sums.contains(hex, Qt::CaseInsensitive)) {
                setUpdate(QStringLiteral("error"), U("Файл не сошёлся с контрольной суммой релиза — нажми ещё раз"));
                return;
            }
            runUpdater(setup);
        });
    });
}

void Engine::runUpdater(const QString& setup)
{
    setUpdate(QStringLiteral("starting"), U("Ставлю %1 — GenryBL закроется и откроется уже новым").arg(m_update.value(QStringLiteral("version")).toString()), 1);
    const QStringList args{QStringLiteral("--silent-to"), QDir::toNativeSeparators(m_root), QStringLiteral("--no-shortcuts"),
                           QStringLiteral("--update"), QStringLiteral("--relaunch")};
    if (!QProcess::startDetached(setup, args, QFileInfo(setup).absolutePath())) {
        setUpdate(QStringLiteral("error"), U("Не смог запустить установщик %1").arg(QDir::toNativeSeparators(setup)));
        return;
    }
    QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
}
