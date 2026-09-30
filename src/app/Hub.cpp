// GenryBL V1 - «Бесконечное лето», the Center: the game and every mod it loads (the Steam Workshop and game/mods),
// the Workshop itself over the web (its own titles and covers, a page of it, «Подписаться» through gb_workshop.exe),
// «Мастерская GenryBL» (a catalog on GitHub - no server of its own), and Discord: what I do in GenryBL, and the
// status mod inside the game (data/presence/genry_presence.rpy).
#include "Engine.h"
#include "Builder.h"
#include "Compiler.h"
#include "Discord.h"
#include "ModHub.h"
#include "Rpa.h"
#include "Py.h"
#include "Text.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent>

#include <memory>

using namespace gb;

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

QByteArray readFile(const QString& p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// GenryBL's own app on discord.com/developers (its name is what Discord shows: «Играет в GenryBL»)
const char* const kDiscordApp = "1554838689467858966";
// the big picture of the status: the art asset «genrybl» uploaded to that app (Rich Presence -> Art Assets)
const char* const kIcon = "genrybl";
const char* const kRepo = "https://github.com/GenryTheFox0/GenryBL";
// the catalog: jsDelivr first (it reaches where raw.githubusercontent does not), GitHub itself second
const char* const kCatalog[] = {"https://cdn.jsdelivr.net/gh/GenryTheFox0/GenryBL@main/catalog/mods.json",
                                "https://raw.githubusercontent.com/GenryTheFox0/GenryBL/main/catalog/mods.json"};

QString clip(QString s)
{
    s = s.simplified();
    if (s.size() > 128) s = s.left(127) + QChar(0x2026);
    while (s.size() < 2 && !s.isEmpty()) s += QLatin1Char(' ');
    return s;
}

QNetworkRequest webRequest(const QUrl& url, const QString& version)
{
    QNetworkRequest rq(url);
    rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GenryBL/") + version);
    rq.setTransferTimeout(20000);
    return rq;
}

QVariantMap hubMap(const HubMod& m)
{
    QVariantList entries;
    for (const HubEntry& e : m.entries) entries << QVariantMap{{QStringLiteral("label"), e.label}, {QStringLiteral("title"), e.title}};
    return {{QStringLiteral("source"), m.source}, {QStringLiteral("id"), m.id}, {QStringLiteral("dir"), m.dir}, {QStringLiteral("title"), m.title},
            {QStringLiteral("entries"), entries}, {QStringLiteral("preview"), m.preview}, {QStringLiteral("size"), m.size},
            {QStringLiteral("files"), m.files}, {QStringLiteral("genrybl"), m.genrybl}, {QStringLiteral("twins"), m.twins}};
}

QString steamDllOf(const QString& esRoot)
{
    for (const char* sub : {"/lib/windows-x86_64/steam_api64.dll", "/lib/py3-windows-x86_64/steam_api64.dll", "/lib/py2-windows-x86_64/steam_api64.dll"})
        if (QFileInfo::exists(esRoot + QString::fromLatin1(sub))) return esRoot + QString::fromLatin1(sub);
    return {};
}

} // namespace

// ---------------------------------------------------------------- Discord

bool Engine::discordOn() const { return m_settings.value(QStringLiteral("discord"), QStringLiteral("true")).toString() == QLatin1String("true"); }
bool Engine::discordHide() const { return m_settings.value(QStringLiteral("discordHide"), QStringLiteral("false")).toString() == QLatin1String("true"); }
bool Engine::gamePresence() const
{
    return !m_es.esRoot().isEmpty() && QFileInfo::exists(m_es.esRoot() + QStringLiteral("/game/mods/genry_presence/genry_presence.rpy"));
}

QString Engine::discordState() const
{
    if (!discordOn()) return QStringLiteral("off");
    const QString id = m_settings.value(QStringLiteral("discordAppId"), QString::fromLatin1(kDiscordApp)).toString();
    if (id.isEmpty()) return QStringLiteral("noid");
    if (m_discord && m_discord->isReady()) return QStringLiteral("ready");
    return m_discord && !m_discord->error().isEmpty() ? QStringLiteral("error") : QStringLiteral("waiting");
}

