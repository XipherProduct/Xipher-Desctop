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

// RTE-04: HTML-экспорт — токены из токенов приложения (тёмная тема, теги).
QString RichDoc::toHtml() const {
    QString body;
    for (const RichBlock& b : blocks) {
        const QString esc = b.text.toHtmlEscaped();
        switch (b.type) {
            case RichBlock::Type::H1: body += QStringLiteral("<h2>%1</h2>").arg(esc); break;
            case RichBlock::Type::H2: body += QStringLiteral("<h3>%1</h3>").arg(esc); break;
            case RichBlock::Type::H3: body += QStringLiteral("<h4>%1</h4>").arg(esc); break;
            case RichBlock::Type::Para:     body += QStringLiteral("<p>%1</p>").arg(esc); break;
            case RichBlock::Type::Quote:
                body += QStringLiteral("<blockquote>%1</blockquote>").arg(esc); break;
            case RichBlock::Type::List: {
                body += QStringLiteral("<ul>");
                for (const QString& i : b.items)
                    body += QStringLiteral("<li>%1</li>").arg(i.toHtmlEscaped());
                body += QStringLiteral("</ul>");
                break;
            }
            case RichBlock::Type::Code:
                body += QStringLiteral("<pre><code>%1</code></pre>").arg(esc); break;
            case RichBlock::Type::Divider:
                body += QStringLiteral("<hr>"); break;
            case RichBlock::Type::Checkbox:
                body += QStringLiteral("<p>%1 %2</p>")
                    .arg(b.checked ? QStringLiteral("☑") : QStringLiteral("☐"), esc); break;
        }
    }
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>body{background:#131218;color:#F3F1F8;font-family:system-ui;"
        "max-width:720px;margin:24px auto;padding:0 16px;line-height:1.5}"
        "blockquote{border-left:3px solid #8B5CF6;margin:8px 0;padding:4px 12px;"
        "color:#ACA6BD}pre{background:#0B0A0E;border-radius:8px;padding:10px;"
        "overflow:auto}h2,h3,h4{margin:16px 0 8px}hr{border:none;"
        "border-top:1px solid rgba(255,255,255,0.15)}</style>"
        "</head><body>%1</body></html>").arg(body);
}

// RTE-05: markdown → блоки (для .md-аттачей и Instant View, IVW-01).
RichDoc RichDoc::fromMarkdown(const QString& md) {
    RichDoc d;
    const auto lines = md.split(QLatin1Char('\n'));
    RichBlock code;
    bool inCode = false;
    auto flushPara = [&](QString& para) {
        if (!para.trimmed().isEmpty()) {
            RichBlock p;
            p.text = para.trimmed();
            d.blocks.append(p);
        }
        para.clear();
    };
    QString para;
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.startsWith(QStringLiteral("```"))) {
            if (inCode) {
                d.blocks.append(code);
                inCode = false;
            } else {
                flushPara(para);
                code = RichBlock();
                code.type = RichBlock::Type::Code;
                inCode = true;
            }
            continue;
        }
        if (inCode) {
            if (!code.text.isEmpty()) code.text += QLatin1Char('\n');
            code.text += raw;
            continue;
        }
        if (l.isEmpty()) { flushPara(para); continue; }
        if (l == QStringLiteral("---")) {
            flushPara(para);
            RichBlock hr;
            hr.type = RichBlock::Type::Divider;
            d.blocks.append(hr);
            continue;
        }
        if (l.startsWith(QStringLiteral("## "))) {
            flushPara(para);
            RichBlock h; h.type = RichBlock::Type::H1; h.text = l.mid(3);
            d.blocks.append(h); continue;
        }
        if (l.startsWith(QStringLiteral("### "))) {
            flushPara(para);
            RichBlock h; h.type = RichBlock::Type::H2; h.text = l.mid(4);
            d.blocks.append(h); continue;
        }
        if (l.startsWith(QStringLiteral("#### "))) {
            flushPara(para);
            RichBlock h; h.type = RichBlock::Type::H3; h.text = l.mid(5);
            d.blocks.append(h); continue;
        }
        if (l.startsWith(QStringLiteral("> "))) {
            flushPara(para);
            RichBlock q; q.type = RichBlock::Type::Quote; q.text = l.mid(2);
            d.blocks.append(q); continue;
        }
        if (l.startsWith(QStringLiteral("- [x] ")) || l.startsWith(QStringLiteral("- [ ] "))) {
            flushPara(para);
            RichBlock cb;
            cb.type = RichBlock::Type::Checkbox;
            cb.checked = l.startsWith(QStringLiteral("- [x] "));
            cb.text = l.mid(6);
            d.blocks.append(cb);
            continue;
        }
        if (l.startsWith(QStringLiteral("- ")) || l.startsWith(QStringLiteral("* "))) {
            flushPara(para);
            // Сглаживаем последовательные пункты в один список.
            if (!d.blocks.isEmpty()
                && d.blocks.last().type == RichBlock::Type::List) {
                d.blocks.last().items.append(l.mid(2));
            } else {
                RichBlock ul;
                ul.type = RichBlock::Type::List;
                ul.items.append(l.mid(2));
                d.blocks.append(ul);
            }
            continue;
        }
        para += (para.isEmpty() ? QString() : QStringLiteral(" ")) + l;
    }
    if (inCode) d.blocks.append(code);
    flushPara(para);
    return d;
}
