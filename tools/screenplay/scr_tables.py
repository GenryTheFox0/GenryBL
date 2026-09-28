# -*- coding: utf-8 -*-
"""GenryBL V1 «пиши как сценарий» - dictionaries (prototype = source of truth for ScreenplayTables.inc).
Every word is matched after norm(): lower-case, ё->е. A key ending in '*' is a prefix (stem), else exact word.
"""

# ---------------------------------------------------------------- emotions: concept -> candidate chain
# The resolver walks the chain and takes the first attribute the character really has (es_catalog.json).
CHAINS = {
    'normal':     ['normal'],
    'smile':      ['smile', 'normal_smile', 'smile2', 'happy', 'normal'],
    'smile2':     ['smile2', 'normal_smile', 'smile', 'normal'],
    'happy':      ['happy', 'smile', 'laugh', 'normal'],
    'laugh':      ['laugh', 'laugh2', 'happy', 'smile', 'normal'],
    'grin':       ['grin', 'smile2', 'smile', 'normal'],
    'evil':       ['evil_smile', 'grin', 'smile', 'normal'],
    'angry':      ['angry', 'rage', 'dontlike', 'upset', 'normal'],
    'rage':       ['rage', 'angry', 'normal'],
    'dontlike':   ['dontlike', 'upset', 'bukal', 'angry2', 'angry', 'normal'],
    'upset':      ['upset', 'bukal', 'sad', 'dontlike', 'normal'],
    'pout':       ['bukal', 'upset', 'dontlike', 'angry', 'normal'],
    'sad':        ['sad', 'upset', 'guilty', 'normal'],
    'cry':        ['cry', 'cry2', 'cry_smile', 'sad', 'upset', 'normal'],
    'crysmile':   ['cry_smile', 'cry', 'smile', 'normal'],
    'shy':        ['shy', 'shy2', 'guilty', 'normal_smile', 'smile', 'normal'],
    'guilty':     ['guilty', 'shy', 'sad', 'upset', 'normal'],
    'surprise':   ['surprise', 'surp1', 'surprise2', 'shocked', 'normal'],
    'shocked':    ['shocked', 'surp3', 'surp2', 'surprise', 'scared', 'normal'],
    'scared':     ['scared', 'fear', 'shocked', 'surp2', 'surprise', 'normal'],
    'serious':    ['serious', 'calml', 'normal'],
    'tender':     ['tender', 'smile2', 'smile', 'normal'],
    'thoughtful': ['serious', 'normal'],
    'fingal':     ['fingal', 'sad', 'normal'],
}
# «очень злая» -> rage, «слегка улыбается» -> smile2
STRONGER = {'angry': 'rage', 'smile': 'happy', 'sad': 'cry', 'surprise': 'shocked', 'dontlike': 'angry', 'upset': 'sad'}
WEAKER = {'smile': 'smile2', 'happy': 'smile', 'laugh': 'smile', 'rage': 'angry', 'angry': 'dontlike', 'cry': 'sad', 'shocked': 'surprise'}
STRONG_WORDS = ['очень', 'сильно', 'крайне', 'жутко', 'ужасно', 'страшно', 'дико', 'совсем', 'безумно', 'чертовски']
WEAK_WORDS = ['слегка', 'чуть', 'чуть-чуть', 'немного', 'едва', 'еле', 'несколько', 'полу*', 'слабо']
NEGATE = ['не', 'ни', 'без', 'нисколько']