void Engine::setDiscordOn(bool on)
{
    m_settings.setValue(QStringLiteral("discord"), on ? QStringLiteral("true") : QStringLiteral("false"));
    updatePresence();
}

void Engine::setDiscordHide(bool on)
{
    m_settings.setValue(QStringLiteral("discordHide"), on ? QStringLiteral("true") : QStringLiteral("false"));
    updatePresence();
}

void Engine::startHub()
{
    if (m_discord) return;
    m_sessionStart = QDateTime::currentMSecsSinceEpoch();
    m_discord = new DiscordPresence(this);
    connect(m_discord, &DiscordPresence::readyChanged, this, &Engine::discordChanged);
    connect(this, &Engine::gameRunningChanged, this, &Engine::updatePresence);
    if (gamePresence()) {                               // a newer GenryBL brings a newer status mod; the language may have changed
        const QString dir = m_es.esRoot() + QStringLiteral("/game/mods/genry_presence");
        if (readFile(dir + QStringLiteral("/genry_presence.rpy")) != readFile(m_root + QStringLiteral("/data/presence/genry_presence.rpy"))) {
            QFile::remove(dir + QStringLiteral("/genry_presence.rpy"));
            QFile::remove(dir + QStringLiteral("/genry_presence.rpyc"));
            QFile::copy(m_root + QStringLiteral("/data/presence/genry_presence.rpy"), dir + QStringLiteral("/genry_presence.rpy"));
        }
        writeGamePresenceConfig();
    }
    updatePresence();
}

void Engine::presence(const QString& kind, const QString& projectId, int line)
{
    if (kind == m_presKind && projectId == m_presProject && line == m_presLine) return;
    m_presKind = kind;
    m_presProject = projectId;
    m_presLine = line;
    updatePresence();
}

void Engine::updatePresence()
{
    if (!m_discord) return;
    const QString id = m_settings.value(QStringLiteral("discordAppId"), QString::fromLatin1(kDiscordApp)).toString();
    if (!discordOn() || id.isEmpty() || m_shotMode) {     // a self-check picture tells Discord nothing
        m_discord->setClientId({});
        emit discordChanged();
        return;
    }
    m_discord->setClientId(id);
    const bool hide = discordHide();
    auto nameOf = [this](const QString& pid) {
        for (const QVariant& v : m_projects)
            if (v.toMap().value(QStringLiteral("id")).toString() == pid) return v.toMap().value(QStringLiteral("name")).toString();
        return pid;
    };
    QString details, state;
    if (gameRunning() && !m_playProject.isEmpty()) {
        details = hide ? gbTr("Проверяет мод в игре") : gbTr("Проверяет мод «%1» в игре").arg(nameOf(m_playProject));
    } else if (m_presKind == QLatin1String("edit") && !m_presProject.isEmpty()) {
        details = hide ? gbTr("Пишет мод") : gbTr("Пишет мод «%1»").arg(nameOf(m_presProject));
        if (!hide) {
            const QStringList lines = pySplitLines(loadStory(m_presProject));
            for (int i = qMin(m_presLine, int(lines.size())) - 1; i >= 0; --i) {
                const QString s = pyStrip(lines[i]);
                if (s.startsWith(QLatin1Char(':'))) { state = gbTr("Сцена «%1»").arg(pyStrip(s.mid(1))); break; }
            }
        }
    } else if (m_presKind == QLatin1String("cinema")) {
        details = gbTr("Смотрит свой мод в кино");
        if (!hide && !m_presProject.isEmpty()) state = U("«") + nameOf(m_presProject) + U("»");
    } else if (m_presKind == QLatin1String("center")) {
        details = gbTr("Выбирает мод для «Бесконечного лета»");
    } else {
        details = gbTr("В главном меню GenryBL");
        state = gbTr("Конструктор модов «Бесконечного лета»");
    }
    QJsonObject act{{QStringLiteral("details"), clip(details)},
                    {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), m_sessionStart / 1000}}},
                    {QStringLiteral("assets"), QJsonObject{{QStringLiteral("large_image"), QString::fromLatin1(kIcon)},
                                                           {QStringLiteral("large_text"), QStringLiteral("GenryBL ") + version()}}},
                    {QStringLiteral("buttons"), QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("GenryBL")},
                                                                       {QStringLiteral("url"), QString::fromLatin1(kRepo)}}}}};
    if (!state.isEmpty()) act.insert(QStringLiteral("state"), clip(state));
    m_discord->setActivity(act);
    emit discordChanged();
}

