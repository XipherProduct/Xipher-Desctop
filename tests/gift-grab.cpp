// gift-grab.cpp — визуальное доказательство порта подарков (GiftArt):
// лист-каталог всех 16 слагов веб-школы (SVG → пиксели, имена, редкость).
//
// Запуск: QT_QPA_PLATFORM=offscreen ./build-linux/bin/gift-grab
// Выход: /tmp/gifts-catalog.png

#include "ui/GiftArt.h"
#include <QApplication>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget sheet;
    sheet.setStyleSheet(QStringLiteral("background:#0B0A0E;"));
    auto* grid = new QGridLayout(&sheet);
    grid->setContentsMargins(20, 20, 20, 20);
    grid->setSpacing(14);
    const QStringList slugs = {
        QStringLiteral("rose"), QStringLiteral("heart"),
        QStringLiteral("clover"), QStringLiteral("candle"),
        QStringLiteral("cake"), QStringLiteral("snowflake"),
        QStringLiteral("star"), QStringLiteral("rocket"),
        QStringLiteral("bouquet"), QStringLiteral("ring"),
        QStringLiteral("crystal"), QStringLiteral("diamond"),
        QStringLiteral("fireworks"), QStringLiteral("medal"),
        QStringLiteral("trophy"), QStringLiteral("crown")};
    int i = 0;
    for (const QString& slug : slugs) {
        auto* cell = new QWidget(&sheet);
        auto* v = new QVBoxLayout(cell);
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(4);
        auto* art = new QLabel(cell);
        art->setAlignment(Qt::AlignCenter);
        art->setPixmap(GiftArt::pixmap(slug, 56));
        auto* nm = new QLabel(GiftArt::name(slug), cell);
        nm->setAlignment(Qt::AlignHCenter);
        nm->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:12px;background:transparent;"));
        auto* rar = new QLabel(GiftArt::rarityLabel(GiftArt::rarityFor(slug, QString())), cell);
        rar->setAlignment(Qt::AlignHCenter);
        rar->setStyleSheet(QStringLiteral("color:#726C82;font-size:10px;background:transparent;"));
        v->addWidget(art);
        v->addWidget(nm);
        v->addWidget(rar);
        grid->addWidget(cell, i / 4, i % 4);
        ++i;
    }
    sheet.resize(4 * 160 + 60, 4 * 150 + 40);
    sheet.show();
    for (int k = 0; k < 20; ++k) QCoreApplication::processEvents();
    const bool ok = sheet.grab().save(QStringLiteral("/tmp/gifts-catalog.png"));
    printf("%s gifts-catalog.png\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