EMO_WORDS = [
    # normal
    ('спокойн*', 'normal'), ('невозмут*', 'normal'), ('нейтральн*', 'normal'), ('равнодуш*', 'normal'),
    ('безразлич*', 'normal'), ('обычн*', 'normal'), ('ровно', 'normal'), ('сонн*', 'normal'), ('устал*', 'normal'),
    ('зева*', 'normal'), ('зевнул*', 'normal'), ('буднич*', 'normal'),
    # smile
    ('улыб*', 'smile'), ('заулыб*', 'smile'), ('приветлив*', 'smile'), ('дружелюб*', 'smile'), ('добродуш*', 'smile'),
    ('мило', 'smile'), ('довольн*', 'smile'), ('полуулыб*', 'smile2'),
    # happy
    ('радост*', 'happy'), ('рад', 'happy'), ('рада', 'happy'), ('радуе*', 'happy'), ('обрадов*', 'happy'), ('счастлив*', 'happy'),
    ('весел*', 'happy'), ('восторг*', 'happy'), ('восторж*', 'happy'), ('воодушев*', 'happy'), ('сияет', 'happy'),
    ('сияя', 'happy'), ('сияющ*', 'happy'), ('ликуе*', 'happy'), ('ликуя', 'happy'), ('в восторге', 'happy'),
    # laugh
    ('смеет*', 'laugh'), ('смеют*', 'laugh'), ('смеялс*', 'laugh'), ('смеял*', 'laugh'), ('смеясь', 'laugh'), ('смех*', 'laugh'), ('со смехом', 'laugh'), ('рассмея*', 'laugh'),
    ('засмея*', 'laugh'), ('хохоч*', 'laugh'), ('хохот*', 'laugh'), ('хихик*', 'laugh'), ('прыска*', 'laugh'),
    ('прыснул*', 'laugh'), ('ржет', 'laugh'), ('смешлив*', 'laugh'),
    # grin
    ('ухмыл*', 'grin'), ('хмык*', 'grin'), ('усмех*', 'grin'), ('усмешк*', 'grin'), ('хитр*', 'grin'), ('ехидн*', 'grin'), ('лукав*', 'grin'),
    ('нахальн*', 'grin'), ('дерзк*', 'grin'), ('дерзит', 'grin'), ('самодовол*', 'grin'), ('насмешлив*', 'grin'),
    ('игрив*', 'grin'), ('озорн*', 'grin'), ('подмиг*', 'grin'), ('язвительн*', 'grin'), ('с вызовом', 'grin'),
    ('ехидничае*', 'grin'), ('издевательск*', 'grin'),
    # evil
    ('зловещ*', 'evil'), ('коварн*', 'evil'), ('злорад*', 'evil'), ('недобр*', 'evil'), ('маниакальн*', 'evil'),
    # angry
    ('злая', 'angry'), ('злой', 'angry'), ('злые', 'angry'), ('зло', 'angry'), ('злится', 'angry'), ('злюсь', 'angry'),
    ('злишься', 'angry'), ('злясь', 'angry'), ('злоб*', 'angry'), ('злющ*', 'angry'), ('разозл*', 'angry'), ('озлоб*', 'angry'),
    ('злост*', 'angry'), ('со злостью', 'angry'), ('сердит*', 'angry'), ('серчае*', 'angry'), ('рассерд*', 'angry'),
    ('раздраж*', 'angry'), ('возмущ*', 'angry'), ('гнев*', 'angry'), ('негоду*', 'angry'), ('сквозь зубы', 'angry'),
    ('огрыза*', 'angry'), ('огрызнул*', 'angry'), ('вспыл*', 'angry'),
    # rage
    ('ярост*', 'rage'), ('в ярости', 'rage'), ('взбеш*', 'rage'), ('бешен*', 'rage'), ('в бешенстве', 'rage'),
    ('рассвиреп*', 'rage'), ('свиреп*', 'rage'), ('вне себя', 'rage'), ('рвет и мечет', 'rage'),
    # dontlike
    ('недовол*', 'dontlike'), ('фырка*', 'dontlike'), ('фыркнул*', 'dontlike'), ('морщ*', 'dontlike'), ('поморщ*', 'dontlike'),
    ('скривил*', 'dontlike'), ('кривит*', 'dontlike'), ('брезглив*', 'dontlike'), ('отвращ*', 'dontlike'),
    ('скептич*', 'dontlike'), ('сомнен*', 'dontlike'), ('хмур*', 'dontlike'), ('нахмур*', 'dontlike'),
    ('исподлобья', 'dontlike'), ('закатыва*', 'dontlike'), ('буркнул*', 'dontlike'), ('бурчит', 'dontlike'),
    ('ворчит', 'dontlike'), ('ворчлив*', 'dontlike'), ('проворчал*', 'dontlike'), ('кисло', 'dontlike'),
    # upset
    ('расстро*', 'upset'), ('огорч*', 'upset'), ('обиж*', 'upset'), ('обид*', 'upset'), ('разочаров*', 'upset'),
    ('досадой', 'upset'), ('досадлив*', 'upset'), ('с досадой', 'upset'),
    # pout
    ('дует*', 'pout'), ('дуется', 'pout'), ('надул*', 'pout'), ('насуп*', 'pout'), ('бука', 'pout'), ('букой', 'pout'),
    ('набычил*', 'pout'),
    # sad
    ('груст*', 'sad'), ('печал*', 'sad'), ('опечал*', 'sad'), ('тоск*', 'sad'), ('уныл*', 'sad'), ('вздыха*', 'sad'),
    ('вздохнул*', 'sad'), ('вздох*', 'sad'), ('со вздохом', 'sad'), ('подавлен*', 'sad'), ('удруч*', 'sad'), ('понур*', 'sad'),
    ('потерянн*', 'sad'), ('горестн*', 'sad'),
    # cry
    ('плач*', 'cry'), ('плача', 'cry'), ('заплак*', 'cry'), ('заплаканн*', 'cry'), ('слез*', 'cry'), ('в слезах', 'cry'),
    ('рыда*', 'cry'), ('зарыда*', 'cry'), ('всхлип*', 'cry'), ('хныч*', 'cry'), ('реве*', 'cry'), ('разревел*', 'cry'),
    # crysmile
    ('растрог*', 'crysmile'), ('сквозь слезы', 'crysmile'),
    # shy
    ('смущ*', 'shy'), ('смутил*', 'shy'), ('смутивш*', 'shy'), ('красне*', 'shy'), ('покрасне*', 'shy'), ('зарде*', 'shy'),
    ('застенч*', 'shy'), ('робк*', 'shy'), ('робко', 'shy'), ('робе*', 'shy'), ('оробе*', 'shy'), ('стесня*', 'shy'),
    ('стеснит*', 'shy'), ('потуп*', 'shy'), ('неловк*', 'shy'), ('стыдлив*', 'shy'), ('скромн*', 'shy'), ('мнется', 'shy'),
    ('неуверенн*', 'shy'), ('нерешительн*', 'shy'), ('запинаясь', 'shy'), ('заика*', 'shy'), ('зардевшись', 'shy'),
    # guilty
    ('винова*', 'guilty'), ('пристыж*', 'guilty'), ('стыд*', 'guilty'), ('извиняющ*', 'guilty'), ('извиня*', 'guilty'),
    ('кается', 'guilty'), ('раская*', 'guilty'), ('повинн*', 'guilty'),
    # surprise
    ('удивл*', 'surprise'), ('удивит*', 'surprise'), ('удивив*', 'surprise'), ('изумл*', 'surprise'), ('изумит*', 'surprise'),
    ('недоум*', 'surprise'), ('озадач*', 'surprise'), ('опеш*', 'surprise'), ('растерян*', 'surprise'),
    ('неожиданн*', 'surprise'), ('вскинув брови', 'surprise'),
    # shocked
    ('шок*', 'shocked'), ('в шоке', 'shocked'), ('ошелом*', 'shocked'), ('ошараш*', 'shocked'), ('остолбен*', 'shocked'),
    ('пораж*', 'shocked'), ('обомле*', 'shocked'), ('оторопе*', 'shocked'), ('потрясен*', 'shocked'), ('ступор*', 'shocked'),
    ('обалде*', 'shocked'), ('офиге*', 'shocked'), ('в ступоре', 'shocked'),
    # scared
    ('испуг*', 'scared'), ('испуж*', 'scared'), ('страх*', 'scared'), ('со страхом', 'scared'), ('боит*', 'scared'),
    ('боясь', 'scared'), ('боязлив*', 'scared'), ('ужас*', 'scared'), ('в ужасе', 'scared'), ('паник*', 'scared'),
    ('в панике', 'scared'), ('дрож*', 'scared'), ('трясет*', 'scared'), ('напуган*', 'scared'), ('перепуг*', 'scared'),
    ('в страхе', 'scared'), ('опасливо', 'scared'), ('тревож*', 'scared'), ('встревож*', 'scared'),
    # serious
    ('серьезн*', 'serious'), ('строг*', 'serious'), ('сосредоточ*', 'serious'), ('решительн*', 'serious'),
    ('тверд*', 'serious'), ('деловит*', 'serious'), ('собранн*', 'serious'), ('сухо', 'serious'), ('холодно', 'serious'),
    ('официальн*', 'serious'), ('властн*', 'serious'), ('командн*', 'serious'), ('назидательн*', 'serious'),
    ('наставительн*', 'serious'), ('важно', 'serious'), ('формальн*', 'serious'),
    # tender
    ('нежн*', 'tender'), ('ласков*', 'tender'), ('тепло', 'tender'), ('мягко', 'tender'), ('заботлив*', 'tender'),
    ('умил*', 'tender'), ('влюблен*', 'tender'), ('мечтательн*', 'tender'), ('с нежностью', 'tender'),
    # thoughtful
    ('задумч*', 'thoughtful'), ('задумал*', 'thoughtful'), ('задумавш*', 'thoughtful'), ('размышля*', 'thoughtful'),
    ('рассеян*', 'thoughtful'), ('в раздумье', 'thoughtful'), ('раздумье*', 'thoughtful'),
    # fingal
    ('фингал*', 'fingal'), ('с фингалом', 'fingal'), ('синяк*', 'fingal'), ('побит*', 'fingal'),
]