void Engine::writeGamePresenceConfig()
{
    QJsonObject cfg{{QStringLiteral("lang"), m_lang}};
    const QString gameApp = m_settings.value(QStringLiteral("discordGameAppId")).toString();
    if (!gameApp.isEmpty()) cfg.insert(QStringLiteral("client_id"), gameApp);
    QFile f(m_es.esRoot() + QStringLiteral("/game/mods/genry_presence/genry_presence.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
}

bool Engine::setGamePresence(bool on)
{
    const QString es = m_es.esRoot();
    if (es.isEmpty()) return false;
    const QString dir = es + QStringLiteral("/game/mods/genry_presence");
    bool ok = true;
    if (on) {
        QDir().mkpath(dir);
        QFile::remove(dir + QStringLiteral("/genry_presence.rpy"));
        QFile::remove(dir + QStringLiteral("/genry_presence.rpyc"));
        ok = QFile::copy(m_root + QStringLiteral("/data/presence/genry_presence.rpy"), dir + QStringLiteral("/genry_presence.rpy"));
        if (ok) writeGamePresenceConfig();
        emit toast(ok ? gbTr("Статус в Discord включён в самой игре — со следующего запуска БЛ") : gbTr("Не удалось положить статус в папку игры"), ok ? 0 : 2);
    } else if (QFileInfo::exists(dir + QStringLiteral("/genry_presence.rpy"))) {
        ok = QDir(dir).removeRecursively();
    }
    emit discordChanged();
    return ok;
}

// ---------------------------------------------------------------- the game and its mods

void Engine::scanGameMods()
{
    const QString es = m_es.esRoot();
    auto* w = new QFutureWatcher<QVariantList>(this);
    connect(w, &QFutureWatcher<QVariantList>::finished, this, [this, w] {
        const QVariantList r = w->result();
        w->deleteLater();
        emit gameModsReady(r);
    });
    w->setFuture(QtConcurrent::run([es]() -> QVariantList {
        QVariantList out;
        for (const HubMod& m : gb::scanGameMods(es)) out << hubMap(m);
        return out;
    }));
}

void Engine::playGame()
{
    // through Steam: playtime, the overlay, the achievements; without Steam - the game itself
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/331470")))) {
        const QString es = m_es.esRoot();
        QProcess::startDetached(build::esExe(es), {}, es);
    }
    emit toast(gbTr("Запускаю «Бесконечное лето»"), 0);
}

void Engine::playGameMod(const QString& label)
{
    QString err;
    removePlayHook(m_es.esRoot());
    if (!writePlayHook(m_es.esRoot(), label, &err)) { emit toast(gbTr("Не удалось подготовить запуск мода: ") + err, 2); return; }
    playGame();
    emit toast(gbTr("БЛ откроется сразу в этом моде, как из её меню «Моды»"), 0);
    // the game was running already (it never passes its main menu again): the hook must not catch a later launch
    QTimer::singleShot(180000, this, [this] { removePlayHook(m_es.esRoot()); });
}

// ---------------------------------------------------------------- the Steam Workshop over the web

void Engine::steamDetails(const QStringList& idsIn)
{
    // the cache on disk first: the Center opens with the covers it saw last time
    static bool loaded = false;
    const QString cacheFile = m_root + QStringLiteral("/work/cache/steam_items.json");
    if (!loaded) {
        loaded = true;
        const QJsonObject o = QJsonDocument::fromJson(readFile(cacheFile)).object();
        for (auto it = o.begin(); it != o.end(); ++it) m_steamCache.insert(it.key(), it.value().toObject().toVariantMap());
    }
    QVariantMap known;
    QStringList ask;
    for (const QString& id : idsIn) {
        if (m_steamCache.contains(id)) known.insert(id, m_steamCache.value(id));
        ask << id;                                       // asked anyway: the numbers change
    }
    if (!known.isEmpty()) emit steamDetailsReady(known);
    if (ask.isEmpty()) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("itemcount"), QString::number(ask.size()));
    for (int i = 0; i < ask.size(); ++i) form.addQueryItem(QStringLiteral("publishedfileids[%1]").arg(i), ask[i]);
    QNetworkRequest rq = webRequest(QUrl(QStringLiteral("https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/")), version());
    rq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* r = m_net->post(rq, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(r, &QNetworkReply::finished, this, [this, r, cacheFile] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        const QJsonArray items = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("response")).toObject()
                                     .value(QStringLiteral("publishedfiledetails")).toArray();
        QVariantMap out;
        for (const QJsonValue& v : items) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("result")).toInt() != 1) continue;
            QString desc = o.value(QStringLiteral("description")).toString();
            static const QRegularExpression bb(QStringLiteral("\\[/?[a-zA-Z0-9=\\*]+[^\\]]*\\]"));
            desc.remove(bb);
            const QVariantMap m{{QStringLiteral("id"), o.value(QStringLiteral("publishedfileid")).toString()},
                                {QStringLiteral("title"), o.value(QStringLiteral("title")).toString()},
                                {QStringLiteral("preview"), o.value(QStringLiteral("preview_url")).toString()},
                                {QStringLiteral("subs"), o.value(QStringLiteral("lifetime_subscriptions")).toVariant().toLongLong()
                                                             ? o.value(QStringLiteral("lifetime_subscriptions")).toVariant()
                                                             : o.value(QStringLiteral("subscriptions")).toVariant()},
                                {QStringLiteral("favorited"), o.value(QStringLiteral("favorited")).toVariant()},
                                {QStringLiteral("views"), o.value(QStringLiteral("views")).toVariant()},
                                {QStringLiteral("updated"), o.value(QStringLiteral("time_updated")).toVariant()},
                                {QStringLiteral("size"), o.value(QStringLiteral("file_size")).toVariant()},
                                {QStringLiteral("desc"), desc.simplified().left(600)}};
            m_steamCache.insert(m.value(QStringLiteral("id")).toString(), m);
            out.insert(m.value(QStringLiteral("id")).toString(), m);
        }
        if (out.isEmpty()) return;
        QJsonObject all;
        for (auto it = m_steamCache.cbegin(); it != m_steamCache.cend(); ++it) all.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
        QDir().mkpath(QFileInfo(cacheFile).absolutePath());
        QFile f(cacheFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        emit steamDetailsReady(out);
    });
}

