#include "ui/RichDoc.h"

namespace {
constexpr const char* kMarker = "__xipher_rich";
}

const char* RichBlock::typeName(Type t) {
    switch (t) {
        case Type::H1: return "h1";
        case Type::H2: return "h2";
        case Type::H3: return "h3";
        case Type::Para: return "p";
        case Type::Quote: return "q";
        case Type::List: return "ul";
        case Type::Code: return "code";
        case Type::Divider: return "hr";
        case Type::Checkbox: return "chk";
    }
    return "p";
}

RichBlock::Type RichBlock::typeFromName(const QString& n) {
    if (n == QLatin1String("h1")) return Type::H1;
    if (n == QLatin1String("h2")) return Type::H2;
    if (n == QLatin1String("h3")) return Type::H3;
    if (n == QLatin1String("q"))  return Type::Quote;
    if (n == QLatin1String("ul")) return Type::List;
    if (n == QLatin1String("code")) return Type::Code;
    if (n == QLatin1String("hr")) return Type::Divider;
    if (n == QLatin1String("chk")) return Type::Checkbox;
    return Type::Para;
}

QJsonArray RichDoc::toJson() const {
    QJsonArray arr;
    for (const RichBlock& b : blocks) {
        QJsonObject o;
        o.insert(QStringLiteral("t"), QLatin1String(RichBlock::typeName(b.type)));
        if (b.type == RichBlock::Type::List) {
            QJsonArray it;
            for (const QString& s : b.items) it.append(s);
            o.insert(QStringLiteral("items"), it);
        } else if (b.type == RichBlock::Type::Divider) {
            // без текста
        } else {
            o.insert(QStringLiteral("x"), b.text);
            if (b.type == RichBlock::Type::Checkbox)
                o.insert(QStringLiteral("checked"), b.checked);
        }
        arr.append(o);
    }
    return arr;
}

RichDoc RichDoc::fromJson(const QJsonArray& arr) {
    RichDoc d;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        RichBlock b;
        b.type = RichBlock::typeFromName(o.value(QStringLiteral("t")).toString());
        b.text = o.value(QStringLiteral("x")).toString();
        b.checked = o.value(QStringLiteral("checked")).toBool(false);
        for (const QJsonValue& i : o.value(QStringLiteral("items")).toArray())
            b.items.append(i.toString());
        d.blocks.append(b);
    }
    return d;
}

QString RichDoc::toMessage() const {
    QJsonObject root;
    root.insert(QLatin1String(kMarker), toJson());
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool RichDoc::isRich(const QString& content) {
    return content.startsWith(QLatin1String("{\"__xipher_rich\":"));
}

RichDoc RichDoc::fromMessage(const QString& content, bool* ok) {
    if (ok) *ok = false;
    if (!isRich(content)) return RichDoc();
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) return RichDoc();
    const QJsonArray arr = doc.object().value(QLatin1String(kMarker)).toArray();
    if (arr.isEmpty()) return RichDoc();
    if (ok) *ok = true;
    return fromJson(arr);
}

QString RichDoc::toPlainMarkdown() const {
    QStringList md;
    for (const RichBlock& b : blocks) {
        switch (b.type) {
            case RichBlock::Type::H1: md << QStringLiteral("## %1").arg(b.text); break;
            case RichBlock::Type::H2: md << QStringLiteral("### %1").arg(b.text); break;
            case RichBlock::Type::H3: md << QStringLiteral("#### %1").arg(b.text); break;
            case RichBlock::Type::Quote: md << QStringLiteral("> %1").arg(b.text); break;
            case RichBlock::Type::List:
                for (const QString& s : b.items) md << QStringLiteral("- %1").arg(s);
                break;
            case RichBlock::Type::Code:
                md << QStringLiteral("```") << b.text << QStringLiteral("```");
                break;
            case RichBlock::Type::Divider: md << QStringLiteral("---"); break;
            case RichBlock::Type::Checkbox:
                md << QStringLiteral("%1 %2").arg(b.checked ? QStringLiteral("[x]")
                                                            : QStringLiteral("[ ]"), b.text);
                break;
            case RichBlock::Type::Para: md << b.text; break;
        }
        md << QString();   // пустая строка между блоками
    }
    while (!md.isEmpty() && md.last().isEmpty()) md.removeLast();
    return md.join(QLatin1Char('\n'));
}
