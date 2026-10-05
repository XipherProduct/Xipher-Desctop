#include "ui/Theme.h"
#include "ui/GroupChannelDialogs.h"
#include "ui/AvatarUtil.h"
#include "net/ApiClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <functional>

namespace {
const char* kCardQss = R"QSS(
#modalCard{background:@{s1};border:1px solid @{bDef};border-radius:18px;}
QLabel{color:@{tp};}
#dlgHeader{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 @{acD},stop:0.55 @{ac},stop:1 #B06CF0);
  border-top-left-radius:18px;border-top-right-radius:18px;}
#dlgTitle{font-size:17px;font-weight:800;color:#FFFFFF;}
#closeBtn{background:@{bStr};border:none;border-radius:16px;color:#fff;font-size:16px;font-weight:700;}
#closeBtn:hover{background:rgba(255,255,255,0.30);}
#hint{font-size:12px;color:@{tt};}
QLineEdit,QPlainTextEdit{background:@{s1};border:1px solid rgba(255,255,255,0.10);border-radius:10px;
  min-height:36px;padding:0 12px;color:@{tp};selection-background-color:@{ac};}
QPlainTextEdit{padding:8px 12px;}
QLineEdit:focus,QPlainTextEdit:focus{border:1px solid @{ac};}
#primaryBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 @{ac},stop:1 @{acD});
  color:#fff;border:none;border-radius:10px;min-height:40px;padding:0 22px;font-weight:700;}
#primaryBtn:hover{background:#9B72F8;}
#joinBtn{background:rgba(139,92,246,0.20);color:#C9B6FF;border:none;border-radius:9px;min-height:30px;padding:0 14px;font-weight:600;}
#joinBtn:hover{background:@{ac34};}
#itemCard{background:@{s2};border:1px solid @{bSub};border-radius:12px;}
#err{color:#E5687A;font-size:12px;}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:@{bStr};border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
)QSS";

// Круглое превью выбранного изображения.
QPixmap roundedPreview(const QString& path, int size) {
    QPixmap src(path);
    if (src.isNull()) return QPixmap();
    src = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip; clip.addEllipse(0, 0, size, size);
    p.setClipPath(clip);
    p.drawPixmap((size - src.width()) / 2, (size - src.height()) / 2, src);
    return out;
}

// Делает QLabel-аватар кликабельным: по клику выбор файла, превью на месте,
// callback с байтами и именем файла.
void makeAvatarClickable(QLabel* avatar, int size, ModalOverlay* parent,
                         std::function<void(QByteArray, QString)> onPick) {
    avatar->setCursor(Qt::PointingHandCursor);
    avatar->setToolTip(QStringLiteral("Выбрать фото"));
    auto* hit = new QPushButton(avatar);
    hit->setCursor(Qt::PointingHandCursor);
    hit->setFixedSize(size, size);
    hit->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;border-radius:%1px;}"
        "QPushButton:hover{background:rgba(0,0,0,0.30);}").arg(size / 2));
    QObject::connect(hit, &QPushButton::clicked, parent, [avatar, size, parent, onPick]() {
        const QString fn = QFileDialog::getOpenFileName(parent, QStringLiteral("Выберите фото"),
            QString(), QStringLiteral("Изображения (*.jpg *.jpeg *.png *.gif)"));
        if (fn.isEmpty()) return;
        QFile f(fn);
        if (!f.open(QIODevice::ReadOnly)) return;
        const QByteArray bytes = f.readAll();
        const QPixmap pm = roundedPreview(fn, size);
        if (!pm.isNull()) avatar->setPixmap(pm);
        if (onPick) onPick(bytes, QFileInfo(fn).fileName());
    });
}

