#pragma once
#include <QString>
#include <QStringList>
#include <QPixmap>

class QLabel;

// ─────────────────────────────────────────────────────────────────────────────
//  Avatar — круглые аватары: буква-плейсхолдер на градиенте + асинхронная
//  подгрузка картинки по avatar_url (с токеном для /files), кэш по URL.
// ─────────────────────────────────────────────────────────────────────────────
namespace Avatar {

QPixmap roundLetter(const QString& text, int size);

// Ставит на label круглый аватар: сразу буква, затем картинка (если url задан).
void setRound(QLabel* label, const QString& url, const QString& fallbackText, int size);

} // namespace Avatar

// ─────────────────────────────────────────────────────────────────────────────
//  FolderIcons — иконки и цвета папок чатов, 1:1 с набором веб-клиента
//  (FOLDER_ICON_KEYS / FOLDER_COLOR_SET в web/js/chat.js).
// ─────────────────────────────────────────────────────────────────────────────
namespace FolderIcons {

// Эмодзи-глиф для ключа иконки ('chat' → '💬'); неизвестный ключ → '💬'.
QString glyph(const QString& key);
// Все ключи иконок (в порядке веба) — для сетки выбора в редакторе папок.
QStringList keys();
// Палитра цветов папок (в порядке веба).
QStringList colors();

} // namespace FolderIcons
