#pragma once
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

// ─────────────────────────────────────────────────────────────────────────────
//  RichDoc — блочная модель документа (RTE-01). Сообщение = компактный JSON
//  {"__xipher_rich":[блоки]}; блок: {t,x,l,items,checked,lang}.
//  Типы: h1/h2/h3 (заголовки), p (абзац), q (цитата), ul (список),
//  code (код-блок), hr (разделитель), chk (чекбокс). Inline-разметка внутри
//  текста — существующий markdown (жирный/курсив/спойлер), общий с plain.
//  Round-trip: fromJson(toJson(doc)) == doc (проверяется тестом).
// ─────────────────────────────────────────────────────────────────────────────
struct RichBlock {
    enum class Type { H1, H2, H3, Para, Quote, List, Code, Divider, Checkbox };
    Type     type = Type::Para;
    QString  text;                 // p/h/q/code/chk
    QStringList items;             // ul
    bool     checked = false;      // chk

    static const char* typeName(Type t);
    static Type typeFromName(const QString& n);
};

class RichDoc {
public:
    QList<RichBlock> blocks;

    bool isEmpty() const { return blocks.isEmpty(); }

    // Канонический JSON сообщения (компактный, ключи по одному символу).
    QJsonArray toJson() const;
    static RichDoc fromJson(const QJsonArray& arr);

    // Транспорт: content сообщения. isRich проверяет маркер.
    QString toMessage() const;
    static bool isRich(const QString& content);
    static RichDoc fromMessage(const QString& content, bool* ok = nullptr);

    // Fallback для plain-клиентов/копирования: markdown-представление.
    QString toPlainMarkdown() const;
};