QWidget* dialogHeader(const QString& title, ModalOverlay* dlg) {
    auto* head = new QWidget();
    head->setObjectName(QStringLiteral("dlgHeader"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setFixedHeight(52);
    auto* h = new QHBoxLayout(head);
    h->setContentsMargins(20, 0, 12, 0);
    auto* t = new QLabel(title); t->setObjectName(QStringLiteral("dlgTitle"));
    auto* c = new QPushButton(QStringLiteral("✕")); c->setObjectName(QStringLiteral("closeBtn"));
    c->setCursor(Qt::PointingHandCursor); c->setFixedSize(32, 32);
    QObject::connect(c, &QPushButton::clicked, dlg, &ModalOverlay::closeAnimated);
    h->addWidget(t); h->addStretch(); h->addWidget(c);
    return head;
}
} // namespace

// ── Создать группу ──────────────────────────────────────────────────────────────
CreateGroupDialog::CreateGroupDialog(ApiClient* api, QWidget* parent)
    : ModalOverlay(parent, 430), api_(api) {
    card()->setStyleSheet(ThemePreset::applyTokens(QString::fromUtf8(kCardQss)));
    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(dialogHeader(QStringLiteral("Новая группа"), this));

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(20, 18, 20, 20);
    v->setSpacing(12);

    auto* top = new QHBoxLayout(); top->setSpacing(14);
    auto* av = new QLabel(); av->setFixedSize(64, 64);
    Avatar::setRound(av, QString(), QStringLiteral("G"), 64);
    makeAvatarClickable(av, 64, this, nullptr);   // превью (аватар группы можно сменить позже)
    auto* name = new QLineEdit(); name->setPlaceholderText(QStringLiteral("Название группы"));
    top->addWidget(av); top->addWidget(name, 1);
    v->addLayout(top);

    auto* desc = new QPlainTextEdit(); desc->setFixedHeight(70);
    desc->setPlaceholderText(QStringLiteral("Описание (необязательно)"));
    v->addWidget(desc);

    auto* err = new QLabel(); err->setObjectName(QStringLiteral("err"));
    v->addWidget(err);

    auto* create = new QPushButton(QStringLiteral("Создать"));
    create->setObjectName(QStringLiteral("primaryBtn"));
    create->setCursor(Qt::PointingHandCursor);
    v->addWidget(create, 0, Qt::AlignRight);

    connect(create, &QPushButton::clicked, this, [this, name, desc, err]() {
        if (name->text().trimmed().size() < 3) { err->setText(QStringLiteral("Название — минимум 3 символа")); return; }
        err->clear();
        api_->createGroup(name->text().trimmed(), desc->toPlainText().trimmed());
    });
    connect(api_, &ApiClient::groupCreated, this, [this, err](bool ok, const QString& m) {
        if (ok) { emit created(); closeAnimated(); }
        else err->setText(m.isEmpty() ? QStringLiteral("Не удалось создать группу") : m);
    });

    lay->addWidget(body);
}

// ── Создать канал ──────────────────────────────────────────────────────────────
CreateChannelDialog::CreateChannelDialog(ApiClient* api, QWidget* parent)
    : ModalOverlay(parent, 430), api_(api) {
    card()->setStyleSheet(ThemePreset::applyTokens(QString::fromUtf8(kCardQss)));
    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(dialogHeader(QStringLiteral("Новый канал"), this));

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(20, 18, 20, 20);
    v->setSpacing(12);

    auto* top = new QHBoxLayout(); top->setSpacing(14);
    auto* av = new QLabel(); av->setFixedSize(64, 64);
    Avatar::setRound(av, QString(), QStringLiteral("C"), 64);
    makeAvatarClickable(av, 64, this, [this](QByteArray b, QString n) {
        avatarBytes_ = b; avatarName_ = n;   // загрузим после создания канала
    });
    auto* name = new QLineEdit(); name->setPlaceholderText(QStringLiteral("Название канала"));
    top->addWidget(av); top->addWidget(name, 1);
    v->addLayout(top);

    auto* desc = new QPlainTextEdit(); desc->setFixedHeight(64);
    desc->setPlaceholderText(QStringLiteral("Описание (необязательно)"));
    v->addWidget(desc);

    auto* link = new QLineEdit(); link->setPlaceholderText(QStringLiteral("@ссылка (необязательно)"));
    v->addWidget(link);
    auto* h = new QLabel(QStringLiteral("Публичная ссылка: 3–50 символов, латиница/цифры"));
    h->setObjectName(QStringLiteral("hint")); h->setWordWrap(true);
    v->addWidget(h);

    auto* err = new QLabel(); err->setObjectName(QStringLiteral("err"));
    v->addWidget(err);

    auto* create = new QPushButton(QStringLiteral("Создать"));
    create->setObjectName(QStringLiteral("primaryBtn"));
    create->setCursor(Qt::PointingHandCursor);
    v->addWidget(create, 0, Qt::AlignRight);

    connect(create, &QPushButton::clicked, this, [this, name, desc, link, err]() {
        if (name->text().trimmed().size() < 3) { err->setText(QStringLiteral("Название — минимум 3 символа")); return; }
        err->clear();
        QString l = link->text().trimmed();
        if (l.startsWith('@')) l = l.mid(1);
        enteredName_ = name->text().trimmed();
        enteredLink_ = l;
        api_->createChannel(enteredName_, desc->toPlainText().trimmed(), l);
    });
    connect(api_, &ApiClient::channelCreated, this, [this, err](bool ok, const QString& m) {
        if (!ok) { err->setText(m.isEmpty() ? QStringLiteral("Не удалось создать канал") : m); return; }
        if (avatarBytes_.isEmpty()) { emit created(); closeAnimated(); return; }
        // Есть фото: находим созданный канал и грузим аватар, затем закрываем.
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(api_, &ApiClient::channelsLoaded, this,
                        [this, conn](const QList<Chat>& chans) {
            disconnect(*conn);
            QString id;
            for (const Chat& c : chans) {   // приоритет — совпадение по @ссылке, иначе по имени
                if (!enteredLink_.isEmpty() && c.customLink == enteredLink_) { id = c.id; break; }
                if (c.displayName == enteredName_) id = c.id;   // последний с таким именем = новый
            }
            if (id.isEmpty()) { emit created(); closeAnimated(); return; }
            connect(api_, &ApiClient::channelAvatarUploaded, this, [this](bool){ emit created(); closeAnimated(); });
            api_->uploadChannelAvatar(id, avatarBytes_, avatarName_);
        });
        api_->getChannels();
    });

    lay->addWidget(body);
}

// ── Каталог ──────────────────────────────────────────────────────────────────────
CatalogDialog::CatalogDialog(ApiClient* api, QWidget* parent)
    : ModalOverlay(parent, 560), api_(api) {
    card()->setFixedHeight(540);
    card()->setStyleSheet(ThemePreset::applyTokens(QString::fromUtf8(kCardQss)));
    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(dialogHeader(QStringLiteral("Каталог каналов и групп"), this));

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(16, 14, 16, 16);
    v->setSpacing(12);

    search_ = new QLineEdit();
    search_->setPlaceholderText(QStringLiteral("Поиск каналов и групп…"));
    v->addWidget(search_);

    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listW = new QWidget();
    list_ = new QVBoxLayout(listW);
    list_->setContentsMargins(0, 0, 6, 0);
    list_->setSpacing(8);
    list_->addStretch();
    sa->setWidget(listW);
    v->addWidget(sa, 1);

    lay->addWidget(body, 1);

    // Поиск с дебаунсом.
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(350);
    connect(timer, &QTimer::timeout, this, [this]() { reload(search_->text().trimmed()); });
    connect(search_, &QLineEdit::textChanged, this, [timer]() { timer->start(); });

    connect(api_, &ApiClient::directoryLoaded, this, [this](const QList<DirectoryItem>& items, bool) {
        renderItems(items);
    });
    connect(api_, &ApiClient::publicJoined, this, [this](bool ok, const QString&) {
        if (ok) { emit joined(); reload(search_->text().trimmed()); }
    });

    reload(QString());
}

void CatalogDialog::reload(const QString& query) {
    api_->publicDirectory(QStringLiteral("all"), query, 0);
}

void CatalogDialog::renderItems(const QList<DirectoryItem>& items) {
    // Очистить (оставив финальный stretch).
    while (list_->count() > 1) {
        QLayoutItem* it = list_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    if (items.isEmpty()) {
        auto* e = new QLabel(QStringLiteral("Ничего не найдено"));
        e->setObjectName(QStringLiteral("hint"));
        list_->insertWidget(0, e);
        return;
    }
    int row = 0;
    for (const DirectoryItem& d : items) {
        auto* card = new QFrame();
        card->setObjectName(QStringLiteral("itemCard"));
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(12, 10, 12, 10);
        h->setSpacing(12);

        auto* av = new QLabel(); av->setFixedSize(46, 46);
        Avatar::setRound(av, d.avatarUrl, d.name, 46);
        h->addWidget(av);

        auto* mid = new QVBoxLayout(); mid->setSpacing(2);
        const QString badge = d.type == QStringLiteral("channel") ? QStringLiteral("Канал") : QStringLiteral("Группа");
        auto* title = new QLabel(d.name + (d.verified ? QStringLiteral("  ✓") : QString()));
        title->setStyleSheet(ThemePreset::applyTokens(QStringLiteral("color:@{tp};font-size:14px;font-weight:700;")));
        QString sub = badge + QStringLiteral(" · %1 участн.").arg(d.membersCount);
        if (!d.username.isEmpty()) sub += QStringLiteral("  @") + d.username;
        auto* subL = new QLabel(sub); subL->setStyleSheet(ThemePreset::applyTokens(QStringLiteral("color:@{tt};font-size:12px;")));
        mid->addWidget(title); mid->addWidget(subL);
        if (!d.description.isEmpty()) {
            auto* ds = new QLabel(d.description);
            ds->setStyleSheet(QStringLiteral("color:@{ts};font-size:12px;"));
            ds->setWordWrap(true);
            mid->addWidget(ds);
        }
        h->addLayout(mid, 1);

        if (d.isMember) {
            auto* m = new QLabel(QStringLiteral("Вы участник"));
            m->setStyleSheet(QStringLiteral("color:@{ok};font-size:12px;"));
            h->addWidget(m, 0, Qt::AlignVCenter);
        } else {
            auto* join = new QPushButton(d.type == QStringLiteral("channel")
                ? QStringLiteral("Подписаться") : QStringLiteral("Вступить"));
            join->setObjectName(QStringLiteral("joinBtn"));
            join->setCursor(Qt::PointingHandCursor);
            const QString id = d.id, type = d.type;
            connect(join, &QPushButton::clicked, this, [this, id, type]() { api_->joinPublic(id, type); });
            h->addWidget(join, 0, Qt::AlignVCenter);
        }
        list_->insertWidget(row++, card);
    }
}
