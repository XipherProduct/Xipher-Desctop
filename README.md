<div align="center">

# Xipher Desktop

**Нативный десктоп-клиент приватного мессенджера Xipher — на C++ и Qt 6, без webview.**

[![License: MIT](https://img.shields.io/badge/License-MIT-8B5CF6.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D6.svg)
![Built with Qt](https://img.shields.io/badge/built%20with-Qt%206%20%2F%20C%2B%2B-41CD52.svg)
[![Download](https://img.shields.io/badge/⬇%20Скачать-messenger.xipher.pro/download-8B5CF6.svg)](https://messenger.xipher.pro/download)

[Скачать](#-скачать) · [Возможности](#-возможности) · [Сборка из исходников](#-сборка-из-исходников) · [Подпись кода](#-подпись-кода)

</div>

---

Xipher — свободный мессенджер с упором на приватность. **Xipher Desktop** — его полностью
нативный клиент для Windows: написан на C++ с Qt 6, без встроенного браузера, поэтому работает
быстро и потребляет мало памяти. Архитектурный ориентир — Telegram Desktop.

Проект разрабатывается открыто под лицензией [MIT](LICENSE).

## ⬇ Скачать

Готовые сборки для **Windows 10/11 (64-bit)**:

| | |
|---|---|
| 🟣 **Последний релиз** | **[github.com/XipherProduct/Xipher-Desctop/releases/latest](https://github.com/XipherProduct/Xipher-Desctop/releases/latest)** |
| 🌐 Страница загрузки | [messenger.xipher.pro/download](https://messenger.xipher.pro/download) |
| 📦 Все версии | [Releases](https://github.com/XipherProduct/Xipher-Desctop/releases) |

Два формата:

- **Установщик** `Xipher-Setup.exe` — ставит приложение, создаёт ярлыки в «Пуск» и на рабочем столе.
- **Портативная версия** — папка с `Xipher.exe`, запуск без установки.

## ✨ Возможности

- **Чаты 1:1** — список чатов, переписка, поиск, «Избранное» (заметки себе), статусы и галочки.
- **Группы и каналы** — создание, каталог публичных, управление (участники, роли, права,
  invite-ссылки, форум-топики).
- **Папки** — вкладки чатов в стиле Telegram.
- **Контакты** — друзья, заявки, поиск и добавление, блокировка — встроенные в приложение.
- **Звонки** — голосовые 1:1 (WebRTC через TURN) и групповые (SFU).
- **Голосовые сообщения** — запись с живым waveform, воспроизведение с перемоткой.
- **Сообщения** — ответы, копирование, пересылка, удаление, закрепление; вставка картинок из
  буфера, многострочный ввод (Enter / Shift+Enter), просмотр фото на весь экран.
- **Настройки** — аккаунт, приватность, активные сеансы, уведомления, звонки, язык, блокировки,
  email для входа, Premium.
- **Системные уведомления** и сворачивание в трей.

## 🖥 Стек

- **C++17** + **Qt 6** (Widgets, Network, WebSockets, Multimedia) — UI и сеть.
- **libdatachannel** + **libopus** — WebRTC-звонки и кодирование голоса.
- Сборка: **CMake** + **Ninja**, тулчейн **MinGW-w64** (MSYS2).
- Прод-API: `https://messenger.xipher.pro` (HTTPS + WebSocket).

## 🔧 Сборка из исходников

> Сборка под Windows, тулчейн MinGW из MSYS2.

Тулчейн ставится один раз:

```sh
winget install MSYS2.MSYS2
C:\msys64\usr\bin\bash -lc "pacman -S --needed --noconfirm \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-multimedia \
  mingw-w64-x86_64-qt6-websockets mingw-w64-x86_64-qt6-tools \
  mingw-w64-x86_64-opus"
```

Сборка:

```sh
export PATH=/c/msys64/mingw64/bin:$PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/c/msys64/mingw64
cmake --build build
```

Результат — `build/bin/Xipher.exe`.

> **Звонки** требуют `libdatachannel` в `third_party/libdatachannel` (собирается отдельно).
> Без неё клиент собирается полностью, звонки отключаются.

Чтобы `.exe` запускался без MSYS2 в `PATH`, рядом раскладываются Qt-DLL и рантайм MinGW
(`windeployqt6` + зависимости). Готовый установщик и портативную сборку собирает
[`installer/build-installer.sh`](installer/build-installer.sh).

## 🔏 Подпись кода

Релизные сборки Xipher для Windows подписываются цифровой подписью с использованием
[SignPath.io](https://signpath.io), бесплатным сертификатом для подписи кода, предоставленным
фондом [SignPath Foundation](https://signpath.org).

> This project uses free code signing provided by [SignPath.io](https://signpath.io)
> and a free code signing certificate by the [SignPath Foundation](https://signpath.org).

Подпись выполняется автоматически в CI ([GitHub Actions](.github/workflows/release.yml)) при
публикации тега `v*`. Подробности — в [docs/SIGNING.md](docs/SIGNING.md).

## 🗺 Планы

- E2EE (Signal-протокол) — на сервере уже есть поддержка.
- Боты, реакции, read-receipts/typing, светлая тема, полная локализация (EN).
- Автообновление.

## 📄 Лицензия

[MIT](LICENSE) © Xipher