# ---------------------------------------------------------------- overlays drawn over the sprite (Overlays.cpp)
OV_WORDS = [
    ('румян*', 'blush'), ('зарумян*', 'blush'), ('пунцов*', 'blush'), ('залил* краской', 'blush'), ('краснющ*', 'blush'),
    ('вспоте*', 'sweat'), ('потн*', 'sweat'), ('в поту', 'sweat'), ('запыха*', 'sweat'), ('обливаясь потом', 'sweat'),
    ('взмок*', 'sweat'),
    ('мокр*', 'wet'), ('промок*', 'wet'), ('вымок*', 'wet'), ('насквозь', 'wet'), ('после купания', 'wet'),
    ('после душа', 'wet'), ('из воды', 'wet'), ('с мокрыми волосами', 'wet'),
    ('слезинк*', 'tears'), ('со слезами на глазах', 'tears'), ('глаза на мокром месте', 'tears'), ('слезы на глазах', 'tears'),
]
BLUSH_STEMS = ['красне', 'покрасне', 'зарде', 'раскрасне']   # «краснеет» = shy AND the blush

# ---------------------------------------------------------------- outfits / accessories / distance
# outfit groups: inside a group the resolver swaps freely (dv/un "body" looks like a swimsuit)
OUTFIT_GROUPS = {'swim': ['swim', 'body'], 'body': ['body', 'swim'], 'dress': ['dress'], 'sport': ['sport'],
                 'pioneer': ['pioneer', 'pioneer2'], 'pioneer2': ['pioneer2', 'pioneer']}
