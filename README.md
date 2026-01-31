# PCController

Telegram-бот для удалённого управления компьютером. Реализация на C++ использует Telegram Bot API через `libcurl` и управляется кнопками клавиатуры Telegram.

## Возможности

- Long polling через `getUpdates`.
- Белый список chat-id.
- Команда `/screenshot` для отправки изображения.
- Открытие веб-интерфейса через кнопку **Open** в меню вложений Telegram.

## Требования

- CMake 3.16+
- Компилятор C++17
- `libcurl`
- ImageMagick (`import`) для Linux (снятие скриншотов)
- Windows использует встроенный WIC (доп. пакеты не требуются)

## Сборка

```bash
cmake -S . -B build
cmake --build build
```

## Настройка

1. Создайте бота через @BotFather и получите токен.
2. Экспортируйте токен как переменную окружения:

```bash
export TELEGRAM_BOT_TOKEN="<TOKEN>"
```

3. Отредактируйте `config/config.ini`:

```ini
allowed_chat_ids=6538203145
screenshot.width=1280
screenshot.height=720
screenshot.compression=true
screenshot.quality=85
screenshot.format=jpg
webapp.url=https://your-domain.example/app
```

`allowed_chat_ids` — список разрешённых чатов (через запятую). Настройки `screenshot.*` управляют разрешением и форматом снимка.
`webapp.url` добавляет кнопку **Open** в меню вложений Telegram и должен указывать на ваш веб-интерфейс.

## Запуск

```bash
./build/pccontroller
```

Можно указать путь к конфигу аргументом:

```bash
./build/pccontroller путь/к/config.ini
```

## Использование

1. Откройте чат с ботом.
2. Используйте `/screenshot` для получения изображения.
3. Кнопка **Open** откроет веб-интерфейс, если указан `webapp.url`.

## Безопасность

- Не добавляйте опасные команды в `config.ini`.
- Ограничивайте доступ только своим chat-id.
- Не храните токен в коде; используйте переменные окружения.