void Engine::steamBrowse(const QString& sort, const QString& query, int page)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("appid"), QStringLiteral("331470"));
    q.addQueryItem(QStringLiteral("section"), QStringLiteral("readytouseitems"));
    q.addQueryItem(QStringLiteral("numperpage"), QStringLiteral("30"));
    q.addQueryItem(QStringLiteral("p"), QString::number(qMax(1, page)));
    if (!query.trimmed().isEmpty()) {
        q.addQueryItem(QStringLiteral("searchtext"), query.trimmed());
        q.addQueryItem(QStringLiteral("browsesort"), QStringLiteral("textsearch"));
    } else {
        const QString s = sort == QLatin1String("new") ? QStringLiteral("mostrecent") : sort == QLatin1String("top") ? QStringLiteral("toprated")
                        : sort == QLatin1String("updated") ? QStringLiteral("lastupdated") : QStringLiteral("trend");
        q.addQueryItem(QStringLiteral("browsesort"), s);
        if (s == QLatin1String("trend")) q.addQueryItem(QStringLiteral("days"), QStringLiteral("90"));
    }
    QUrl url(QStringLiteral("https://steamcommunity.com/workshop/browse/"));
    url.setQuery(q);
    QNetworkReply* r = m_net->get(webRequest(url, version()));
    connect(r, &QNetworkReply::finished, this, [this, r, page] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit steamBrowseReady({}, page, false, gbTr("Мастерская Steam не отвечает — проверь интернет"));
            return;
        }
        const QString html = QString::fromUtf8(r->readAll());
        // the items of the page: links to their pages (the page today), data-publishedfileid (the page before)
        static const QRegularExpression idRe(QStringLiteral("(?:sharedfiles/filedetails/\\?id=|data-publishedfileid=\")(\\d+)"));
        QStringList ids;
        for (auto it = idRe.globalMatch(html); it.hasNext();) {
            const QString id = it.next().captured(1);
            if (!ids.contains(id)) ids << id;
        }
        QVariantList items;
        for (const QString& id : ids) items << QVariantMap{{QStringLiteral("id"), id}};
        emit steamBrowseReady(items, page, ids.size() >= 30, QString());
        if (!ids.isEmpty()) steamDetails(ids);
    });
}

