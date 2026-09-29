#include "ui/SuperSearchDialog.h"
#include "net/ApiClient.h"
#include "net/Models.h"

#include <QAbstractButton>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>

namespace {

// TYPE_MAP из supersearch.js: русские/английские ключевые слова → тип сообщения.
struct TypeMap { const char* prefix; const char* type; };
const TypeMap kTypeMap[] = {
    {"фото", "image"}, {"фотка", "image"}, {"фотки", "image"}, {"фоток", "image"},
    {"фотку", "image"}, {"фотографию", "image"}, {"картинк", "image"},
    {"изображен", "image"}, {"скрин", "image"}, {"скриншот", "image"},
    {"снимок", "image"}, {"image", "image"}, {"photo", "image"}, {"img", "image"},
    {"pic", "image"},
    {"файл", "file"}, {"документ", "file"}, {"док", "file"}, {"pdf", "file"},
    {"file", "file"}, {"document", "file"}, {"архив", "file"}, {"zip", "file"},
    {"голосов", "voice"}, {"аудио", "voice"}, {"войс", "voice"}, {"voice", "voice"},
    {"audio", "voice"}, {"голосовое", "voice"},
    {"видео", "video"}, {"video", "video"}, {"ролик", "video"}, {"клип", "video"},
    {"локац", "location"}, {"геолокац", "location"}, {"место", "location"},
    {"карт", "location"}, {"location", "location"}, {"map", "location"},
    {"ссылк", "_link"}, {"ссылку", "_link"}, {"линк", "_link"},
    {"link", "_link"}, {"url", "_link"},
};

const char* kStopWords[] = {
    "где","мы","обсуждали","обсуждал","обсуждала","обсуждать","найди","найти",
    "поиск","покажи","покажу","ищи","искать","сообщение","сообщения","сообщений",
    "месседж","он","она","оно","они","я","ты","вы","мне","меня","нас","что","как",
    "кто","когда","то","это","тот","этот","эта","в","на","с","за","из","по","от",
    "до","об","о","к","ко","и","а","но","или","не","да","нет","ли","же","бы",
    "сказал","сказала","написал","написала","отправил","отправила","было","были",
    "был","была","есть","будет","the","a","an","is","was","were","are","be","been",
    "find","search","where","did","we","discuss","said","wrote","with","for",
    "from","that","this","have","has","had","про","чата","чате","всё","все",
    "тут","здесь",
};

QString typeIcon(const QString& type) {
    if (type == QStringLiteral("image"))    return QStringLiteral("📷");
    if (type == QStringLiteral("file"))     return QStringLiteral("📎");
    if (type == QStringLiteral("voice"))    return QStringLiteral("🎤");
    if (type == QStringLiteral("video"))    return QStringLiteral("🎬");
    if (type == QStringLiteral("location") || type == QStringLiteral("live_location"))
                                            return QStringLiteral("📍");
    return QStringLiteral("💬");
}

} // namespace

