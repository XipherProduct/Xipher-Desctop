#pragma once
#include <QByteArray>

// ─────────────────────────────────────────────────────────────────────────────
//  AesGcm — AES-256-GCM (шифрование/расшифровка + тег целостности).
//  Нужен для сторис: медиа-файлы истории шифруются этим алгоритмом
//  (web/js/stories.src.js, crypto.subtle AES-GCM 256), ключ и IV приходят
//  с сервера в /api/stories/all. Корректность проверена тест-векторами
//  NIST SP 800-38D (tests/aesgcm-test.cpp).
// ─────────────────────────────────────────────────────────────────────────────
namespace AesGcm {

// key — 32 байта, iv — 12 байт (стандартный размер GCM). aad может быть пустым.
// encrypt: out+tag; decrypt: out (проверяет тег, false — не сошёлся).
bool encrypt(const QByteArray& key, const QByteArray& iv,
             const QByteArray& plain, const QByteArray& aad,
             QByteArray* out, QByteArray* tag);
bool decrypt(const QByteArray& key, const QByteArray& iv,
             const QByteArray& cipher, const QByteArray& aad,
             const QByteArray& tag, QByteArray* out);

} // namespace AesGcm