QString Engine::workshopHelper() const { return QCoreApplication::applicationDirPath() + QStringLiteral("/gb_workshop.exe"); }

void Engine::steamSubscribe(const QString& id, bool on)
{
    if (m_subscribe) { emit steamSubscribed(id, on, false, gbTr("Steam ещё занят прошлой подпиской — секунду")); return; }
    const QString dll = steamDllOf(m_es.esRoot());
    if (dll.isEmpty() || !QFileInfo::exists(workshopHelper())) {
        emit steamSubscribed(id, on, false, gbTr("Нет Steam-библиотеки игры или gb_workshop.exe — открываю страницу в Steam"));
        QDesktopServices::openUrl(QUrl(QStringLiteral("steam://url/CommunityFilePage/") + id));
        return;
    }
    auto* p = new QProcess(this);
    m_subscribe = p;
    auto out = std::make_shared<QString>();
    connect(p, &QProcess::readyReadStandardOutput, this, [p, out] { *out += QString::fromUtf8(p->readAllStandardOutput()); });
    connect(p, &QProcess::finished, this, [this, p, id, on, out](int code) {
        p->deleteLater();
        m_subscribe = nullptr;
        const bool ok = code == 0 && out->contains(QLatin1String("DONE"));
        emit steamSubscribed(id, on, ok,
                             ok ? (on ? gbTr("Подписка оформлена — Steam скачает мод сам, он появится во вкладке «В игре»")
                                      : gbTr("Отписался — Steam уберёт мод из игры"))
                                : gbTr("Steam не ответил: он запущен и ты в своём аккаунте?"));
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, id, on](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        m_subscribe = nullptr;
        emit steamSubscribed(id, on, false, gbTr("Не удалось запустить gb_workshop.exe — возможно, его блокирует антивирус"));
    });
    p->setWorkingDirectory(QDir::tempPath());
    p->start(workshopHelper(), {QStringLiteral("--dll"), QDir::toNativeSeparators(dll), on ? QStringLiteral("--subscribe") : QStringLiteral("--unsubscribe"), id});
}

// ---------------------------------------------------------------- «Мастерская GenryBL»