SuperSearchDialog::SuperSearchDialog(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {
    setObjectName(QStringLiteral("superSearchOverlay"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#superSearchOverlay { background:rgba(5,4,8,0.55); }"
        "#ssCard { background:#131218; border:1px solid rgba(255,255,255,0.10);"
        "  border-radius:20px; }"
        "#ssTitle { font-size:16px; font-weight:700; color:#F3F1F8; }"
        "#ssInput { background:#1A1822; border:1px solid rgba(255,255,255,0.10);"
        "  border-radius:12px; min-height:42px; padding:0 14px; color:#F3F1F8; font-size:15px; }"
        "#ssInput:focus { border:1px solid #8B5CF6; }"
        "#ssSegBtn { background:transparent; border:none; border-radius:9px;"
        "  color:#726C82; font-size:13px; font-weight:600; min-height:30px; padding:0 14px; }"
        "#ssSegBtn:checked { background:#221F2C; color:#F3F1F8; }"
        "#ssChip { background:#1A1822; border:1px solid rgba(255,255,255,0.08);"
        "  border-radius:14px; color:#ACA6BD; font-size:12px; padding:5px 10px; }"
        "#ssChip:hover { background:rgba(139,92,246,0.16); color:#F3F1F8; }"
        "#ssChip:checked { background:rgba(139,92,246,0.28); color:#F3F1F8;"
        "  border-color:rgba(139,92,246,0.6); }"
        "#ssGroup { color:#9B82C9; font-size:11px; font-weight:800; letter-spacing:1px;"
        "  text-transform:uppercase; padding:10px 2px 2px; }"
        "#ssResult { background:#1A1822; border-radius:12px; }"
        "#ssResult:hover { background:#221F2C; }"
        "#ssHint { color:#726C82; font-size:12px; }"
        "#ssMeta { color:#726C82; font-size:11px; }"
        "#ssText { color:#F3F1F8; font-size:13px; }"));
    buildUi();

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(350);
    connect(debounce_, &QTimer::timeout, this, &SuperSearchDialog::onDebouncedSearch);
    connect(api_, &ApiClient::messagesSearched, this, &SuperSearchDialog::onResults);
}

void SuperSearchDialog::buildUi() {
    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("ssCard"));
    card->setFixedWidth(600);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(18, 14, 18, 14);
    cl->setSpacing(10);

    // Заголовок + сегмент режимов + закрыть (как xpss-header веба).
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("🔍  Поиск"));
    title->setObjectName(QStringLiteral("ssTitle"));
    auto* seg = new QHBoxLayout();
    seg->setSpacing(2);
    segNormal_ = new QPushButton(QStringLiteral("Обычный"));
    segNormal_->setObjectName(QStringLiteral("ssSegBtn"));
    segNormal_->setCheckable(true);
    segNormal_->setCursor(Qt::PointingHandCursor);
    segSuper_ = new QPushButton(QStringLiteral("Супер-поиск"));
    segSuper_->setObjectName(QStringLiteral("ssSegBtn"));
    segSuper_->setCheckable(true);
    segSuper_->setCursor(Qt::PointingHandCursor);
    segSuper_->setChecked(true);
    connect(segNormal_, &QAbstractButton::clicked, this, [this]() { setMode(false); });
    connect(segSuper_,  &QAbstractButton::clicked, this, [this]() { setMode(true); });
    seg->addWidget(segNormal_);
    seg->addWidget(segSuper_);
    auto* close = new QPushButton(QStringLiteral("✕"));
    close->setFixedSize(28, 28);
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet(QStringLiteral(
        "background:transparent;border:none;color:#726C82;font-size:15px;"));
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    head->addWidget(title);
    head->addStretch();
    head->addLayout(seg);
    head->addSpacing(8);
    head->addWidget(close);
    cl->addLayout(head);

    input_ = new QLineEdit();
    input_->setPlaceholderText(QStringLiteral("Поиск по сообщениям: «фото за неделю», «где обсуждали цену»…"));
    connect(input_, &QLineEdit::textChanged, this,
            [this](const QString& t) { if (t.trimmed().size() >= 2) debounce_->start(); });
    connect(input_, &QLineEdit::returnPressed, this, &SuperSearchDialog::onDebouncedSearch);
    cl->addWidget(input_);

    // Область: «В этом чате» / «Во всех чатах» (как xpss-miniseg веба).
    auto* metaRow = new QHBoxLayout();
    metaRow->setSpacing(6);
    scopeChatBtn_ = new QPushButton(QStringLiteral("В этом чате"));
    scopeChatBtn_->setObjectName(QStringLiteral("ssChip"));
    scopeChatBtn_->setCheckable(true);
    scopeChatBtn_->setCursor(Qt::PointingHandCursor);
    scopeAllBtn_ = new QPushButton(QStringLiteral("Во всех чатах"));
    scopeAllBtn_->setObjectName(QStringLiteral("ssChip"));
    scopeAllBtn_->setCheckable(true);
    scopeAllBtn_->setCursor(Qt::PointingHandCursor);
    connect(scopeChatBtn_, &QAbstractButton::clicked, this, [this]() { setScope(false); });
    connect(scopeAllBtn_,  &QAbstractButton::clicked, this, [this]() { setScope(true); });
    metaRow->addWidget(scopeChatBtn_);
    metaRow->addWidget(scopeAllBtn_);
    metaRow->addStretch();
    cl->addLayout(metaRow);

    // Чипы быстрых фильтров: фиксируют тип контента (как CHIPS веба).
    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    const QList<std::pair<QString, QString>> chipDefs = {
        {QStringLiteral("📷 Фото"),     QStringLiteral("image")},
        {QStringLiteral("📎 Файлы"),    QStringLiteral("file")},
        {QStringLiteral("🎤 Голосовые"),QStringLiteral("voice")},
        {QStringLiteral("🎬 Видео"),    QStringLiteral("video")},
        {QStringLiteral("🔗 Ссылки"),   QStringLiteral("_link")},
        {QStringLiteral("📍 Локации"),  QStringLiteral("location")},
    };
    for (const auto& d : chipDefs) {
        auto* b = new QPushButton(d.first);
        b->setObjectName(QStringLiteral("ssChip"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        const QString type = d.second;
        connect(b, &QAbstractButton::clicked, this, [this, b, type]() {
            pinnedType_ = (pinnedType_ == type) ? QString() : type;
            for (auto* other : typeChips_) other->setChecked(other == b && !pinnedType_.isEmpty());
            onDebouncedSearch();
        });
        typeChips_.append(b);
        chips->addWidget(b);
    }
    chips->addStretch();
    cl->addLayout(chips);

    auto* hint = new QLabel(QStringLiteral(
        "Супер-режим понимает естественный язык: «найди фотку паспорта», "
        "«где он сказал 'ок'». Обычный — просто ищет текст."));
    hint->setObjectName(QStringLiteral("ssHint"));
    hint->setWordWrap(true);
    cl->addWidget(hint);

    // Результаты.
    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setFixedHeight(380);
    sa->setStyleSheet(QStringLiteral(
        "QScrollArea{background:transparent;} QScrollBar:vertical{background:transparent;width:8px;margin:2px;}"
        "QScrollBar::handle:vertical{background:rgba(255,255,255,0.12);border-radius:4px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"));
    resultsBox_ = new QWidget();
    resultsBox_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rl = new QVBoxLayout(resultsBox_);
    rl->setContentsMargins(0, 0, 4, 0);
    rl->setSpacing(4);
    rl->addStretch();
    sa->setWidget(resultsBox_);
    cl->addWidget(sa);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch();
    auto* h = new QHBoxLayout();
    h->addStretch();
    h->addWidget(card);
    h->addStretch();
    outer->addLayout(h);
    outer->addStretch();
    card_ = card;
    hide();
}

void SuperSearchDialog::setMode(bool superMode) {
    superMode_ = superMode;
    segSuper_->setChecked(superMode);
    segNormal_->setChecked(!superMode);
    if (!input_->text().trimmed().isEmpty()) onDebouncedSearch();
}

void SuperSearchDialog::setScope(bool allChats) {
    scopeAll_ = allChats;
    scopeAllBtn_->setChecked(allChats);
    scopeChatBtn_->setChecked(!allChats);
    if (!input_->text().trimmed().isEmpty()) onDebouncedSearch();
}

void SuperSearchDialog::openFor(const QString& chatId, const QString& context) {
    chatId_ = chatId;
    context_ = context.isEmpty() ? QStringLiteral("dm") : context;
    // Без чата (вызов из пустого состояния) — сразу глобальная область.
    scopeAll_ = chatId_.isEmpty() ? true : scopeAll_;
    scopeAllBtn_->setChecked(scopeAll_);
    scopeChatBtn_->setChecked(!scopeAll_);
    scopeChatBtn_->setEnabled(!chatId_.isEmpty());
    clearResults();
    input_->clear();
    show();
    raise();
    input_->setFocus();
}

void SuperSearchDialog::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { hide(); return; }
    QWidget::keyPressEvent(e);
}

