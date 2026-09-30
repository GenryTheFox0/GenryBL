# Мастерская GenryBL — каталог

`mods.json` — всё, что видно во вкладке «Мастерская GenryBL» у каждого, кто открыл GenryBL.
Сервер не нужен: GenryBL читает этот файл с GitHub (через jsDelivr, он доходит туда, где режется
raw.githubusercontent). Правка файла на GitHub = у всех новый каталог через минуты.

Мод в каталоге бывает двух видов.

**Мод из Мастерской Steam** — кнопка «Подписаться», Steam скачает его сам:

```json
{ "id": "genry_2_mod", "title": "Название", "author": "GenryTheFox",
  "workshop": "3755737355", "cover": "https://…/cover.jpg", "size": 120000000 }
```

**Архив** — кнопка «Установить», GenryBL скачает ZIP и положит мод в `game/mods`:

```json
{ "id": "genry_leto", "title": "Название", "author": "Автор", "version": "1.0",
  "zip": "https://github.com/GenryTheFox0/GenryBL/releases/download/mods/genry_leto.zip",
  "folder": "genry_leto", "cover": "https://…/cover.jpg", "size": 80000000,
  "page": "https://…" }
```

ZIP — это «Экспорт → Архив для игроков» из GenryBL (внутри папка мода и «КАК_УСТАНОВИТЬ.txt»).
Его удобно класть в релиз `mods` на GitHub. `folder` — имя папки мода: GenryBL обновляет только ту папку,
которую сам поставил из каталога, чужие не трогает.

Предложения модов приходят в Issues (кнопка «Предложить свой мод» в GenryBL).
