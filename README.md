# MapForge — редактор карт + движок с Lua (C++ / raylib)

Три части:

| Файл | Что делает |
|---|---|
| `src/map.h` | общий формат карты (txt): чтение, запись, границы фигур |
| `src/editor.cpp` | **редактор карт** — расставляет фигуры в 3D, сохраняет в txt |
| `src/engine.cpp` | **движок** — грузит карту, рисует с освещением, игровая логика на Lua |
| `examples/demo/` | пример: карта `map.txt` и игра `game.lua` («собери монеты») |

## Сборка

Нужны CMake ≥ 3.16, компилятор C++17 и git. raylib и Lua скачиваются автоматически.

Linux (один раз): `sudo apt install build-essential cmake git libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`

```bash
cmake -B build
cmake --build build -j
```

Нужен OpenGL 3.3.

## Запуск

```bash
./build/mapforge_editor examples/demo/map.txt
./build/mapforge_engine examples/demo/map.txt examples/demo/game.lua
```

Если файла карты нет, редактор начнёт новую карту и создаст файл по `Ctrl+S`.
В движке **F5** перезагружает карту и скрипт без перезапуска; ошибки Lua показываются красной плашкой внизу.

## Формат карты

```
# type x y z  sx sy sz  r g b  [name]
cube     0 0.5 0   1 1 1   220 70 60   player
sphere   6 2.2 0   0.6 0.6 0.6   240 200 40   coin1
cylinder -8 0.75 6   2 1.5 2   150 100 220
plane    0 0 0   60 0 60   90 160 90   ground
```

`x y z` — центр фигуры. Размеры: `cube` — ширина/высота/глубина; `sphere` — `sx` диаметр; `cylinder` — `sx` диаметр, `sy` высота; `plane` — `sx`, `sz` (лежит на высоте `y`). Имя необязательно, без пробелов — по нему Lua находит объекты.

## Управление редактором

| Действие | Клавиши |
|---|---|
| Камера | ПКМ + движение — вращение, колесо — зум, WASD — сдвиг (Shift — быстрее) |
| Выбрать | ЛКМ по фигуре |
| Поставить | **Shift + ЛКМ** по земле (тип — клавишами 1–4: куб/сфера/цилиндр/плоскость) |
| Двигать | стрелки (по X/Z относительно камеры), R/F — вверх/вниз |
| Размер | Tab — выбрать ось (X/Y/Z/ALL), `+`/`-` — изменить |
| Цвет | C — следующий цвет палитры (применяется к выбранной и новым фигурам) |
| Имя | N — ввести имя, Enter — применить |
| Шаг сетки | G (1 / 0.5 / 0.25 / 0.1) |
| Копия / удалить | Ctrl+D / Delete |
| Сохранить / перечитать | Ctrl+S / Ctrl+O |

## Lua API

Скрипт может определить три функции: `on_start()`, `on_update(dt)`, `on_draw2d()`.
Объекты в Lua — целые `id`.

**Объекты:** `find(name)` → id или nil · `objects()` → таблица всех id · `exists(id)` ·
`get_pos(id)` / `set_pos(id, x,y,z)` · `get_size(id)` / `set_size(id, sx,sy,sz)` ·
`get_color(id)` / `set_color(id, r,g,b)` · `get_type(id)` · `get_name(id)` ·
`spawn(type, x,y,z, sx,sy,sz, r,g,b, name)` → id · `remove(id)` · `overlaps(a, b)` (пересечение ограничивающих коробок)

**Ввод:** `key_down("W")`, `key_pressed("SPACE")` (буквы, цифры, `SPACE ENTER TAB UP DOWN LEFT RIGHT SHIFT CTRL ALT F1..F4`) ·
`mouse_down(1|2|3)`, `mouse_pressed(btn)`, `mouse_delta()` → dx, dy · `lock_mouse(true|false)`

**Сцена и экран:** `camera(px,py,pz, tx,ty,tz [, fov])` · `sky(r,g,b, r,g,b)` (верх и горизонт) · `light(dx,dy,dz)` ·
`draw_text(text, x, y [, size, r,g,b])` (из `on_draw2d`) · `screen_size()` → w, h · `time()` · `screenshot("file.png")` · `quit()`

Обычные библиотеки Lua (`math`, `string`, `table`) доступны, `require("module")` ищет файлы рядом со скриптом.

## Ограничения

- Текст на экране — шрифт raylib по умолчанию, только латиница (кириллица не отображается).
- Физики нет: в примере столкновения и гравитация написаны на Lua через `overlaps`. `overlaps` работает по ограничивающим коробкам (для сферы и цилиндра — по описанной коробке).
- Нет поворота фигур — только позиция, размер, цвет.