SuperSearchDialog::Parsed SuperSearchDialog::parseQuery(const QString& raw, bool super) const {
    QString q = raw.trimmed();
    Parsed p;
    if (!super) { p.keywords = q; return p; }   // обычный режим: текст как есть

    // 1) Кавычки → точная фраза.
    static const QRegularExpression quoteRe(QStringLiteral("[\"'«]([^\"'»]+)[\"'»]"));
    const auto qm = quoteRe.match(q);
    QString quoted;
    if (qm.hasMatch()) {
        quoted = qm.captured(1).trimmed();
        q.remove(qm.capturedStart(), qm.capturedLength());
        q = q.trimmed();
    }
    // 2) Ключевое слово типа.
    const QStringList words = q.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& w : words) {
        for (const TypeMap& tm : kTypeMap) {
            const QString pref = QLatin1String(tm.prefix);
            if (w.startsWith(pref) || w == pref) {
                p.type = QLatin1String(tm.type);
                q.remove(QRegularExpression(
                    QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(w)),
                    QRegularExpression::CaseInsensitiveOption));
                q = q.trimmed();
                break;
            }
        }
        if (!p.type.isEmpty()) break;
    }
    if (p.type == QStringLiteral("_link")) {
        p.type.clear();
        if (quoted.isEmpty()) quoted = QStringLiteral("http");
    }
    // 3) Стоп-слова.
    QStringList kept;
    for (const QString& w : q.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString lw = w.toLower().remove(QRegularExpression(QStringLiteral("[.,!?;:]")));
        if (lw.size() > 1) {
            bool stop = false;
            for (const char* s : kStopWords) if (lw == QLatin1String(s)) { stop = true; break; }
            if (!stop) kept += w;
        }
    }
    p.keywords = !quoted.isEmpty() ? quoted
               : !kept.isEmpty()   ? kept.join(QLatin1Char(' '))
               : raw.trimmed();
    return p;
}

