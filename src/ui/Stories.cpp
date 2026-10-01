#include "ui/Stories.h"
#include "ui/AvatarUtil.h"
#include "ui/SuperSearchDialog.h"
#include "net/ApiClient.h"
#include "net/Session.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// ── Разбор /api/stories/all → группы (как groupStories в stories.src.js) ─────
QList<StoryUserGroup> groupStoriesPayload(const QJsonObject& data, const QString& myId,
                                          bool* isPremiumOut) {
    if (isPremiumOut)
        *isPremiumOut = data.value(QStringLiteral("is_premium")).toBool(false);
    QHash<QString, StoryUserGroup> byUser;
    QList<QString> order;
    for (const QJsonValue& v : data.value(QStringLiteral("stories")).toArray()) {
        const QJsonObject o = v.toObject();
        StoryItem s;
        s.id        = o.value(QStringLiteral("id")).toString();
        s.userId    = o.value(QStringLiteral("user_id")).toString();
        s.username  = o.value(QStringLiteral("username")).toString();
        s.avatarUrl = o.value(QStringLiteral("avatar_url")).toString();
        s.mediaUrl  = o.value(QStringLiteral("media_url")).toString();
        s.mediaType = o.value(QStringLiteral("media_type")).toString(QStringLiteral("image"));
        s.caption   = o.value(QStringLiteral("caption")).toString();
        s.createdAt = o.value(QStringLiteral("created_at")).toString();
        s.isViewed  = o.value(QStringLiteral("is_viewed")).toBool(false);
        s.keyB64    = o.value(QStringLiteral("encryption_key")).toString();
        s.ivB64     = o.value(QStringLiteral("encryption_iv")).toString();
        s.isOwn     = s.userId == myId;
        if (!byUser.contains(s.userId)) {
            StoryUserGroup g;
            g.userId = s.userId; g.username = s.username; g.avatarUrl = s.avatarUrl;
            g.isOwn = s.isOwn;
            byUser.insert(s.userId, g);
            order.append(s.userId);
        }
        StoryUserGroup& g = byUser[s.userId];
        if (g.stories.isEmpty()) {
            g.username = s.username;      // на случай пустого first
            g.avatarUrl = s.avatarUrl;
        }
        if (!s.isViewed && !s.isOwn) g.hasUnread = true;
        g.stories.append(s);
    }
    QList<StoryUserGroup> groups;
    // Свои — первыми (веб ставит myStories в начало), дальше: непрочитанные сверху,
    // внутри — по свежести.
    StoryUserGroup mine;
    bool haveMine = false;
    for (const QString& uid : order) {
        StoryUserGroup g = byUser.value(uid);
        std::sort(g.stories.begin(), g.stories.end(),
                  [](const StoryItem& a, const StoryItem& b) {
                      return a.createdAt < b.createdAt;
                  });
        if (g.isOwn) { mine = g; haveMine = true; }
        else groups.append(g);
    }
    std::sort(groups.begin(), groups.end(),
              [](const StoryUserGroup& a, const StoryUserGroup& b) {
                  if (a.hasUnread != b.hasUnread) return a.hasUnread;
                  const QString at = a.stories.isEmpty() ? QString()
                                   : a.stories.last().createdAt;
                  const QString bt = b.stories.isEmpty() ? QString()
                                   : b.stories.last().createdAt;
                  return at > bt;
              });
    if (haveMine) groups.prepend(mine);
    return groups;
}

// Шифрование историй удалено: медиа приходит открытым текстом. Старые
// записи с ключами показываем заглушкой (расшифровать их больше нечем).
static QByteArray decryptStoryMedia(const StoryItem& s, const QByteArray& bytes) {
    if (s.keyB64.isEmpty() && s.ivB64.isEmpty()) return bytes;   // обычная история
    return {};   // старая зашифрованная — не показываем битую картинку
}

