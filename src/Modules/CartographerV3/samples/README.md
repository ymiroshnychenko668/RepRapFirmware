# Cartographer V3 opt-in configuration samples

These templates belong to the module. They are never installed into printer
profiles, `config.g`, homing macros or active SD configurations by a module
build. The `.g.in` files require substitution and are not runnable macros.
No UUID, plate identity, measured offsets, operating speed or Touch threshold
is supplied as a default. Error-message placeholders use the selected `en`/`uk`
entry in `messages.json`; generated bundles use rrfsetup's matching catalog.

Use the offline `rrfsetup cartographer config` command with a validated `.cart`
model and explicit matching UUID/profile/surface to generate a reviewable ZIP:

```sh
rrfsetup cartographer config --model /path/to/measured.cart \
  --uuid '<actual-12-hex-UUID>' --profile-sha256 '<identify-SHA256>' \
  --surface '<actual-surface-ID>' --mode diagnostic \
  --output /path/to/separate/cartographer-config.zip
```

For `scan`, `home` and `touch`, also supply all of `--probe-index`,
`--trigger-height`, `--dive-height`, `--probe-speed`, `--travel-speed`,
`--duration-ms`, `--x-offset` and `--y-offset`. Speeds are mm/s in the tool;
the generated M558 F/T values are mm/min. M558 H is the **dive height**;
G31 Z and S6 E hold the trigger height/Touch datum. A Touch bundle also requires
an explicitly verified `--touch-threshold`. No movement command is part of the
configuration macro. Each separate measurement/homing/Touch macro has its own
disabled activation gate.

Scan/home starts must already lie inside the calibrated height domain; the
software never infers physical clearance or finds the surface automatically.
`cartographer-home.g.in` uses `G30` to establish the selected Z datum. It is an
explicitly invoked alternative macro, not an edit to the printer's existing
`homez.g`. The measurement macro uses `S-1` and does not establish a datum.
Touch has an independent gate, physical datum and raw threshold; neither a model
nor a successful scan authorizes contact by itself. Review all machine-specific
conditions before enabling a gate. Hardware commissioning and stopping accuracy
remain separate from software test results.

Copy the binary model and selected generated macros using an explicit file
transfer, then compare SHA256 against `manifest.json`. Do not upload `.cart`
through a text/G-code upload command. Loading configuration, physical wiring,
firmware installation and movement are separate operations.

Continuous mesh is optional. In `scan` mode, specify **all** of
`--mesh-x-min`, `--mesh-x-max`, `--mesh-y-min`, `--mesh-y-max` and
`--mesh-spacing` to include `cartographer-mesh.g` in the generated bundle.
Without them no grid or mesh macro is invented. F446 limits are 441 total
points, 41 points per X row, at least 1 mm range per axis and 0.1 mm spacing.
The explicit bounds must also allow both the probe and nozzle to move safely;
the offline tool cannot determine this from the model. All axes must already
be homed; the macro never runs G28. `G29 S0 K...` bypasses any existing
`mesh.g`. This fork's P12-specific `P` output selects the separate
`cartographer-v3-heightmap.csv` under `sys`, preserving the default
`heightmap.csv`. Use the `cartographer-mesh.g.in` template for manual preparation;
its output filename is another explicit placeholder. Its action gate stays false.
When explicitly enabled and executed, the macro defines the M557 grid, scans,
saves the separate file and activates the new mesh in RAM on success. It does
not restore a previously active mesh. Merely preparing the files performs none
of these operations.

The workspace guide is `rrfsetup/Docs/Firmware/CARTOGRAPHER_V3.md`; the pinned
wiring guide is `rrfsetup/Docs/Firmware/CARTOGRAPHER_V3_CONNECTION.md`.

## Українська

Шаблони належать модулю та не встановлюються в профілі принтера, `config.g`,
макроси базування чи активну SD-копію. Файли `.g.in` потребують підстановки
значень і не є готовими макросами. UUID, поверхня, виміряні зміщення,
робочі швидкості та поріг Touch не мають типових значень.

Автономна команда `rrfsetup cartographer config --ui-language uk` перевіряє
модель та явні UUID/профіль/поверхню і створює окремий ZIP для перегляду.
Для режимів `scan`, `home`, `touch` усі механічні параметри задаються явно;
`touch` також вимагає перевіреного порога. Швидкості інструмента — мм/с,
M558 F/T — мм/хв. M558 H — висота занурення, G31 Z та S6 E — висота
спрацьовування або опорна висота Touch. Конфігураційний макрос не рухає
принтер. Кожен окремий макрос вимірювання, базування чи Touch має власний
вимкнений дозвіл. Поточні `homez.g` та інші налаштування не змінюються.

Перед початком датчик має бути в межах відкаліброваного діапазону. Модель
та успішний Scan самі не дозволяють контакт. Перевірка фізичної зупинки,
порога, вільного ходу й апаратного підключення виконується окремо.
Передавайте `.cart` бінарним файловим інтерфейсом і звіряйте SHA256 із
manifest; текстовий завантажувач G-code для моделі не підходить.

Для необов’язкової неперервної сітки в режимі `scan` задайте всі п’ять
параметрів `--mesh-x-min`, `--mesh-x-max`, `--mesh-y-min`, `--mesh-y-max`,
`--mesh-spacing`. Без них інструмент не створює сітки чи макросу сітки.
Межі F446 — 441 точка загалом, 41 у рядку X, діапазон від 1 мм на вісь
і крок від 0.1 мм. Фізичний вільний хід сопла й датчика та базування всіх
осей перевіряються окремо. Автоматичного G28 немає. G29 S0 обходить чинний
`mesh.g`, а результат P12 записується в окремий `cartographer-v3-heightmap.csv`
у `sys`; типовий `heightmap.csv` зберігається. Дозвіл сітки залишається false.
Після явного дозволу та запуску макрос задає сітку M557, сканує, зберігає
окремий файл і в разі успіху активує нову сітку в RAM. Попередня активна
сітка не відновлюється. Підготовка файлів не виконує цих операцій.
