# Сторонние компоненты

Корневая [лицензия MIT](LICENSE) относится к собственному коду проекта.
Перечисленные ниже компоненты сохраняют свои уведомления об авторстве и условия.

| Компонент | Назначение | Лицензия и источник |
| --- | --- | --- |
| MinHook 1.3.4 | Перехват функций в нативном моде | [BSD, включая уведомления HDE](native/vendor/minhook-1.3.4/LICENSE.txt); [исходный проект](https://github.com/TsudaKageyu/minhook/tree/v1.3.4) |
| Quaternius Universal Animation Library | Данные базовой локомоции | [CC0](native/vendor/quaternius/LICENSE.txt); [происхождение и SHA-256](native/vendor/quaternius/SOURCE.json) |

## Внешние инструменты и runtime

Для Steam Spacewar используется оригинальная `steam_api64.dll` Valve из Steamworks SDK.
Она не относится к MIT-лицензии кода мода. `tools/setup_steam.ps1 -Download` извлекает её
из [Steamworks.NET Standalone 2025.164.1](https://github.com/rlabrecque/Steamworks.NET/releases/tag/2025.164.1),
проверяя SHA-256 архива. Источник и хеши сохраняются в `runtime/steam/SOURCE.json`,
уведомление Steamworks.NET — в `runtime/steam/Steamworks.NET-LICENSE.txt`.
Собственная привязка к C API находится в `server/ghost_coop/steam_api.py`;
управляемая библиотека Steamworks.NET не используется. Steam и Steamworks принадлежат Valve.

UE4SS скачивается пользователем отдельно со
[страницы UE4SS for Atomic Heart](https://www.nexusmods.com/atomicheart/mods/186).
Бинарники, конфигурация, загрузчик и сигнатуры UE4SS не входят в исходники
и архивы мода. Поддерживаемая сборка и установка описаны в [README](README.md#установка-ue4ss).

Переносимый Python включается в установочный архив вместе с его лицензией.
В эту копию исходников Python не включён.

Unreal Engine 4.27 устанавливается отдельно для сборки собственных ресурсов
интерфейса. Исходники движка и его SDK в репозиторий не входят.

## Игровые данные

Мод использует ресурсы установленной Atomic Heart. Игровые модели, текстуры,
материалы, извлечённые пакеты и приватные reference-данные не включены в эту
копию.

Atomic Heart и Unreal Engine не лицензируются корневой MIT-лицензией проекта.