OUTFIT_WORDS = [
    ('купальник*', 'swim'), ('купальн*', 'swim'), ('бикини', 'swim'), ('плавк*', 'swim'), ('пляжн*', 'swim'),
    ('бель*', 'body'), ('нижнем белье', 'body'), ('раздет*', 'body'), ('голая', 'body'), ('голый', 'body'), ('голой', 'body'),
    ('голышом', 'body'), ('обнажен*', 'body'), ('без одежды', 'body'), ('полотенц*', 'body'), ('неглиже', 'body'),
    ('плать*', 'dress'), ('сарафан*', 'dress'), ('нарядн*', 'dress'), ('праздничн*', 'dress'), ('вечернем платье', 'dress'),
    ('спортивн*', 'sport'), ('спортивк*', 'sport'), ('трико', 'sport'), ('треник*', 'sport'), ('физкульт*', 'sport'),
    ('шорт*', 'sport'), ('футболк*', 'sport'),
    ('форм*', 'pioneer'), ('пионерск*', 'pioneer'), ('пионерк*', 'pioneer'), ('галстук*', 'pioneer'), ('рубашк*', 'pioneer'),
    ('форма2', 'pioneer2'), ('расстегнут*', 'pioneer2'), ('завязан*', 'pioneer2'), ('небрежн*', 'pioneer2'),
]
OUTFIT_PRIORITY = ['body', 'swim', 'dress', 'sport', 'pioneer2', 'pioneer']   # «в спортивной форме» = sport, not pioneer
ACC_WORDS = [('панам*', 'panama'), ('очк*', 'glasses'), ('стетоскоп*', 'stethoscope'), ('фонендоскоп*', 'stethoscope')]
DIST_WORDS = [('близко', 'close'), ('вблизи', 'close'), ('вплотную', 'close'), ('крупно', 'close'),
              ('крупным планом', 'close'), ('лицом к лицу', 'close'), ('наклоняет*', 'close'), ('ближе', 'close'),
              ('вдали', 'far'), ('вдалеке', 'far'), ('издали', 'far'), ('издалека', 'far'), ('поодаль', 'far'),
              ('далеко', 'far'), ('на расстоянии', 'far'),
              ('отходит', 'normal'), ('отступает', 'normal'), ('отошл*', 'normal'), ('отошел', 'normal')]
