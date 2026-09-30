"""V2.1 review fixes: three new messages in every language.
Order of each row: en uk be kk pl cs de fr es pt_BR it tr ja zh_CN zh_TW ko vi id th"""
import io, json, os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CODES = ['en', 'uk', 'be', 'kk', 'pl', 'cs', 'de', 'fr', 'es', 'pt_BR', 'it', 'tr', 'ja', 'zh_CN', 'zh_TW', 'ko', 'vi', 'id', 'th']
R = [
 ('Не удалось запустить gb_workshop.exe — возможно, его блокирует антивирус',
  ['Couldn’t start gb_workshop.exe — an antivirus may be blocking it', 'Не вдалося запустити gb_workshop.exe — можливо, його блокує антивірус',
   'Не ўдалося запусціць gb_workshop.exe — магчыма, яго блакуе антывірус', 'gb_workshop.exe іске қосылмады — мүмкін, оны антивирус бұғаттап тұр',
   'Nie udało się uruchomić gb_workshop.exe — możliwe, że blokuje go antywirus', 'gb_workshop.exe se nepodařilo spustit — možná ho blokuje antivir',
   'gb_workshop.exe ließ sich nicht starten — vielleicht blockiert ihn ein Virenscanner', 'Impossible de lancer gb_workshop.exe — un antivirus le bloque peut-être',
   'No se pudo iniciar gb_workshop.exe — puede que un antivirus lo esté bloqueando', 'Não deu para iniciar o gb_workshop.exe — talvez um antivírus esteja bloqueando',
   'Impossibile avviare gb_workshop.exe — forse lo blocca un antivirus', 'gb_workshop.exe başlatılamadı — bir antivirüs engelliyor olabilir',
   'gb_workshop.exe を起動できません — ウイルス対策ソフトがブロックしているかも', '无法启动 gb_workshop.exe — 可能被杀毒软件拦截了',
   '無法啟動 gb_workshop.exe — 可能被防毒軟體攔截了', 'gb_workshop.exe를 실행하지 못했어요 — 백신 프로그램이 막고 있을 수 있어요',
   'Không khởi chạy được gb_workshop.exe — có thể phần mềm diệt virus đang chặn nó', 'gb_workshop.exe tidak bisa dijalankan — mungkin diblokir antivirus',
   'เปิด gb_workshop.exe ไม่ได้ — อาจถูกโปรแกรมแอนติไวรัสบล็อกอยู่']),
 ('Загрузка в Steam уже идёт',
  ['An upload to Steam is already running', 'Завантаження в Steam уже йде', 'Загрузка ў Steam ужо ідзе', 'Steam-ге жүктеу жүріп жатыр',
   'Wysyłanie do Steam już trwa', 'Nahrávání do Steamu už běží', 'Ein Upload zu Steam läuft schon', 'Un envoi vers Steam est déjà en cours',
   'Ya hay una subida a Steam en curso', 'Já tem um envio para o Steam em andamento', 'Un caricamento su Steam è già in corso',
   'Steam’e yükleme zaten sürüyor', 'Steam へのアップロードはすでに進行中', '已经在上传到 Steam 了', '已經在上傳到 Steam 了',
   'Steam 업로드가 이미 진행 중이에요', 'Đang tải lên Steam rồi', 'Unggahan ke Steam sedang berjalan', 'กำลังอัปโหลดไป Steam อยู่แล้ว']),
 ('Steam ещё занят прошлой подпиской — секунду',
  ['Steam is still busy with the last subscription — one moment', 'Steam ще зайнятий минулою підпискою — секунду',
   'Steam яшчэ заняты мінулай падпіскай — секунду', 'Steam әлі алдыңғы жазылыммен бос емес — бір сәт',
   'Steam jest jeszcze zajęty poprzednią subskrypcją — chwilę', 'Steam ještě řeší minulé odebírání — okamžik',
   'Steam ist noch mit dem letzten Abo beschäftigt — einen Moment', 'Steam est encore occupé avec l’abonnement précédent — une seconde',
   'Steam sigue ocupado con la suscripción anterior — un momento', 'O Steam ainda está ocupado com a inscrição anterior — um segundo',
   'Steam è ancora occupato con l’iscrizione precedente — un attimo', 'Steam hâlâ önceki abonelikle meşgul — bir saniye',
   'Steam は前のサブスクライブを処理中 — ちょっと待ってね', 'Steam 还在处理上一次订阅 — 稍等', 'Steam 還在處理上一次訂閱 — 稍等',
   'Steam이 아직 지난 구독을 처리 중이에요 — 잠깐만요', 'Steam vẫn đang bận với lượt đăng ký trước — chờ chút',
   'Steam masih sibuk dengan langganan sebelumnya — sebentar', 'Steam ยังติดการติดตามครั้งก่อนอยู่ — รอสักครู่']),
]
src = json.load(io.open(os.path.join(ROOT, 'data', 'i18n', '_source.json'), encoding='utf-8'))
for ru, tr in R:
    assert ru in src, 'not in the source: ' + ru
    assert len(tr) == len(CODES)
for k, code in enumerate(CODES):
    p = os.path.join(ROOT, 'data', 'i18n', code + '.json')
    d = json.load(io.open(p, encoding='utf-8'))
    for ru, tr in R:
        d[ru] = tr[k]
    json.dump(dict(sorted(d.items())), io.open(p, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(CODES), 'languages +', len(R))
