"""docs/ -> the GitHub Wiki of GenryBL (GenryTheFox0/GenryBL.wiki.git).

The wiki is the same text as docs/, with page names instead of file names and pictures from the repository:
    python tools/docs_to_wiki.py <folder of a clone of the wiki repo>
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = 'https://raw.githubusercontent.com/GenryTheFox0/GenryBL/main/docs/'
PAGES = {                      # docs file -> wiki page name
    'README.md': 'Home',
    'install.md': 'Установка',
    'first-mod.md': 'Первый-мод',
    'commands.md': 'Шпаргалка-команд',
    'characters.md': 'Персонажи-и-гардероб',
    'backgrounds.md': 'Фоны-CG-и-18+',
    'sound.md': 'Музыка-звук-и-видео',
    'choices.md': 'Выборы-очки-и-концовки',
    'phone-map-weather.md': 'Телефон-карта-погода-и-эффекты',
    'screenplay.md': 'Пиши-как-сценарий',
    'cinema-check.md': 'Кино-режим-и-проверка',
    'publish.md': 'Выложить-мод',
    'faq.md': 'Вопросы-и-проблемы',
    'developers.md': 'Для-разработчиков',
}


def convert(text):
    # pictures: from the repository
    text = re.sub(r'(!\[[^\]]*\]\()(images/[^)]+)\)', lambda m: m.group(1) + RAW + m.group(2) + ')', text)
    text = re.sub(r'src="(images/[^"]+)"', lambda m: 'src="' + RAW + m.group(1) + '"', text)
    # links between pages: page names
    for f, page in PAGES.items():
        text = re.sub(r'\]\(' + re.escape(f) + r'(#[^)]*)?\)', lambda m, p=page: '](' + p + (m.group(1) or '') + ')', text)
    return text


def main():
    out = sys.argv[1]
    for f, page in PAGES.items():
        src = io.open(os.path.join(ROOT, 'docs', f), encoding='utf-8').read()
        io.open(os.path.join(out, page + '.md'), 'w', encoding='utf-8', newline='\n').write(convert(src))
    side = ['**[Главная](Home)**', '']
    for f, page in list(PAGES.items())[1:]:
        side.append('- [' + page.replace('-', ' ') + '](' + page + ')')
    side += ['', '[Скачать GenryBL](https://github.com/GenryTheFox0/GenryBL/releases/latest)']
    io.open(os.path.join(out, '_Sidebar.md'), 'w', encoding='utf-8', newline='\n').write('\n'.join(side) + '\n')
    print('wiki pages:', len(PAGES), '+ sidebar ->', out)


if __name__ == '__main__':
    main()
