# Doom для Лілки!

Це - порт Doom для Лілки.

[lib/doomgeneric](./lib/doomgeneric) містить модифікований код рушія Doom Generic для запуску на ESP32-S3.
Я додав динамічну алокацію пам'яті для деяких "товстих" масивів, щоб використовувати PSRAM. Таким чином весь основний код Doom тепер спокійно поміщається в 400 КБ основної RAM. Крім цього, є ще деякі зміни (див. [README](./lib/doomgeneric)).

[src/main.cpp](./src/main.cpp) містить код, специфічний для Лілки (малювання екрана, ініціалізація, etc).

Решта файлів в `src` - це звукові драйвери для I2S (з простим міксуванням звуку) та п'єзодинаміка.

## Керування

Під час гри 3D-зображення разом з панеллю стану займає весь екран Лілки v2
(280×240) без обрізання боків світу. Меню та інші екрани зберігають
оригінальні пропорції 4:3. Лічильник FPS та журнал на екрані вимкнені;
журнал залишається доступним через Serial.

| Кнопка | У грі | У меню Doom |
| --- | --- | --- |
| Хрестовина | Рух уперед/назад, поворот | Перехід між пунктами, зміна повзунків |
| A | Стріляти | Вибрати / «так» |
| B | Використати | Назад / «ні» |
| C | Карта | — |
| D | Наступна зброя | — |
| Select | Не використовується | Не використовується |
| Start | Відкрити меню | Вибрати / «так» |

Кнопка B повертається на попередній екран меню, а з головного меню закриває його.
При збереженні виберіть слот кнопкою A або Start: порожній слот отримує
скорочену назву поточного рівня, наприклад `1: Hangar`. Перезапис наявного
слота зберігає його назву.

Щоб вийти: **Start → Quit Game → A або Start → A або Start** на
підтвердженні «Press Y». Кнопка B скасовує підтвердження.
Пункт **End Game** працює лише під час гри, запущеної через **New Game**;
автоматична демонстрація Doom не є активною грою.
Після коректного виходу Doom перезавантажує Лілку; якщо його запущено через
MultiBoot, завантажиться основна прошивка Keira.

Для гри потрібен Doom-сумісний базовий IWAD (`.wad`) поруч із файлом прошивки на SD-карті. Під час запуску Doom перевіряє всі файли `.wad` у цій папці та пропонує вибрати один зі знайдених IWAD. Назва файлу не мусить починатися з `doom`; окремі PWAD-доповнення не запускаються без базового IWAD і в цьому списку не показуються.

Збереження кожного вибраного WAD лежать окремо: `saves/<назва-wad>/` поруч із прошивкою (наприклад, `saves/doom2.wad/doomsav0.dsg`). Старі збереження, що вже лежать безпосередньо поруч із WAD, не видаляються й автоматично не переносяться: їхню належність до конкретного WAD неможливо визначити надійно. Якщо вона відома, файли `doomsav*.dsg` можна скопіювати до відповідної нової папки після першого запуску.

## Перегляд інтерфейсу на кадрі демонстрації

Перед перевіркою прошивки на пристрої можна зняти справжній кадр `DEMO1`
із наданого IWAD без відкритого меню та порівняти поточне й запропоноване
відображення Лілки:

```sh
tools/render_demo_preview.sh /шлях/до/DOOM.WAD /tmp/doom-preview
```

Проміжний кадр оригінального переходу-плавлення можна перевірити окремо:

```sh
tools/render_demo_preview.sh /шлях/до/DOOM.WAD /tmp/doom-wipe 25 wipe
```

Під час переходу показується лише рухоме зображення гри; звичайна панель
стану повертається після завершення ефекту. У відкритому ігровому меню та
на мапі використовується та сама нова панель, без переходу до старої.

Скрипт створює `doom-preview-source.ppm`, `doom-preview-stage.png`,
`doom-preview-candidate.png` та `doom-preview-compare.png`. Тонка смуга
безпосередньо над нижньою панеллю зберігає всі чотири оригінальні типи
набоїв та обидві числові колонки
(поточна/максимальна); нижня панель є оригінальними пікселями x=0..250,
розміщеними по центру. Лише цілі секції ARMS і портрета міняються місцями
без масштабування їхнього вмісту. Нижня панель прилягає до краю дисплея
без чорної смуги знизу. Меню скрипт не показує і прошивку не змінює.
Потрібні `gcc`, Python 3 та, для PNG, ImageMagick `convert`; IWAD не
копіюється до репозиторію.

## Automatic battery calibration (modified hardware)

All charging logic runs in the shared SDK. This app already calls `lilka::begin()`,
which starts its worker when `LILKA_ADC_CHARGE_STATUS=1` is enabled. No app-side
calibration code, polling or menu action is needed. It works during gameplay,
playback/scanning, menus, and LCD-off periods while the firmware is running.

After a confirmed **Charged → Battery** transition, the SDK waits **30 seconds**,
then saves the full-charge reference once. Reconnecting USB or invalid readings
cancel it. Startup on Battery or disconnecting before charging finishes does not
calibrate. The saved reference/discharge profile is shared with Keira and other
SDK firmware. Switching firmware restarts the SDK's RAM-only transition history.

Requires the existing **33 kΩ/10 kΩ optocoupler ADC-tag modification** and a current
SDK `features/stage` or `features/stage-lilplayer` containing the charge monitor.
Stock builds remain opt-out; do not select these profiles on unmodified hardware.
For ADC tags only: `pio run -e v2-adc-charge-status`. For both ADC tags and the
independently wired amplifier/backlight:

```sh
pio run -e v2-modified-backlight-adc-charge-status
```

Profile/startup wiring and the shared SDK worker have source/host verification;
these checks are not a firmware build or physical-device calibration test.
