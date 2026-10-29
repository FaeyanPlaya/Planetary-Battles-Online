# Planetary Battles Online

Сетевые планетарные бои Space Rangers HD для 2–4 игроков (люди и ИИ в любых сочетаниях) на открытом движке MatrixGame.
*Online planetary battles of Space Rangers HD for 2–4 players, built on the open-source MatrixGame engine.*

Неофициальный фанатский проект. Ресурсов игры (модели, текстуры, карты, звуки, настройки роботов) нет ни в репозитории,
ни в сборках: при первом запуске игра копирует их из вашей собственной установленной Space Rangers HD: A War Apart.

## Как играть

1. Нужна установленная Space Rangers HD: A War Apart (проверена Steam-версия 2.1.2500).
2. Скачайте `Planetary_Battles_Online.zip` со страницы [Releases](../../releases) и распакуйте в папку, куда можно писать
   (не в Program Files).
3. Запустите `Planetary Battles Online.exe`. При первом запуске он найдёт игру и предложит взять из неё файлы.
4. Один игрок создаёт сетевой бой, остальные подключаются по его адресу (локальная сеть, VPN или виртуальная LAN).
   Подробности — в `README.txt` внутри архива.

У всех игроков должна быть одна и та же сборка.

## Моды

Моды этого проекта встроены в exe. Моды боя хост включает в лобби (кнопка «Моды»), у всех игроков они одинаковые:
исправления движка и ошибок оригинала, ремонтные заводы, которые чинят роботов, управление турелями. Без модов бой
идёт как в оригинальной игре. Личные моды каждый включает у себя в настройках: дальняя прорисовка и правки
интерфейса. Карты из модов Space Rangers HD (например, PBNewMaps) берутся прямо из установленной игры; у кого
такого мода нет, тот получит карту от хоста.

## Сборка из исходников

Windows 10 или новее, 7-Zip в PATH. Из корня репозитория:

```powershell
.\tools\setup-toolchain.ps1
.\tools\build.ps1 -Configuration Release -WithoutTests
```

`setup-toolchain.ps1` скачивает и проверяет закреплённые версии MinGW (GCC 13.2.0, x86), Ninja и CMake 4.4.4 в папку
`.tools`; для DirectX 9 SDK (D3DX9) есть `tools\setup-directx-sdk.ps1`. Можно и проще: `build.bat` запускает мастер,
который находит или скачивает всё нужное и задаёт вопросы. Готовый `MatrixGame.exe` появится в
`build\mingw-release-exe\MatrixGame`; при первом запуске он так же скопирует ресурсы из установленной игры.

## Лицензия и авторы

Код распространяется по **GPLv2 или более поздней версии** (см. [LICENSE](LICENSE)). Правообладатели выпустили исходники
движка (`MatrixGame`, `MatrixLib`) под этой лицензией; сохраняйте их уведомления об авторских правах.

Проект основан на [pozitronik/MatrixGame](https://github.com/pozitronik/MatrixGame), который продолжает
[vladislavrv/MatrixGame](https://github.com/vladislavrv/MatrixGame) и [murgesku/MatrixGame](https://github.com/murgesku/MatrixGame);
есть [зеркало исходного кода](https://github.com/twoweeks/MatrixGame). Сетевая игра добавлена поверх.

Используются libpng, zlib, libogg и libvorbis по их собственным лицензиям
([libpng](https://www.libpng.org/pub/png/src/libpng-LICENSE.txt), [zlib](https://github.com/madler/zlib/blob/v1.3.2/LICENSE),
[libogg](https://github.com/xiph/ogg/blob/v1.3.6/COPYING), [libvorbis](https://github.com/xiph/vorbis/blob/v1.3.7/COPYING));
версии и контрольные суммы — в [ThirdParty/dependencies.json](ThirdParty/dependencies.json).

Space Rangers и все ресурсы игры принадлежат их правообладателям.

Сетевая часть и моды написаны с помощью ИИ: Claude (Anthropic) в Claude Code.