DEFAULT_LOOK = {   # tag -> (outfit, acc) the game itself uses most (scenario .rpyc statistics)
    'dv': ('pioneer', ''), 'sl': ('pioneer', ''), 'un': ('pioneer', ''), 'us': ('pioneer', ''), 'mi': ('pioneer', ''),
    'mt': ('pioneer', ''), 'el': ('pioneer', ''), 'sh': ('pioneer', ''), 'mz': ('pioneer', 'glasses'),
    'uv': ('', ''), 'cs': ('', ''), 'pi': ('', ''),
}

# ---------------------------------------------------------------- positions (phrases, longest first)
POS_PHRASES = [
    ('у левого края', 'fleft'), ('с левого края', 'fleft'), ('далеко слева', 'fleft'), ('с краю слева', 'fleft'),
    ('слева с краю', 'fleft'), ('крайняя слева', 'fleft'), ('крайний слева', 'fleft'), ('совсем слева', 'fleft'),
    ('в левом углу', 'fleft'),
    ('у правого края', 'fright'), ('с правого края', 'fright'), ('далеко справа', 'fright'), ('с краю справа', 'fright'),
    ('справа с краю', 'fright'), ('крайняя справа', 'fright'), ('крайний справа', 'fright'), ('совсем справа', 'fright'),
    ('в правом углу', 'fright'),
    ('левее центра', 'cleft'), ('слева от центра', 'cleft'), ('чуть левее', 'cleft'), ('немного левее', 'cleft'),
    ('ближе к центру слева', 'cleft'), ('левее', 'cleft'),
    ('правее центра', 'cright'), ('справа от центра', 'cright'), ('чуть правее', 'cright'), ('немного правее', 'cright'),
    ('ближе к центру справа', 'cright'), ('правее', 'cright'),
    ('в центре', 'center'), ('по центру', 'center'), ('посередине', 'center'), ('в середине', 'center'),
    ('посредине', 'center'), ('центр', 'center'),
    ('слева', 'left'), ('справа', 'right'),
    ('fleft', 'fleft'), ('fright', 'fright'), ('cleft', 'cleft'), ('cright', 'cright'), ('left', 'left'),
    ('right', 'right'), ('center', 'center'), ('truecenter', 'truecenter'),
]
DIRECTION_WORDS = [('налево', 'left'), ('влево', 'left'), ('слева', 'left'), ('направо', 'right'), ('вправо', 'right'),
                   ('справа', 'right')]

# ---------------------------------------------------------------- stage verbs
ENTER_WORDS = ['входит', 'вошла', 'вошел', 'вход*', 'заходит', 'зашла', 'зашел', 'появля*', 'появил*', 'подходит',
               'подошла', 'подошел', 'прибега*', 'прибежал*', 'врыва*', 'ворвал*', 'влета*', 'влетел*', 'возвраща*',
               'вернул*', 'выбега*', 'выбежал*', 'выглядыва*', 'выглянул*']
EXIT_WORDS = ['уходит', 'уход*', 'ушла', 'ушел', 'ушли', 'уходя', 'убега*', 'убежал*', 'выходит', 'вышла', 'вышел',
              'скрыва*', 'скрыл*', 'исчеза*', 'исчезл*', 'исчез', 'пропада*', 'пропал*', 'удаля*', 'удалил*',
              'покида*', 'покинул*', 'уносит*', 'уехал*', 'уезжа*', 'сбега*', 'сбежал*']
FAST_EXIT = ['убега*', 'убежал*', 'сбега*', 'сбежал*', 'прибега*', 'прибежал*', 'врыва*', 'ворвал*', 'влета*',
             'влетел*', 'выбега*', 'выбежал*']