void Engine::loadCatalog()
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [this, attempt](int n) {
        QNetworkReply* r = m_net->get(webRequest(QUrl(QString::fromLatin1(kCatalog[n])), version()));
        connect(r, &QNetworkReply::finished, this, [this, r, n, attempt] {
            r->deleteLater();
            const QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
            // not on GitHub yet (404): an empty catalog, not «no internet»
            if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404 && n + 1 >= int(sizeof kCatalog / sizeof *kCatalog)) {
                emit catalogReady({}, QString());
                return;
            }
            if (r->error() != QNetworkReply::NoError || !doc.isObject()) {
                if (n + 1 < int(sizeof kCatalog / sizeof *kCatalog)) { (*attempt)(n + 1); return; }
                emit catalogReady({}, gbTr("Каталог не загрузился — проверь интернет"));
                return;
            }
            QVariantList mods;
            const QString modsDir = m_es.esRoot() + QStringLiteral("/game/mods/");
            for (const QJsonValue& v : doc.object().value(QStringLiteral("mods")).toArray()) {
                QVariantMap m = v.toObject().toVariantMap();
                const QString folder = m.value(QStringLiteral("folder")).toString();
                // a Steam Workshop item in the catalog: installed = subscribed (its folder is in the Workshop content)
                const QString ws = m.value(QStringLiteral("workshop")).toString();
                const QString wsDir = Vfs::workshopDirFor(m_es.esRoot());
                m.insert(QStringLiteral("installed"), ws.isEmpty() ? !folder.isEmpty() && QFileInfo::exists(modsDir + folder + QStringLiteral("/.genrybl_catalog"))
                                                                   : !wsDir.isEmpty() && QFileInfo::exists(wsDir + QLatin1Char('/') + ws));
                mods << m;
            }
            emit catalogReady(mods, QString());
        });
    };
    (*attempt)(0);
}

void Engine::installCatalogMod(const QVariantMap& mod)
{
    const QString id = mod.value(QStringLiteral("id")).toString();
    const QUrl url(mod.value(QStringLiteral("zip")).toString());
    if (!url.isValid() || url.scheme() != QLatin1String("https")) { emit catalogInstalled(id, false, gbTr("У мода нет ссылки на архив")); return; }
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest rq = webRequest(url, version());
    rq.setTransferTimeout(120000);
    QNetworkReply* r = m_net->get(rq);
    connect(r, &QNetworkReply::finished, this, [this, r, id, mod] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) { emit catalogInstalled(id, false, gbTr("Архив не скачался: ") + r->errorString()); return; }
        auto tmp = std::make_shared<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/genrybl_catalog_XXXXXX.zip"));
        if (!tmp->open() || tmp->write(r->readAll()) < 0) { emit catalogInstalled(id, false, gbTr("Не удалось сохранить архив")); return; }
        tmp->flush();
        const QString zip = tmp->fileName(), modsDir = m_es.esRoot() + QStringLiteral("/game/mods"), expect = mod.value(QStringLiteral("folder")).toString();
        auto* w = new QFutureWatcher<QString>(this);
        connect(w, &QFutureWatcher<QString>::finished, this, [this, w, id, tmp] {
            const QString err = w->result();
            w->deleteLater();
            emit catalogInstalled(id, err.isEmpty(), err.isEmpty() ? gbTr("Мод установлен в игру — он уже в её меню «Моды»") : err);
        });
        w->setFuture(QtConcurrent::run([zip, modsDir, expect]() -> QString {
            QString folder, err;
            return build::installModArchive(zip, modsDir, expect, &folder, &err) ? QString() : err;
        }));
    });
}

QString Engine::catalogSuggestUrl(const QString& projectId) const
{
    QString name = projectId;
    for (const QVariant& v : m_projects)
        if (v.toMap().value(QStringLiteral("id")).toString() == projectId) name = v.toMap().value(QStringLiteral("name")).toString();
    QUrl url(QString::fromLatin1(kRepo) + QStringLiteral("/issues/new"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), gbTr("Мод в Мастерскую GenryBL: ") + name);
    q.addQueryItem(QStringLiteral("body"), gbTr("Название: ") + name + QStringLiteral("\n") + gbTr("Автор: ") + QStringLiteral("\n") +
                                               gbTr("О чём мод: ") + QStringLiteral("\n\n") +
                                               gbTr("Приложи архив из «Экспорт → Архив для игроков» и обложку (перетащи файлы сюда)."));
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}
