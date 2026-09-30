"""The mod's commands stay Russian in every language (they ARE the language of ES mods), but a person who reads
«конецигры» in English or Japanese must understand it: the first time a message names a command, its meaning
follows in brackets in that language - «конецигры» (end of the game). Run after every translation batch."""
import io, json, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
G = {
    #         конецигры              конецсцены            если         выбор            конецвыбора              переход
    'en':    ['end of the game',     'end of the scene',   'if',        'choice',        'end of the choice',     'go to a scene'],
    'uk':    ['кінець гри',          'кінець сцени',       'якщо',      'вибір',         'кінець вибору',         'перехід до сцени'],
    'be':    ['канец гульні',        'канец сцэны',        'калі',      'выбар',         'канец выбару',          'пераход да сцэны'],
    'kk':    ['ойын соңы',           'сахна соңы',         'егер',      'таңдау',        'таңдау соңы',           'сахнаға өту'],
    'pl':    ['koniec gry',          'koniec sceny',       'jeśli',     'wybór',         'koniec wyboru',         'przejście do sceny'],
    'cs':    ['konec hry',           'konec scény',        'pokud',     'volba',         'konec volby',           'přechod do scény'],
    'de':    ['Spielende',           'Szenenende',         'wenn',      'Auswahl',       'Ende der Auswahl',      'Sprung zur Szene'],
    'fr':    ['fin du jeu',          'fin de la scène',    'si',        'choix',         'fin du choix',          'aller à une scène'],
    'es':    ['fin del juego',       'fin de la escena',   'si',        'elección',      'fin de la elección',    'ir a una escena'],
    'pt_BR': ['fim do jogo',         'fim da cena',        'se',        'escolha',       'fim da escolha',        'ir para uma cena'],
    'it':    ['fine del gioco',      'fine della scena',   'se',        'scelta',        'fine della scelta',     'vai a una scena'],
    'tr':    ['oyunun sonu',         'sahnenin sonu',      'eğer',      'seçim',         'seçimin sonu',          'sahneye geç'],
    'ja':    ['ゲーム終了',          'シーン終了',         'もし',      '選択',          '選択の終わり',          'シーンへ移動'],
    'zh_CN': ['游戏结束',            '场景结束',           '如果',      '选择',          '选择结束',              '跳到场景'],
    'zh_TW': ['遊戲結束',            '場景結束',           '如果',      '選擇',          '選擇結束',              '跳到場景'],
    'ko':    ['게임 종료',           '장면 종료',          '만약',      '선택',          '선택 끝',               '장면으로 이동'],
    'vi':    ['kết thúc trò chơi',   'kết thúc cảnh',      'nếu',       'lựa chọn',      'kết thúc lựa chọn',     'chuyển tới cảnh'],
    'id':    ['akhir permainan',     'akhir adegan',       'jika',      'pilihan',       'akhir pilihan',         'pindah ke adegan'],
    'th':    ['จบเกม',               'จบฉาก',              'ถ้า',       'ตัวเลือก',      'จบตัวเลือก',            'ไปยังฉาก'],
}
CMDS = ['конецигры', 'конецсцены', 'если', 'выбор', 'конецвыбора', 'переход']
changed = 0
for code, gl in G.items():
    p = os.path.join(ROOT, 'data', 'i18n', code + '.json')
    d = json.load(io.open(p, encoding='utf-8'))
    for k, v in d.items():
        if '«фон»' in v:                       # the sentence that explains why the commands stay Russian
            continue
        nv = v
        for cmd, g in zip(CMDS, gl):
            tag = '«' + cmd + '»'
            i = nv.find(tag)
            if i < 0:
                continue
            after = nv[i + len(tag):]
            if after.startswith(' (' + g + ')') or after.startswith('(' + g + ')'):
                continue                       # glossed already
            wide = code in ('ja', 'zh_CN', 'zh_TW')
            nv = nv[:i + len(tag)] + ('(' + g + ')' if wide else ' (' + g + ')') + after
        if nv != v:
            d[k] = nv
            changed += 1
    json.dump(dict(sorted(d.items())), io.open(p, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('glossed', changed, 'strings')