// Загрузка медиа по media_url (с токеном), затем расшифровка.
static void loadStoryMedia(ApiClient* api, const StoryItem& s,
                           std::function<void(const QByteArray&)> done) {
    QUrl url(s.mediaUrl);
    if (s.mediaUrl.startsWith(QLatin1String("/files")))
        url = QUrl(QStringLiteral("https://messenger.xipher.pro") + s.mediaUrl);
    QNetworkRequest req(url);
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + Session::instance().token.toUtf8());
    QNetworkReply* reply = api->nam()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, s, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done({}); return; }
        done(decryptStoryMedia(s, reply->readAll()));
    });
}

// ═══════════════════════════ StoriesBar ══════════════════════════════════════

StoriesBar::StoriesBar(QWidget* parent) : QWidget(parent) {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(10, 6, 10, 6);
    l->setSpacing(10);
    l->addStretch();
}

void StoriesBar::applyData(const QList<StoryUserGroup>& groups) {
    // Пересобрать плитки (оставив финальный stretch).
    QLayoutItem* it;
    while ((it = layout()->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    auto* lay = static_cast<QHBoxLayout*>(layout());
    int idx = 0;
    for (const StoryUserGroup& g : groups) {
        QWidget* t = makeTile(g, idx++);
        lay->insertWidget(lay->count() - 1, t);
    }
    setVisible(true);
}

QWidget* StoriesBar::makeTile(const StoryUserGroup& g, int index) {
    // Кольцо + аватар + подпись — 1:1 с .story-ring/.story-avatar веба.
    // Аватар — ОТДЕЛЬНЫЙ лейбл внутри кольца: раньше setRound сжимал сам
    // лейбл-кольцо до 52px, и рамка 2px срезала картинку сверху/справа.
    auto* col = new QWidget();
    auto* v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto* ring = new QWidget(col);
    ring->setFixedSize(56, 56);
    const QString ringColor = g.stories.isEmpty() ? QStringLiteral("rgba(255,255,255,0.14)")
                            : (g.hasUnread ? QStringLiteral("#8B5CF6")
                                           : QStringLiteral("rgba(255,255,255,0.22)"));
    ring->setStyleSheet(QStringLiteral(
        "border:2px solid %1;border-radius:30px;background:#1A1822;")
        .arg(ringColor));
    auto* ringLay = new QVBoxLayout(ring);
    ringLay->setContentsMargins(0, 0, 0, 0);
    bool hasAvatar = false;
    if (!g.avatarUrl.isEmpty() && !g.stories.isEmpty()) {
        auto* av = new QLabel(ring);
        av->setAttribute(Qt::WA_TransparentForMouseEvents);
        Avatar::setRound(av, g.avatarUrl, g.username.left(1).toUpper(), 46);
        ringLay->addWidget(av, 0, Qt::AlignCenter);
        hasAvatar = true;
    }
    if (!hasAvatar) {
        auto* ph = new QLabel(g.stories.isEmpty() ? QStringLiteral("＋")
                              : g.username.left(1).toUpper(), ring);
        ph->setAttribute(Qt::WA_TransparentForMouseEvents);
        ph->setAlignment(Qt::AlignCenter);
        ph->setStyleSheet(QStringLiteral(
            "font-size:20px;font-weight:700;color:#F3F1F8;background:transparent;"));
        ringLay->addWidget(ph);
    }
    v->addWidget(ring, 0, Qt::AlignHCenter);

    auto* name = new QLabel(g.isOwn
        ? (g.stories.isEmpty() ? QStringLiteral("Добавить") : QStringLiteral("Моя история"))
        : g.username, col);
    name->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:10px;"));
    name->setFixedWidth(68);
    name->setAlignment(Qt::AlignCenter);
    v->addWidget(name, 0, Qt::AlignHCenter);

    col->setCursor(Qt::PointingHandCursor);
    col->installEventFilter(new SuperSearchClickFilter([this, index, g]() {
        if (g.stories.isEmpty() && g.isOwn) emit addRequested();
        else if (!g.stories.isEmpty()) emit userClicked(index);
    }, col));
    return col;
}

void StoriesBar::clear() {
    QLayoutItem* it;
    while ((it = layout()->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    qobject_cast<QHBoxLayout*>(layout())->addStretch();
}

// ═══════════════════════════ StoriesViewer ═══════════════════════════════════

StoriesViewer::StoriesViewer(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {
    setObjectName(QStringLiteral("storiesViewer"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#storiesViewer { background:rgba(4,3,8,0.94); }"));
    buildUi();
    hide();
}

void StoriesViewer::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new QWidget(this);
    card_->setObjectName(QStringLiteral("storyCard"));
    card_->setStyleSheet(QStringLiteral(
        "#storyCard { background:#0B0A0E; border:1px solid rgba(255,255,255,0.10);"
        "  border-radius:22px; }"));
    card_->setFixedSize(420, 700);
    auto* cl = new QVBoxLayout(card_);
    cl->setContentsMargins(12, 12, 12, 12);
    cl->setSpacing(8);

    // Прогресс-сегменты (как .story-progress в вебе).
    progressRow_ = new QWidget(card_);
    auto* pr = new QHBoxLayout(progressRow_);
    pr->setContentsMargins(0, 0, 0, 0);
    pr->setSpacing(4);
    cl->addWidget(progressRow_);

    media_ = new QLabel(card_);
    media_->setAlignment(Qt::AlignCenter);
    media_->setStyleSheet(QStringLiteral(
        "background:#131218;border-radius:14px;color:#726C82;font-size:14px;"));
    media_->setText(QStringLiteral("Загрузка…"));
    cl->addWidget(media_, 1);

    meta_ = new QLabel(card_);
    meta_->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:12px;"));
    cl->addWidget(meta_);

    caption_ = new QLabel(card_);
    caption_->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:14px;"));
    caption_->setWordWrap(true);
    caption_->hide();
    cl->addWidget(caption_);

    // Ответ (в личку автору) + реакции.
    auto* act = new QHBoxLayout();
    replyEdit_ = new QLineEdit(card_);
    replyEdit_->setPlaceholderText(QStringLiteral("Ответить…"));
    replyEdit_->setStyleSheet(QStringLiteral(
        "background:#1A1822;border:1px solid rgba(255,255,255,0.10);border-radius:18px;"
        "min-height:36px;padding:0 14px;color:#F3F1F8;"));
    connect(replyEdit_, &QLineEdit::returnPressed, this, [this]() { reactOrReply(false); });
    auto* like = new QPushButton(QStringLiteral("❤️"), card_);
    like->setCursor(Qt::PointingHandCursor);
    like->setStyleSheet(QStringLiteral("background:transparent;border:none;font-size:18px;"));
    connect(like, &QPushButton::clicked, this, [this]() { reactOrReply(true); });
    act->addWidget(replyEdit_, 1);
    act->addWidget(like);
    cl->addLayout(act);

    auto* close = new QPushButton(QStringLiteral("✕"), card_);
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(30, 30);
    close->setStyleSheet(QStringLiteral(
        "background:rgba(255,255,255,0.08);border:none;border-radius:15px;color:#F3F1F8;"));
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    close->move(card_->width() - 42, 10);
    close->raise();

    auto* h = new QHBoxLayout();
    h->addStretch();
    h->addWidget(card_);
    h->addStretch();
    outer->addLayout(h);
}

void StoriesViewer::open(const QList<StoryUserGroup>& groups, int userIndex, int storyIndex) {
    groups_ = groups;
    userIdx_ = userIndex;
    storyIdx_ = storyIndex;
    show();
    raise();
    showCurrent();
}

void StoriesViewer::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape: hide(); break;
        case Qt::Key_Right:  nextStory(); break;
        case Qt::Key_Left:   prevStory(); break;
        default: QWidget::keyPressEvent(e);
    }
}

void StoriesViewer::mousePressEvent(QMouseEvent* e) {
    // Клик по правой половине — вперёд, по левой — назад (как в TG/вебе).
    if (e->button() == Qt::LeftButton) {
        const QPoint p = e->pos();
        if (p.x() > width() / 2) nextStory(); else prevStory();
    }
    QWidget::mousePressEvent(e);
}

void StoriesViewer::showCurrent() {
    if (userIdx_ < 0 || userIdx_ >= groups_.size()) { hide(); return; }
    const StoryUserGroup& g = groups_[userIdx_];
    if (storyIdx_ < 0 || storyIdx_ >= g.stories.size()) { nextStory(); return; }
    const StoryItem s = g.stories[storyIdx_];

    // Прогресс-бары по числу историй пользователя.
    while (progressRow_->layout()->count() > 0) {
        QLayoutItem* it = progressRow_->layout()->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    progressBars_.clear();
    for (int i = 0; i < g.stories.size(); ++i) {
        auto* seg = new QWidget(progressRow_);
        seg->setFixedHeight(3);
        seg->setStyleSheet(QStringLiteral(
            "background:%1;border-radius:1px;")
            .arg(i < storyIdx_ ? QStringLiteral("rgba(255,255,255,0.55)")
                 : i == storyIdx_ ? QStringLiteral("#8B5CF6")
                                  : QStringLiteral("rgba(255,255,255,0.14)")));
        progressRow_->layout()->addWidget(seg);
        progressBars_.append(seg);
    }

    meta_->setText(QStringLiteral("%1 • %2 (%3/%4)").arg(
        g.username, s.createdAt.left(10), QString::number(storyIdx_ + 1),
        QString::number(g.stories.size())));
    caption_->setText(s.caption);
    caption_->setVisible(!s.caption.isEmpty());

    media_->setText(QStringLiteral("Загрузка…"));
    loadStoryMedia(api_, s, [this](const QByteArray& bytes) {
        QPixmap pm;
        if (!bytes.isEmpty() && pm.loadFromData(bytes)) {
            media_->setPixmap(pm.scaled(media_->width(), media_->height(),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            media_->setText(QStringLiteral("Не удалось загрузить историю"));
        }
    });

    markViewed(s);
}

void StoriesViewer::markViewed(const StoryItem& s) {
    if (!s.isOwn) api_->storyView(s.id);
}

void StoriesViewer::nextStory() {
    if (groups_.isEmpty()) return;
    ++storyIdx_;
    if (storyIdx_ >= groups_[userIdx_].stories.size()) {
        // Следующий пользователь.
        if (userIdx_ + 1 < groups_.size()) { ++userIdx_; storyIdx_ = 0; }
        else { hide(); return; }
    }
    showCurrent();
}

void StoriesViewer::prevStory() {
    --storyIdx_;
    if (storyIdx_ < 0) {
        if (userIdx_ > 0) { --userIdx_; storyIdx_ = groups_[userIdx_].stories.size() - 1; }
        else { storyIdx_ = 0; }
    }
    showCurrent();
}

void StoriesViewer::reactOrReply(bool react) {
    if (userIdx_ >= groups_.size()) return;
    const StoryItem& s = groups_[userIdx_].stories[storyIdx_];
    // Реакция/ответ уходят в личку автору (как reply в stories.src.js).
    const QString text = react ? QStringLiteral("❤️")
                               : QStringLiteral("📹 %1").arg(replyEdit_->text().trimmed());
    if (text == QStringLiteral("📹")) return;   // пустой ответ — ничего не шлём
    api_->sendMessage(s.userId, text, QStringLiteral("st_%1").arg(QDateTime::currentMSecsSinceEpoch()),
                      0, QString());
    replyEdit_->clear();
}

// ═══════════════════════════ StoryCreatorDialog ══════════════════════════════

StoryCreatorDialog::StoryCreatorDialog(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {
    setObjectName(QStringLiteral("storyCreator"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#storyCreator { background:rgba(4,3,8,0.9); }"));
    buildUi();
    hide();
}

void StoryCreatorDialog::buildUi() {
    auto* card = new QWidget(this);
    card->setStyleSheet(QStringLiteral(
        "background:#131218;border:1px solid rgba(255,255,255,0.10);border-radius:20px;"));
    card->setFixedWidth(420);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(16, 16, 16, 16);
    cl->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Новая история"));
    title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:#F3F1F8;"));
    cl->addWidget(title);

    preview_ = new QLabel(QStringLiteral("Выберите фото…"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFixedHeight(300);
    preview_->setStyleSheet(QStringLiteral(
        "background:#1A1822;border:1px dashed rgba(255,255,255,0.16);border-radius:14px;"
        "color:#726C82;font-size:14px;"));
    preview_->setCursor(Qt::PointingHandCursor);
    preview_->installEventFilter(new SuperSearchClickFilter([this]() { pickFile(); }, preview_));
    cl->addWidget(preview_);

    captionEdit_ = new QLineEdit();
    captionEdit_->setPlaceholderText(QStringLiteral("Подпись…"));
    captionEdit_->setStyleSheet(QStringLiteral(
        "background:#1A1822;border:1px solid rgba(255,255,255,0.10);border-radius:12px;"
        "min-height:38px;padding:0 12px;color:#F3F1F8;"));
    cl->addWidget(captionEdit_);

    privacy_ = new QComboBox();
    privacy_->addItem(QStringLiteral("🌍 Все"), QStringLiteral("everyone"));
    privacy_->addItem(QStringLiteral("👥 Контакты"), QStringLiteral("contacts"));
    privacy_->addItem(QStringLiteral("🔒 Близкие"), QStringLiteral("close"));
    privacy_->setStyleSheet(QStringLiteral(
        "QComboBox{background:#1A1822;border:1px solid rgba(255,255,255,0.10);"
        "border-radius:12px;min-height:36px;color:#F3F1F8;padding:0 10px;}"
        "QComboBox QAbstractItemView{background:#1A1822;color:#F3F1F8;"
        "selection-background-color:rgba(139,92,246,0.3);}"));
    cl->addWidget(privacy_);

    auto* row = new QHBoxLayout();
    auto* cancel = new QPushButton(QStringLiteral("Отмена"));
    cancel->setStyleSheet(QStringLiteral(
        "background:transparent;border:1px solid rgba(255,255,255,0.14);border-radius:10px;"
        "min-height:36px;padding:0 16px;color:#ACA6BD;"));
    connect(cancel, &QPushButton::clicked, this, &QWidget::hide);
    postBtn_ = new QPushButton(QStringLiteral("Опубликовать"));
    postBtn_->setStyleSheet(QStringLiteral(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);"
        "color:#fff;border:none;border-radius:10px;min-height:36px;padding:0 20px;font-weight:700;"));
    connect(postBtn_, &QPushButton::clicked, this, &StoryCreatorDialog::publish);
    row->addStretch();
    row->addWidget(cancel);
    row->addWidget(postBtn_);
    cl->addLayout(row);

    auto* outer = new QVBoxLayout(this);
    outer->addStretch();
    auto* h = new QHBoxLayout();
    h->addStretch(); h->addWidget(card); h->addStretch();
    outer->addLayout(h);
    outer->addStretch();
    card_ = card;
}

void StoryCreatorDialog::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) hide();
    QWidget::keyPressEvent(e);
}

void StoryCreatorDialog::pickFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Медиа для истории"),
        QString(), QStringLiteral("Изображения (*.png *.jpg *.jpeg *.webp)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    mediaBytes_ = f.readAll();
    mediaName_ = QFileInfo(path).fileName();
    mediaType_ = QStringLiteral("image");
    QPixmap pm;
    if (pm.loadFromData(mediaBytes_))
        preview_->setPixmap(pm.scaled(preview_->width(), preview_->height(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void StoryCreatorDialog::publish() {
    if (mediaBytes_.isEmpty()) { pickFile(); return; }
    postBtn_->setEnabled(false);
    postBtn_->setText(QStringLiteral("Публикация…"));

    // Шифрование удалено: файл публикуется как есть, без ключей.
    connect(api_, &ApiClient::fileUploaded, this,
            [this](const QString& filePath, const QString&, long long, const QString&) {
        if (!isVisible()) return;
        api_->storyCreate(filePath, mediaType_, captionEdit_->text().trimmed(),
                          privacy_->currentData().toString());
    }, static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
    connect(api_, &ApiClient::storyCreated, this, [this](bool ok, const QString& msg) {
        postBtn_->setEnabled(true);
        postBtn_->setText(QStringLiteral("Опубликовать"));
        if (ok) { hide(); mediaBytes_.clear(); preview_->setText(QStringLiteral("Выберите фото…")); }
        else preview_->setText(msg);
    }, static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
    api_->uploadFile(mediaBytes_, mediaName_, QString());
}