void SuperSearchDialog::onDebouncedSearch() {
    const QString val = input_->text().trimmed();
    if (val.size() < 2 && pinnedType_.isEmpty()) { clearResults(); return; }
    Parsed p = parseQuery(val, superMode_);
    if (!pinnedType_.isEmpty()) {
        // Чип-тип главнее парсера; для ссылок ищем подстроку http.
        p.type = pinnedType_ == QStringLiteral("_link") ? QString() : pinnedType_;
        if (pinnedType_ == QStringLiteral("_link") && p.keywords.size() < 2)
            p.keywords = QStringLiteral("http");
    }
    doSearch(p.keywords, p.type);
}

void SuperSearchDialog::doSearch(const QString& keywords, const QString& type) {
    lastKeywords_ = keywords;
    lastType_ = type;
    clearResults();
    if (keywords.size() < 2 && type.isEmpty()) return;

    if (!scopeAll_ && !chatId_.isEmpty()) {
        // Один чат — обычный запрос с пагинацией на сервере (50 на страницу).
        activeReqIds_.insert(QStringLiteral("ssd_%1").arg(++seq_));
        ++pending_;
        api_->searchMessages(chatId_, context_, keywords, type, 0, 50);
        return;
    }
    // Во всех чатах: параллельный обход до 16 чатов по 10 результатов
    // (1:1 с searchAcross веба: каналы и «Избранное» не ищем).
    QList<QPair<QString, QString>> targets;   // (id, context)
    if (!chatId_.isEmpty()) {
        targets.append({chatId_, context_});
    }
    for (const Chat& c : chats_) {
        if (c.id == chatId_) continue;
        if (c.kind == ChatKind::Channel || c.isSaved) continue;
        targets.append({c.id, c.kind == ChatKind::Group
            ? QStringLiteral("group") : QStringLiteral("dm")});
        if (targets.size() >= 16) break;
    }
    for (const auto& t : targets) {
        activeReqIds_.insert(QStringLiteral("ssd_%1").arg(++seq_));
        ++pending_;
        api_->searchMessages(t.first, t.second, keywords, type, 0, 10);
    }
}

