# Slice Chaser — FFGL-эффект чейсера / строба для Resolume Arena

Эффект (Effects → **Slice Chaser**) делит картинку на **зоны** и включает их по очереди или все вместе, как световой чейсер.

- **Зоны:**
  - сетка (колонки × ряды);
  - **слайсы Resolume Advanced Output** из XML-файла;
  - слайсы, сгруппированные **по экранам**: все слайсы одного экрана мигают вместе.
- **Чем запускается шаг:**
  - **время** (Гц);
  - **BPM Resolume** (от 4 тактов до 1/32, плюс триоли);
  - **звук** (удары в выбранной полосе частот);
  - **вручную** (кнопка Step на MIDI, OSC или клавиатуре).

## Установка

- **Windows:** `SliceChaser.dll` → `Documents\Resolume Arena\Extra Effects\`
- **macOS:** `SliceChaser.bundle` → `~/Documents/Resolume Arena/Extra Effects/`, затем:
  ```
  xattr -dr com.apple.quarantine ~/Documents/Resolume\ Arena/Extra\ Effects/SliceChaser.bundle
  ```

Перезапустить Arena → **Effects** → **Slice Chaser**.

## Зоны

### Сетка
**Zones = Grid**, Columns × Rows. Например, три экрана в ряд — 3 × 1, LED-стена из 8 кабинетов по горизонтали — 8 × 1.

### Слайсы Advanced Output
FFGL-эффект не видит раскладку Advanced Output напрямую, поэтому плагин читает её из файла, который Resolume сама сохраняет.

1. **Zones = Advanced Output XML**.
2. В **Slices File** выбрать один из файлов:
   - текущую раскладку: `Documents/Resolume Arena/Preferences/AdvancedOutput.xml`. Arena перезаписывает этот файл при каждой правке слайсов, а плагин раз в секунду проверяет его и подхватывает изменения сам;
   - или сохранённый пресет из `Documents/Resolume Arena/Presets/Advanced Output/`.
3. **Group By:** **Slices** (каждый слайс — своя зона) или **Screens** (все слайсы одного экрана — одна зона).
4. Включить **Show Zones**. Зоны подсветятся разными цветами с номерами, а в названии параметра Use Zones появится сводка («4 of 4 zones (3 screens, 4 slices)»).

**Эффект нужно ставить на композицию** (или на слой размером с композицию). Координаты слайсов задаются в пикселях композиции, иначе зоны не совпадут с экранами.

Что берётся из файла:
- **Input**-область каждого слайса (то, что слайс забирает из композиции), включая наклонные четырёхугольники;
- маски и кропы пропускаются;
- выключенные экраны и слайсы тоже пропускаются;
- если слайсы перекрываются, общую область получает тот, что идёт в файле позже.

### Use Zones и Order
- **Use Zones** — какие зоны участвуют: `1-4, 7`, `5-` (с пятой до конца). Пусто — все. Номера видны в Show Zones.
- **Order** — порядок обхода: As Listed (как в файле), Left to Right, Top to Bottom, Shuffled.

## Паттерны

| Pattern | Что делает |
|---|---|
| Chase > / Chase < | Бегущий огонь вперёд или назад. **Width** — сколько зон горит одновременно |
| Ping-Pong | Туда-обратно |
| Random | Случайные зоны, одна и та же не повторяется дважды подряд |
| All (Strobe) | Все зоны мигают вместе — строб |
| Odd / Even | Через одну: чётные ↔ нечётные |
| Center Out | От центра к краям |
| Fill | Заполнение: 1, 1-2, 1-2-3… |
| Audio Meter | VU-метр: уровень звука заполняет зоны по порядку |

## Trigger — чем запускается шаг

| Trigger | Параметры |
|---|---|
| Time (Hz) | **Rate Hz** 0.1–30 шагов в секунду |
| BPM Sync | **Beat Division**: 4 Bars … 1/32, 1/4 и 1/8 триоли. Работает от BPM Resolume и тап-темпа |
| Audio | **Audio Band** (Full / Bass (kick) / Mid / High (hats)) и **Sensitivity**. Шаг на каждый удар |
| Manual (Step) | Кнопка **Step**: MIDI-пэд, OSC, клавиша |

**Restart** возвращает чейз к первой зоне.

**Как работает звук.** Плагин получает от Resolume спектр звука (параметр Audio, буфер FFT). Детектор срабатывает, когда энергия в выбранной полосе резко поднимается над своим средним. Громкость подстраивается автоматически: тихий и громкий трек работают одинаково, **Sensitivity** решает, насколько сильным должен быть удар. В режиме **Show Zones** внизу слева есть индикатор уровня, он вспыхивает красным на каждом ударе. С ним удобно подстраивать чувствительность.

## Strobe — как выглядит вспышка

| Параметр | Что делает |
|---|---|
| Flash Length | Какую часть шага зона горит. 100 % — горит весь шаг (чейз без пауз); 20 % — короткий «удар» |
| Decay | Хвост затухания после вспышки. 0 — резкий обрыв |
| Inner Strobe Hz | Горящая зона дополнительно мерцает с этой частотой (0 = выкл.) |
| Flash Look | **Content** — показать контент; **Content Boost** — контент ярче; **White**; **Color** (Flash Color, альфа = сила); **Invert** |
| Off Level | Что остаётся в погашенных зонах: 0 % — чёрное или прозрачное, 100 % — контент без изменений |
| Outside Zones | Что делать вне всех зон: Unchanged / Hidden / Like Off Zones |

Все параметры — обычные параметры Resolume: их можно назначить на MIDI или OSC, привязать к BPM или к звуку средствами самой Resolume.

## Сборка из исходников

Нужны CMake 3.20+ и Visual Studio 2019+ (Windows) или Xcode (macOS). FFGL SDK и GLEW скачиваются автоматически.

```
# Windows
cmake -B build -A x64
cmake --build build --config Release

# macOS (universal: Apple Silicon + Intel)
cmake -B build -G Xcode
cmake --build build --config Release
```

Результат — в `build/dist/`. Через GitHub: workflow `.github/workflows/build.yml` собирает Windows и macOS и прогоняет тесты.

Тесты (паттерны, BPM, звук, сетка, разбор XML): `-DSLICECHASER_BUILD_TESTS=ON`, затем `chaser_tests examples/example_3_screens.xml`.

## Устройство

- `src/FFGLChaser.*` — плагин: параметры, карта зон в текстуре, шейдер.
- `src/ChaseEngine.*` — паттерны, тайминг (Hz / BPM / звук / Step), огибающая вспышки, анализ звука.
- `src/Zones.*` — сетка, чтение Advanced Output, порядок, растеризация зон.
- `src/MiniXml.*` — маленький XML-парсер.
- `examples/example_3_screens.xml` — пример файла: 3 экрана, 4 слайса, маска, выключенный слайс.

Формат Advanced Output XML сверен с файлами, которые пишет Resolume Arena 7.27 ([описание](https://raw.githubusercontent.com/stoatworks-labs/negative-space/HEAD/docs/resolume-export.md)).
