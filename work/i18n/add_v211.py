"""V2.1.1: the timeline's stretch and keyframes - every new string in every language.
Order of each row: en uk be kk pl cs de fr es pt_BR it tr ja zh_CN zh_TW ko vi id th"""
import io, json, os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CODES = ['en', 'uk', 'be', 'kk', 'pl', 'cs', 'de', 'fr', 'es', 'pt_BR', 'it', 'tr', 'ja', 'zh_CN', 'zh_TW', 'ko', 'vi', 'id', 'th']
R = [
 ('Тащи край — растянуть или укоротить: сдвигается строка, которая это заканчивает',
  ['Drag the edge to stretch or shorten it: the line that ends it moves', 'Тягни край — розтягнути чи вкоротити: зсувається рядок, який це закінчує',
   'Цягні край — расцягнуць ці скараціць: зрушваецца радок, які гэта заканчвае', 'Шетін сүйре — созу не қысқарту: мұны аяқтайтын жол жылжиды',
   'Przeciągnij krawędź — rozciągnij lub skróć: przesuwa się linijka, która to kończy', 'Táhni okraj — natáhnout nebo zkrátit: posune se řádek, který to končí',
   'Zieh den Rand — länger oder kürzer: die Zeile, die es beendet, wandert mit', 'Glisse le bord pour allonger ou raccourcir : la ligne qui le termine se déplace',
   'Arrastra el borde para alargar o acortar: se mueve la línea que lo termina', 'Arraste a borda para esticar ou encurtar: a linha que termina isso se move',
   'Trascina il bordo per allungare o accorciare: si sposta la riga che lo chiude', 'Kenarı sürükle — uzat ya da kısalt: bunu bitiren satır kayar',
   '端をドラッグで伸縮 — それを終わらせる行が動きます', '拖动边缘可拉长或缩短 — 结束它的那一行会移动', '拖曳邊緣可拉長或縮短 — 結束它的那一行會移動',
   '가장자리를 끌어 늘리거나 줄이기 — 이걸 끝내는 줄이 옮겨져요', 'Kéo mép để kéo dài hoặc rút ngắn — dòng kết thúc nó sẽ dịch chuyển',
   'Seret tepinya untuk memanjangkan atau memendekkan — baris yang mengakhirinya ikut bergeser', 'ลากขอบเพื่อยืดหรือหด — บรรทัดที่จบสิ่งนี้จะขยับตาม']),
 ('Ключевой кадр: отсюда персонаж плавно переедет на новое место — потом тащи его в превью',
  ['Keyframe: from here the character glides to a new place — then drag them in the preview', 'Ключовий кадр: звідси персонаж плавно переїде на нове місце — потім тягни його в превʼю',
   'Ключавы кадр: адсюль персанаж плаўна пераедзе на новае месца — потым цягні яго ў прэв’ю', 'Кілт кадр: осы жерден кейіпкер жаңа орынға баяу жылжиды — сосын оны превьюде сүйре',
   'Klatka kluczowa: stąd postać płynnie przejedzie na nowe miejsce — potem przeciągnij ją w podglądzie', 'Klíčový snímek: odsud postava plynule přejede na nové místo — pak ji táhni v náhledu',
   'Schlüsselbild: ab hier gleitet die Figur an einen neuen Platz — dann zieh sie in der Vorschau', 'Image clé : d’ici le personnage glisse vers une nouvelle place — puis déplace-le dans l’aperçu',
   'Fotograma clave: desde aquí el personaje se desliza a otro sitio — luego arrástralo en la vista previa', 'Quadro-chave: daqui o personagem desliza para um novo lugar — depois arraste-o na prévia',
   'Fotogramma chiave: da qui il personaggio scivola in un nuovo punto — poi trascinalo nell’anteprima', 'Anahtar kare: buradan karakter yeni yerine süzülür — sonra önizlemede sürükle',
   'キーフレーム:ここからキャラが新しい位置へなめらかに移動 — あとはプレビューでドラッグ', '关键帧:从这里角色平滑移到新位置 — 然后在预览里拖动', '關鍵影格:從這裡角色平滑移到新位置 — 然後在預覽裡拖曳',
   '키프레임: 여기서부터 캐릭터가 새 자리로 부드럽게 이동 — 그다음 미리보기에서 끌어 옮기세요', 'Khung hình khóa: từ đây nhân vật lướt sang chỗ mới — rồi kéo họ trong bản xem trước',
   'Keyframe: dari sini karakter meluncur ke tempat baru — lalu seret di pratinjau', 'คีย์เฟรม: จากตรงนี้ตัวละครจะเลื่อนไปที่ใหม่อย่างนุ่มนวล — แล้วลากในพรีวิว']),
 ('Сначала покажи персонажа в этой сцене — ключевой кадр двигает его с места',
  ['Show the character in this scene first — a keyframe moves them from where they stand', 'Спершу покажи персонажа в цій сцені — ключовий кадр рухає його з місця',
   'Спачатку пакажы персанажа ў гэтай сцэне — ключавы кадр рухае яго з месца', 'Алдымен кейіпкерді осы сахнада көрсет — кілт кадр оны орнынан жылжытады',
   'Najpierw pokaż postać w tej scenie — klatka kluczowa przesuwa ją z miejsca', 'Nejdřív postavu v této scéně ukaž — klíčový snímek ji posouvá z místa',
   'Zeig die Figur zuerst in dieser Szene — ein Schlüsselbild bewegt sie von ihrem Platz', 'Montre d’abord le personnage dans cette scène — une image clé le déplace de sa place',
   'Primero muestra al personaje en esta escena — un fotograma clave lo mueve de su sitio', 'Primeiro mostre o personagem nesta cena — o quadro-chave o move de onde está',
   'Prima mostra il personaggio in questa scena — un fotogramma chiave lo sposta da dov’è', 'Önce karakteri bu sahnede göster — anahtar kare onu yerinden oynatır',
   'まずこのシーンでキャラを表示してね — キーフレームはそこから動かします', '先在这个场景里显示角色 — 关键帧会把它从原位移走', '先在這個場景裡顯示角色 — 關鍵影格會把它從原位移走',
   '먼저 이 장면에 캐릭터를 보여 주세요 — 키프레임은 그 자리에서 움직여요', 'Hãy cho nhân vật xuất hiện trong cảnh này trước — khung hình khóa di chuyển họ từ chỗ đang đứng',
   'Tampilkan dulu karakternya di adegan ini — keyframe menggesernya dari tempatnya', 'แสดงตัวละครในฉากนี้ก่อน — คีย์เฟรมจะขยับจากที่ที่ยืนอยู่']),
 ('Ключевой кадр: персонаж плавно переедет. Тащи его в превью — место поменяется на этой строке',
  ['Keyframe: the character will glide over. Drag them in the preview — the place changes on this line', 'Ключовий кадр: персонаж плавно переїде. Тягни його в превʼю — місце зміниться в цьому рядку',
   'Ключавы кадр: персанаж плаўна пераедзе. Цягні яго ў прэв’ю — месца зменіцца ў гэтым радку', 'Кілт кадр: кейіпкер баяу жылжиды. Оны превьюде сүйре — орын осы жолда өзгереді',
   'Klatka kluczowa: postać płynnie przejedzie. Przeciągnij ją w podglądzie — miejsce zmieni się w tej linijce', 'Klíčový snímek: postava plynule přejede. Táhni ji v náhledu — místo se změní na tomto řádku',
   'Schlüsselbild: die Figur gleitet hinüber. Zieh sie in der Vorschau — der Platz ändert sich in dieser Zeile', 'Image clé : le personnage va glisser. Déplace-le dans l’aperçu — la place change sur cette ligne',
   'Fotograma clave: el personaje se deslizará. Arrástralo en la vista previa — el sitio cambia en esta línea', 'Quadro-chave: o personagem vai deslizar. Arraste-o na prévia — o lugar muda nesta linha',
   'Fotogramma chiave: il personaggio scivolerà. Trascinalo nell’anteprima — il posto cambia in questa riga', 'Anahtar kare: karakter süzülerek geçecek. Önizlemede sürükle — yeri bu satırda değişir',
   'キーフレーム:キャラがなめらかに移動します。プレビューでドラッグすると、この行の位置が変わります', '关键帧:角色会平滑移动。在预览里拖动它 — 这一行的位置会改变', '關鍵影格:角色會平滑移動。在預覽裡拖曳它 — 這一行的位置會改變',
   '키프레임: 캐릭터가 부드럽게 이동해요. 미리보기에서 끌면 이 줄의 자리가 바뀌어요', 'Khung hình khóa: nhân vật sẽ lướt sang. Kéo họ trong bản xem trước — vị trí đổi ở dòng này',
   'Keyframe: karakter akan meluncur. Seret di pratinjau — tempatnya berubah di baris ini', 'คีย์เฟรม: ตัวละครจะเลื่อนไปอย่างนุ่มนวล ลากในพรีวิว — ตำแหน่งจะเปลี่ยนในบรรทัดนี้']),
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
