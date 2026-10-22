# Установка gears2-nx 0.8.1 (Gears of War 2 для Nintendo Switch)

gears2-nx — нативный порт Gears of War 2 (Xbox 360) для Switch в виде homebrew-программы. Готовых файлов не
распространяется: код игры, шейдеры и программа собираются у вас на компьютере **из вашей собственной копии** (диск
Xbox 360).

> Версия 0.8.0: играбельна с разгоном (~25 кадров/с), все шейдеры игры читаются с карты по запросу.

## Что понадобится

| Что | Подробности |
| --- | --- |
| Nintendo Switch | С кастомной прошивкой **Atmosphère** и Homebrew Menu |
| Карта microSD | Свободно **~8 ГБ**, exFAT или FAT32 (файлы игры меньше 4 ГБ каждый) |
| Образ вашего диска | Gears of War 2 для Xbox 360: `.iso`, `.7z` с ISO внутри (читается без распаковки) или уже распакованная папка с `default.xex` и `GearGame`. Исполняемый файл: Title ID **4D53082D**, Media ID **63AF211E** (0.0.0.5) или **33FCE762** (0.0.0.6, европейский диск — проверено) |
| Title Update | **Не нужен и не поддерживается**: код собирается из исполняемого файла без обновлений (так же, как в PC-порте) |
| Компьютер | **macOS** (проверено) или Linux (не проверялось), ~12 ГБ свободного места, интернет |

Заголовок `default.xex` проверяется: другие версии исполняемого файла к коду не подходят.

## Шаг 1. Инструменты (один раз)

1. **Инструменты Xcode:** `xcode-select --install`.
2. **Homebrew** (https://brew.sh), затем `brew install cmake ninja python git`.
3. **devkitPro:** установщик со страницы https://github.com/devkitPro/pacman/releases, затем
   `sudo dkp-pacman -S switch-dev deko3d`.
4. **Vulkan SDK** (https://vulkan.lunarg.com): нужны `dxc` и `spirv-val` для шейдеров; на macOS ставится в
   `~/VulkanSDK`.

## Шаг 2. Подготовить файлы игры

```
git clone --recursive https://github.com/VasilkovND/gears2-nx
cd gears2-nx
python3 gow2/tools/stage_sd.py --disc "<путь к ISO или .7z>" -o sd/gears2-nx
```

Скрипт распакует диск в `sd/gears2-nx/game/` и проверит `default.xex` (несколько минут). Если диск уже распакован:
`--folder <папка>` вместо `--disc`.

## Шаг 3. Собрать программу и шейдеры

```
gow2/tools/build_mac.sh sd/gears2-nx/game
```

Первый раз — долго (десятки минут) и ~5 ГБ места: скрипт скачает PC-порт gears-of-war-2-pc-port (ровно проверенную
версию) и ReXGlue, сгенерирует код игры из вашего `default.xex`, распакует пакеты игры, извлечёт и переведёт шейдеры и
соберёт программу. Результат:

- `gow2/out/sw/gow2.nro`
- `gow2/out/shaders/gow2_shaders.dslib`
- `gow2/out/shaders/shadercache_dksh.bin`

## Шаг 4. Скопировать на карту

```
sdmc:/switch/gears2-nx/
    gears2-nx.nro            gow2/out/sw/gow2.nro, переименован
    gow2_shaders.dslib       gow2/out/shaders/
    shadercache_dksh.bin     gow2/out/shaders/
    game/                    sd/gears2-nx/game/ целиком
```

Логи пишутся в `sdmc:/switch/gears2-nx/logs/`.

## Запуск

1. Запустите **любую установленную игру, удерживая кнопку R**: так откроется Homebrew Menu с полным доступом к памяти.
   Через альбом (режим апплета) памяти не хватит.
2. Выберите **Gears of War 2**.

Счётчик FPS: **ZL + ZR + вверх на крестовине**.

Если игра покажет шейдер, которого нет в библиотеке, программа сохранит его в `shaders_dump/` рядом с собой (когда
адреса функций создания шейдеров уже известны): пришлите эту папку и `logs/` разработчику.