OFFSCREEN = ['за кадром', 'з.к.', 'зк', 'з/к', 'голос за кадром', 'из-за двери', 'из-за кадра', 'по телефону', 'v.o.',
             'o.s.', 'vo', 'os', 'off', 'не видно', 'издалека кричит']
THOUGHT = ['про себя', 'мысленно', 'думает', 'думаю', 'в мыслях', 'мысли', 'мысль', 'подумал*']
PUNCH = ['крич*', 'крикнул*', 'закричал*', 'орет', 'вопит', 'завопил*', 'рявка*', 'рявкнул*', 'во весь голос', 'гаркнул*',
         'выкрикива*', 'выкрикнул*']
IGNORE = ['шепотом', 'шепчет', 'прошептал*', 'тихо', 'тихонько', 'громко', 'негромко', 'вполголоса', 'быстро',
          'медленно', 'продолжая', 'прод.', "cont'd", 'contd', 'в сторону', 'нараспев', 'сквозь сон',
          # gestures and speech verbs of «— Привет, — сказала Алиса.»: understood, no sprite change
          'маш*', 'махнул*', 'рукой', 'руками', 'кива*', 'кивнул*', 'пожима*', 'пожал*', 'плечами', 'головой', 'кача*',
          'покачал*', 'отворачива*', 'отвернул*', 'садится', 'села', 'сел', 'вста*', 'смотрит', 'глядя', 'глядит',
          'посмотрел*', 'сказал*', 'говор*', 'ответил*', 'отвеча*', 'спросил*', 'спрашива*', 'произн*', 'добавил*',
          'добавля*', 'бросил*', 'броса*', 'сообщил*', 'заметил*', 'уточнил*', 'повторил*', 'продолжил*', 'протянул*',
          'шепнул*', 'пробормотал*', 'бормоч*', 'воскликнул*', 'восклица*', 'позвал*', 'зовет', 'перебил*', 'перебива*',
          'отозвал*', 'откликнул*', 'пояснил*', 'объяснил*', 'объясня*', 'признал*', 'призна*', 'согласил*', 'возразил*',
          'предложил*', 'напомнил*', 'поинтересовал*', 'выдохнул*', 'выдавил*', 'пропел*', 'промурлыкал*']

# ---------------------------------------------------------------- scene headings
KIND_RE = r'(?:ИНТ\.?\s*/\s*НАТ|НАТ\.?\s*/\s*ИНТ|ИНТ|НАТ|INT\.?\s*/\s*EXT|EXT\.?\s*/\s*INT|INT|EXT|I/E|ЭКСТ)'
TIME_WORDS = [
    ('раннее утро', 'day'), ('утро', 'day'), ('утром', 'day'), ('день', 'day'), ('днем', 'day'), ('полдень', 'day'),
    ('после обеда', 'day'), ('обед', 'day'), ('morning', 'day'), ('day', 'day'), ('noon', 'day'),
    ('поздний вечер', 'night'), ('вечер', 'sunset'), ('вечером', 'sunset'), ('закат', 'sunset'), ('на закате', 'sunset'),
    ('сумерки', 'sunset'), ('в сумерках', 'sunset'), ('рассвет', 'sunset'), ('на рассвете', 'sunset'),
    ('evening', 'sunset'), ('sunset', 'sunset'), ('dusk', 'sunset'), ('dawn', 'sunset'),
    ('ночь', 'night'), ('ночью', 'night'), ('полночь', 'night'), ('глубокая ночь', 'night'), ('night', 'night'),
    ('пролог', 'prolog'),
    ('позже', 'same'), ('чуть позже', 'same'), ('позднее', 'same'), ('то же время', 'same'), ('непрерывно', 'same'),
    ('продолжение', 'same'), ('тогда же', 'same'), ('later', 'same'), ('continuous', 'same'), ('same', 'same'),
]
TIME_RU = {'day': 'день', 'sunset': 'вечер', 'night': 'ночь', 'prolog': 'пролог'}
BLACK_WORDS = ['затемнение', 'затемнение.', 'темнота', 'черный экран', 'чёрный экран', 'fade out', 'fade out.',
               'fade to black', 'fade to black.', 'кромешная тьма']