void SuperSearchDialog::clearResults() {
    acc_.clear();
    activeReqIds_.clear();
    pending_ = 0;
    while (resultsBox_->layout()->count() > 1) {
        QLayoutItem* it = resultsBox_->layout()->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
}

void SuperSearchDialog::addChatGroupHeader(const QString& title) {
    auto* l = new QLabel(title);
    l->setObjectName(QStringLiteral("ssGroup"));
    auto* lay = static_cast<QVBoxLayout*>(resultsBox_->layout());
    lay->insertWidget(lay->count() - 1, l);
}

void SuperSearchDialog::addResultRow(const QJsonObject& m) {
    const QString type = m.value(QStringLiteral("message_type")).toString(QStringLiteral("text"));
    const QString id = m.value(QStringLiteral("id")).toString();
    const QString chatId = m.value(QStringLiteral("__chat_id")).toString(chatId_);
    const bool isMedia = type != QStringLiteral("text");
    const QString kw = lastKeywords_;

    QString preview;
    if (isMedia)
        preview = QStringLiteral("%1 %2").arg(typeIcon(type),
            m.value(QStringLiteral("file_name")).toString(type));
    else
        preview = m.value(QStringLiteral("content")).toString();

    auto* row = new QWidget();
    row->setObjectName(QStringLiteral("ssResult"));
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(10, 8, 12, 8);
    hl->setSpacing(10);
    auto* ic = new QLabel(typeIcon(type), row);
    ic->setFixedSize(24, 24);
    ic->setAlignment(Qt::AlignCenter);
    ic->setStyleSheet(QStringLiteral("font-size:16px;"));
    hl->addWidget(ic);
    auto* body = new QVBoxLayout();
    body->setSpacing(1);
    const QString sender = m.value(QStringLiteral("sent")).toBool()
        ? QStringLiteral("Вы") : m.value(QStringLiteral("sender_username")).toString();
    const QString when = m.value(QStringLiteral("created_at")).toString().left(10)
                       + QLatin1Char(' ') + m.value(QStringLiteral("time")).toString();
    auto* meta = new QLabel(QStringLiteral("%1  •  %2").arg(sender, when), row);
    meta->setObjectName(QStringLiteral("ssMeta"));
    body->addWidget(meta);
    auto* txt = new QLabel(preview, row);
    txt->setObjectName(QStringLiteral("ssText"));
    txt->setWordWrap(true);
    if (!kw.isEmpty() && !isMedia) {
        // Подсветка совпадения, как ss-mark веба.
        const int at = txt->text().toLower().indexOf(kw.toLower());
        if (at >= 0) {
            const QString esc = txt->text();
            txt->setText(QStringLiteral("%1<span style=\"background:rgba(139,92,246,0.45);\">%2</span>%3")
                .arg(esc.left(at).toHtmlEscaped(), esc.mid(at, kw.size()).toHtmlEscaped(),
                     esc.mid(at + kw.size()).toHtmlEscaped()));
            txt->setTextFormat(Qt::RichText);
        }
    }
    body->addWidget(txt);
    hl->addLayout(body, 1);

    row->setCursor(Qt::PointingHandCursor);
    row->installEventFilter(new SuperSearchClickFilter([this, chatId, id]() {
        emit resultPicked(chatId, id);
        hide();
    }, row));

    auto* lay = static_cast<QVBoxLayout*>(resultsBox_->layout());
    lay->insertWidget(lay->count() - 1, row);
}

void SuperSearchDialog::onResults(const QString& requestId, const QJsonArray& messages) {
    if (!activeReqIds_.contains(requestId)) return;   // устаревший ответ
    activeReqIds_.remove(requestId);
    const bool single = !scopeAll_ && !chatId_.isEmpty();

    if (single) {
        // Одиночный чат: рисуем как есть.
        auto* lay = static_cast<QVBoxLayout*>(resultsBox_->layout());
        if (messages.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("Ничего не найдено"));
            empty->setAlignment(Qt::AlignCenter);
            empty->setStyleSheet(QStringLiteral("color:#726C82;padding:24px;font-size:14px;"));
            lay->insertWidget(lay->count() - 1, empty);
            return;
        }
        for (const QJsonValue& v : messages) addResultRow(v.toObject());
        return;
    }

    // «Во всех чатах»: каждый ответ — один чат; копим группы, рисуем,
    // когда завершатся все запросы (pending_).
    if (!messages.isEmpty()) {
        const QString cid = messages.first().toObject()
                                .value(QStringLiteral("__chat_id")).toString();
        QString cname;
        for (const Chat& c : chats_)
            if (c.id == cid) { cname = c.displayName; break; }
        acc_.append({cid, QString(), cname, messages});
    }
    if (--pending_ > 0) return;

    auto* lay = static_cast<QVBoxLayout*>(resultsBox_->layout());
    if (acc_.isEmpty()) {
        auto* empty = new QLabel(QStringLiteral("Ничего не найдено"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color:#726C82;padding:24px;font-size:14px;"));
        lay->insertWidget(lay->count() - 1, empty);
        return;
    }
    std::sort(acc_.begin(), acc_.end(), [](const Acc& a, const Acc& b) {
        const QString da = a.msgs.first().toObject().value(QStringLiteral("created_at")).toString();
        const QString db = b.msgs.first().toObject().value(QStringLiteral("created_at")).toString();
        return da > db;
    });
    for (const Acc& g : acc_) {
        addChatGroupHeader(g.name.isEmpty() ? QStringLiteral("Чат") : g.name);
        for (const QJsonValue& v : g.msgs) addResultRow(v.toObject());
    }
    acc_.clear();
}

// ── Простой клик-фильтр для строк результата ─────────────────────────────────
bool SuperSearchClickFilter::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease) cb_();
    return QObject::eventFilter(obj, e);
}
