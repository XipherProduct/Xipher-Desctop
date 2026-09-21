#include "ui/SuperSearchDialog.h"
#include "net/ApiClient.h"

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
        "#ssInput { background:#1A1822; border:1px solid rgba(255,255,255,0.10);"
        "  border-radius:12px; min-height:42px; padding:0 14px; color:#F3F1F8; font-size:15px; }"
        "#ssInput:focus { border:1px solid #8B5CF6; }"
        "#ssChip { background:#1A1822; border:1px solid rgba(255,255,255,0.08);"
        "  border-radius:14px; color:#ACA6BD; font-size:12px; padding:5px 10px; }"
        "#ssChip:hover { background:rgba(139,92,246,0.16); color:#F3F1F8; }"
        "#ssResult { background:#1A1822; border-radius:12px; }"
        "#ssResult:hover { background:#221F2C; }"
        "#ssHint { color:#726C82; font-size:12px; }"
        "#ssMark { background:rgba(139,92,246,0.35); color:#F3F1F8; }"
        "#ssMore { background:transparent; border:1px solid rgba(139,92,246,0.4);"
        "  border-radius:10px; color:#BBA4FF; min-height:34px; padding:0 18px; }"
        "#ssMore:hover { background:rgba(139,92,246,0.12); }"));
    buildUi();

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(400);
    connect(debounce_, &QTimer::timeout, this, &SuperSearchDialog::onDebouncedSearch);
    connect(api_, &ApiClient::messagesSearched, this, &SuperSearchDialog::onResults);
}

void SuperSearchDialog::buildUi() {
    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("ssCard"));
    card->setFixedWidth(560);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(18, 14, 18, 14);
    cl->setSpacing(10);

    // Заголовок + закрыть.
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("🔍  Супер-поиск"));
    title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:#F3F1F8;"));
    auto* close = new QPushButton(QStringLiteral("✕"));
    close->setFixedSize(28, 28);
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet(QStringLiteral(
        "background:transparent;border:none;color:#726C82;font-size:15px;"));
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    head->addWidget(title);
    head->addStretch();
    head->addWidget(close);
    cl->addLayout(head);

    input_ = new QLineEdit();
    input_->setPlaceholderText(QStringLiteral("Искать «текст», фото, файлы, ссылки…"));
    connect(input_, &QLineEdit::textChanged, this,
            [this](const QString& t) { if (t.trimmed().size() >= 2) debounce_->start(); });
    connect(input_, &QLineEdit::returnPressed, this, &SuperSearchDialog::onDebouncedSearch);
    cl->addWidget(input_);

    // Чипы быстрых фильтров (как CHIPS в вебе).
    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    const QList<std::pair<QString, QPair<QString, QString>>> chipDefs = {
        {QStringLiteral("📷 Фото"),     {QStringLiteral(""),  QStringLiteral("image")}},
        {QStringLiteral("📎 Файлы"),    {QStringLiteral(""),  QStringLiteral("file")}},
        {QStringLiteral("🎤 Голосовые"),{QStringLiteral(""),  QStringLiteral("voice")}},
        {QStringLiteral("🔗 Ссылки"),   {QStringLiteral("http"), QStringLiteral("")}},
        {QStringLiteral("📍 Локации"),  {QStringLiteral(""),  QStringLiteral("location")}},
    };
    for (const auto& d : chipDefs) {
        auto* b = new QPushButton(d.first);
        b->setObjectName(QStringLiteral("ssChip"));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, d]() {
            input_->clear();
            doSearch(d.second.first, d.second.second);
        });
        chips->addWidget(b);
    }
    chips->addStretch();
    cl->addLayout(chips);

    auto* hint = new QLabel(QStringLiteral(
        "Примеры: «где обсуждали цену?» • «найди фотку паспорта» • «найди где он сказал 'ок'»"));
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

    moreBtn_ = new QPushButton(QStringLiteral("Загрузить ещё"));
    moreBtn_->setObjectName(QStringLiteral("ssMore"));
    moreBtn_->setCursor(Qt::PointingHandCursor);
    moreBtn_->hide();
    connect(moreBtn_, &QPushButton::clicked, this, &SuperSearchDialog::loadMore);
    cl->addWidget(moreBtn_, 0, Qt::AlignHCenter);

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

void SuperSearchDialog::openFor(const QString& chatId, const QString& context) {
    chatId_ = chatId;
    context_ = context.isEmpty() ? QStringLiteral("dm") : context;
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

SuperSearchDialog::Parsed SuperSearchDialog::parseQuery(const QString& raw) const {
    QString q = raw.trimmed();
    Parsed p;
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
    if (val.size() < 2) { clearResults(); return; }
    const Parsed p = parseQuery(val);
    doSearch(p.keywords, p.type);
}

void SuperSearchDialog::doSearch(const QString& keywords, const QString& type) {
    if (chatId_.isEmpty()) return;
    lastKeywords_ = keywords;
    lastType_ = type;
    clearResults();
    setBusy(true);
    reqId_ = QStringLiteral("ssd_%1").arg(++seq_);
    api_->searchMessages(chatId_, context_, keywords, type, 0, 50);
}

void SuperSearchDialog::loadMore() {
    if (chatId_.isEmpty()) return;
    reqId_ = QStringLiteral("ssd_%1").arg(++seq_);
    api_->searchMessages(chatId_, context_, lastKeywords_, lastType_, offset_, 50);
}

void SuperSearchDialog::setBusy(bool busy) { searching_ = busy; }

void SuperSearchDialog::clearResults() {
    offset_ = 0;
    moreBtn_->hide();
    while (resultsBox_->layout()->count() > 1) {
        QLayoutItem* it = resultsBox_->layout()->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
}

void SuperSearchDialog::onResults(const QString& requestId, const QJsonArray& messages) {
    if (requestId != reqId_) return;   // устаревший ответ
    searching_ = false;
    auto* lay = static_cast<QVBoxLayout*>(resultsBox_->layout());
    int insertAt = lay->count() - 1;   // перед финальным stretch

    if (messages.isEmpty() && lay->count() == 1) {
        auto* empty = new QLabel(QStringLiteral("Ничего не найдено"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color:#726C82;padding:24px;font-size:14px;"));
        lay->insertWidget(insertAt++, empty);
        return;
    }

    for (const QJsonValue& v : messages) {
        const QJsonObject m = v.toObject();
        const QString type = m.value(QStringLiteral("message_type")).toString(QStringLiteral("text"));
        const bool isMedia = type != QStringLiteral("text");
        const QString kw = lastKeywords_;

        // Превью: медиа — имя файла; текст — контент с подсветкой.
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
        meta->setStyleSheet(QStringLiteral("color:#726C82;font-size:11px;"));
        body->addWidget(meta);
        auto* txt = new QLabel(preview, row);
        txt->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:13px;"));
        txt->setWordWrap(true);
        body->addWidget(txt);
        hl->addLayout(body, 1);

        const QString id = m.value(QStringLiteral("id")).toString();
        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(new SuperSearchClickFilter([this, id]() {
            emit resultPicked(id);
            hide();
        }, row));

        lay->insertWidget(insertAt++, row);
    }
    offset_ += messages.size();
    moreBtn_->setVisible(messages.size() >= 50);
}

// ── Простой клик-фильтр для строк результата ─────────────────────────────────
bool SuperSearchClickFilter::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease) cb_();
    return QObject::eventFilter(obj, e);
}