# place key -> (default kind, ext base, int base); a base maps to the bg variants in es_catalog.json
PLACES = {
    'camp_entrance':  ('ext', 'ext_camp_entrance', None),
    'bus':            ('ext', 'ext_bus', 'int_bus'),
    'bus_people':     ('int', None, 'int_bus_people'),
    'no_bus':         ('ext', 'ext_no_bus', None),
    'city_stop':      ('ext', 'bus_stop', None),
    'liaz':           ('int', None, 'int_liaz'),
    'square':         ('ext', 'ext_square', None),
    'party':          ('ext', 'ext_square_night_party', None),
    'dining':         ('int', 'ext_dining_hall_near', 'int_dining_hall'),
    'dining_people':  ('int', 'ext_dining_hall_near', 'int_dining_hall_people'),
    'dining_away':    ('ext', 'ext_dining_hall_away', 'int_dining_hall'),
    'house_mt':       ('int', 'ext_house_of_mt', 'int_house_of_mt'),
    'house_dv':       ('int', 'ext_house_of_dv', 'int_house_of_dv'),
    'house_sl':       ('int', 'ext_house_of_sl', 'int_house_of_sl'),
    'house_un':       ('int', 'ext_house_of_un', 'int_house_of_un'),
    'houses':         ('ext', 'ext_houses', None),
    'aidpost':        ('int', 'ext_aidpost', 'int_aidpost'),
    'library':        ('int', 'ext_library', 'int_library'),
    'musclub':        ('int', 'ext_musclub', 'int_musclub'),
    'clubs':          ('int', 'ext_clubs', 'int_clubs_male'),
    'boathouse':      ('ext', 'ext_boathouse', None),
    'beach':          ('ext', 'ext_beach', None),
    'island':         ('ext', 'ext_island', None),
    'polyana':        ('ext', 'ext_polyana', None),
    'forest':         ('ext', 'ext_path', None),
    'forest_deep':    ('ext', 'ext_path2', None),
    'road':           ('ext', 'ext_road', None),
    'washstand':      ('ext', 'ext_washstand', None),
    'playground':     ('ext', 'ext_playground', None),
    'stage':          ('ext', 'ext_stage_normal', None),
    'stage_big':      ('ext', 'ext_stage_big', None),
    'bathhouse':      ('ext', 'ext_bathhouse', None),
    'old_building':   ('ext', 'ext_old_building', 'int_old_building'),
    'mine':           ('int', None, 'int_mine'),
    'catacombs':      ('int', None, 'int_catacombs_entrance'),
    'catacombs_room': ('int', None, 'int_catacombs_living'),
    'semen_room':     ('int', None, 'semen_room'),
    'city':           ('ext', 'ext_square_day_city', None),
}
PLACE_WORDS = [   # (phrase with stems, place key); longest phrase wins
    ('вход в лагерь', 'camp_entrance'), ('ворот*', 'camp_entrance'), ('кпп', 'camp_entrance'), ('въезд*', 'camp_entrance'),
    ('у лагеря', 'camp_entrance'),
    ('автобус* с пионер*', 'bus_people'), ('полн* автобус*', 'bus_people'), ('автобус* с людьми', 'bus_people'),
    ('автобус*', 'bus'), ('икарус*', 'bus'), ('лиаз*', 'liaz'), ('городск* автобус*', 'liaz'),
    ('городск* остановк*', 'city_stop'), ('зимн* остановк*', 'city_stop'), ('остановк*', 'no_bus'),
    ('площад*', 'square'), ('генд*', 'square'), ('памятник*', 'square'), ('дискотек*', 'party'), ('танц*', 'party'),
    ('вид на столов*', 'dining_away'), ('столов* издалека', 'dining_away'), ('у столов*', 'dining'),
    ('столов* полн*', 'dining_people'), ('полн* столов*', 'dining_people'), ('людн* столов*', 'dining_people'),
    ('столов* с людьми', 'dining_people'), ('столов*', 'dining'), ('буфет*', 'dining'),
    ('домик* вожат*', 'house_mt'), ('домик* ольг*', 'house_mt'), ('домик* од', 'house_mt'), ('домик* семен*', 'house_mt'),
    ('наш* домик*', 'house_mt'), ('мой домик', 'house_mt'), ('моем домике', 'house_mt'), ('вожат*', 'house_mt'),
    ('домик* алис*', 'house_dv'), ('домик* двачевск*', 'house_dv'), ('домик* ульян*', 'house_dv'),
    ('домик* слав*', 'house_sl'), ('домик* жен*', 'house_sl'),
    ('домик* лен*', 'house_un'), ('домик* мику', 'house_un'),
    ('домики', 'houses'), ('домиков', 'houses'), ('жил* корпус*', 'houses'), ('аллея', 'houses'), ('аллее', 'houses'),
    ('медпункт*', 'aidpost'), ('лазарет*', 'aidpost'), ('медсестр*', 'aidpost'), ('медпост*', 'aidpost'),
    ('библиотек*', 'library'), ('читальн*', 'library'),
    ('музклуб*', 'musclub'), ('музыкальн* клуб*', 'musclub'), ('музкружок*', 'musclub'), ('клуб* музык*', 'musclub'),
    ('клуб* кибернет*', 'clubs'), ('кибернет*', 'clubs'), ('клуб*', 'clubs'), ('кружк*', 'clubs'), ('кружок', 'clubs'),
    ('лодочн*', 'boathouse'), ('причал*', 'boathouse'), ('пристан*', 'boathouse'), ('лодк*', 'boathouse'),
    ('пляж*', 'beach'), ('берег*', 'beach'), ('озер*', 'beach'), ('речк*', 'beach'), ('реке', 'beach'), ('река', 'beach'),
    ('остров*', 'island'),
    ('полян*', 'polyana'),
    ('глуб* лес*', 'forest_deep'), ('чащ*', 'forest_deep'), ('дальн* троп*', 'forest_deep'),
    ('лес', 'forest'), ('лесу', 'forest'), ('леса', 'forest'), ('лесн* троп*', 'forest'), ('тропинк*', 'forest'),
    ('тропа', 'forest'), ('тропе', 'forest'), ('тропу', 'forest'),
    ('дорог*', 'road'), ('шоссе', 'road'),
    ('умывальник*', 'washstand'), ('умывальн*', 'washstand'), ('раковин*', 'washstand'),
    ('спортплощадк*', 'playground'), ('площадк*', 'playground'), ('футбол*', 'playground'), ('стадион*', 'playground'),
    ('поле', 'playground'), ('поля', 'playground'),
    ('больш* сцен*', 'stage_big'), ('концерт*', 'stage_big'), ('сцен*', 'stage'), ('эстрад*', 'stage'),
    ('баня', 'bathhouse'), ('бани', 'bathhouse'), ('бане', 'bathhouse'), ('баню', 'bathhouse'), ('баньк*', 'bathhouse'),
    ('стар* корпус*', 'old_building'), ('стар* лагер*', 'old_building'), ('заброшен*', 'old_building'),
    ('шахт*', 'mine'), ('штольн*', 'mine'),
    ('бомбоубежищ*', 'catacombs_room'), ('убежищ*', 'catacombs_room'), ('катакомб*', 'catacombs'), ('подземель*', 'catacombs'),
    ('квартир*', 'semen_room'), ('комнат* семен*', 'semen_room'), ('реальн* мир*', 'semen_room'),
    ('город*', 'city'),
]

# ---------------------------------------------------------------- speakers the screenplay layer adds (V1 only,
# kSpeakers stays byte-equal to the old builder)
EXTRA_SPEAKERS = {
    'двачевская': 'dv', 'алиска': 'dv', 'ульянка': 'us', 'од': 'mt', 'ольга д.': 'mt', 'сыроежкин': 'el', 'серёжа': 'el',
    'сережа': 'el', 'юлия': 'uv', 'пионер': 'pi', 'лена тихонова': 'un', 'славяна феоктистова': 'sl', 'мику хацунэ': 'mi',
}
CANON_NAME = {   # id -> the name the expander writes in «Имя: текст» (resolves back through kSpeakers)
    'dv': 'Алиса', 'sl': 'Славя', 'un': 'Лена', 'us': 'Ульяна', 'mi': 'Мику', 'mt': 'Ольга Дмитриевна', 'el': 'Электроник',
    'sh': 'Шурик', 'mz': 'Женя', 'uv': 'Юля', 'cs': 'Виола', 'me': 'Семён', 'genry': 'Генри', 'scar': 'Шрам', 'pi': 'pi',
    'th': 'th',
}
