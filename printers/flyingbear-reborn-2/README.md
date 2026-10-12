# FlyingBear Reborn 2 / MKS Nano6 — экспериментальный профиль

Ветка: `printer/flyingbear-reborn-2`. Целевой MCU по сохранённым исходникам: STM32F407VGT6, Flash 1 MiB, кварц 8 MHz. На экземпляре пользователя прочитано только MKS / F407; суффикс MCU и ревизия не подтверждены. Сборка: `PRINTER=flyingbear-reborn-2 NETWORK=COMBINED`. Профиль рассчитан на штатный SD-загрузчик Nano6 с областью 48 KiB; выходной файл — `Robin_nano_6.bin`. Сам загрузчик не заменяется. Управление — USB CDC. TS35 и штатный MKS WiFi не поддерживаются, AUX UART отключён.

## Память и обновление

| Область | Адрес | Размер |
|---|---|---|
| Штатный загрузчик | `0x08000000` | 48 KiB |
| Векторы приложения, сектор 3 | `0x0800C000` | 16 KiB |
| Reset data, весь сектор 4 | `0x08010000` | 64 KiB |
| Код и встроенные настройки, секторы 5–11 | `0x08020000` | 896 KiB |

Одного изменения VTOR недостаточно: прежние reset data в `0x0800C000` совпадали бы с векторами. Новый linker script резервирует отдельный сектор 4; размер массива NVM соответствует размеру стираемого сектора. Generic SD updater использует имя `firmware.bin`, а Nano6 ожидает `Robin_nano_6.bin`, поэтому M997 для этого экспериментального профиля отклоняется. Обновление — штатным загрузчиком с SD, без generic IAP/DFU из rrfsetup.

SD-карта подключена по SPI3: SCK PC10, MISO PC11, MOSI PC12, CS PC9. В RRF это `sdcard.internal.type=3`, начальная частота 4 MHz. SDIO использовать нельзя: его PD2/PC8 заняты концевиками Y/Z.

## Комплект

`rrfboot.txt` и `rrfpins.txt` встраиваются в firmware; `sd/sys/` содержит CoreXY, два независимых Z, экструдер, PT1000 хотэнда, термистор стола, вентиляторы и homing-макросы. При сборке rrfsetup создаёт единый ZIP с SD-каталогом, бинарником, ELF/map и manifest с SHA256 и версиями зависимостей.

Это исходный профиль для проверки, а не результат испытаний данного экземпляра. Ревизия платы пока неизвестна. ENABLE Y по Nano6 Marlin и wiki — PE4 (общий с X); blacknet/MeowFire указывают PE1. Переопределение находится в `sd/sys/board.txt`. Драйверы — standalone, 32 микрошагов; UART и чужие токи/PID не переносятся. Шаги E=420 взяты из заводского V13; направления, микрошаги, концевики, передачи и термодатчики нужно сверить. Homing/нагрев не запускаются автоматически. При общем ENABLE X/Y для независимого отключения двигателей потребуется аппаратная проверка; профиль не подразумевает такую независимость.

## Происхождение

- Nano6 и смещение: [vlsi Marlin ini/stm32f4.ini](https://github.com/vlsi/reborn2-marlin/blob/vlsi-2.1.x/ini/stm32f4.ini), `mks_robin_nano6`.
- Пины: [Nano6 Marlin](https://github.com/vlsi/reborn2-marlin/blob/vlsi-2.1.x/Marlin/src/pins/stm32f4/pins_MKS_ROBIN_NANO6.h) и [общие пины](https://github.com/vlsi/reborn2-marlin/blob/vlsi-2.1.x/Marlin/src/pins/stm32f4/pins_MKS_ROBIN_NANO_V3_common.h).
- Сверка конфигов и заводского V13: `references/flyingbear-reborn-2/docs/HARDWARE_BASELINE.md` в общем workspace RepRap.

Текстовые настройки профиля составлены для этого проекта по сохранённым данным. Изменения исходников RRF сохраняют лицензию GPL-3.0 исходного проекта.
