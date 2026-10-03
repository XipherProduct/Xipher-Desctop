#pragma once
#include <QByteArray>
#include <QPixmap>
#include <QString>

// Внутренняя таблица каталога (реализация — сгенерированный GiftArtData.cpp).
namespace GiftArtData {
struct Entry { const char* slug; const char* name; const char* rarity; const char* svg; };
const Entry* entries();
int count();
const char* boxSvg();
} // namespace GiftArtData

// ─────────────────────────────────────────────────────────────────────────────
//  GiftArt — векторные иллюстрации подарков (порт web/js/gift-art.js).
//
//  Десктоп раньше рисовал слаг («crown») текстом: системные эмодзи отличаются
//  на каждой ОС и рядом с остальным интерфейсом выглядят чужими — поэтому,
//  как и в вебе, рисуем собственные SVG 64×64 в единой школе (градиенты,
//  блики, одинаковая плотность силуэта). Данные — сгенерированный
//  GiftArtData.cpp, идентичный веб-файлу байт в байт.
//
//  Редкость — шкала (common/rare/epic/legendary); серверный признак тиража
//  (limited/event) может её только поднять. Номер экземпляра — стабильный
//  FNV-1a от UUID копии, как number() веба.
// ─────────────────────────────────────────────────────────────────────────────
class GiftArt {
public:
    static bool known(const QString& slug);

    // SVG-разметка; для неизвестного слага — нейтральная коробка,
    // чтобы карточка не осталась пустой (как giftArt() веба).
    static QByteArray svg(const QString& slug);

    // Рендер в пиксели с кэшем. px — логический размер; рисуем 2× под
    // чёткость на hidpi и проставляем devicePixelRatio.
    static QPixmap pixmap(const QString& slug, int px);

    // Локальное имя из каталога («Корона»), пусто для неизвестного.
    static QString name(const QString& slug);

    // rarityFor: базовый разряд каталога, поднятый серверным признаком
    // (limited→epic, event→rare). rarityLabel — «Обычный/Редкий/…».
    static QString rarityFor(const QString& slug, const QString& serverRarity);
    static QString rarityLabel(const QString& tier);

    // Стабильный пятизначный номер экземпляра (FNV-1a по UUID, как в вебе).
    static int number(const QString& instanceId);
};
