#include "ui/SettingsDialog.h"
#include "util/Autostart.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include <QUrlQuery>
#include <QMessageBox>
#include <QTimer>
#include <QVariantAnimation>
#include "ui/ToggleSwitch.h"
#include "ui/AvatarUtil.h"
#include "net/ApiClient.h"
#include "net/Session.h"
#include "net/Prefs.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QButtonGroup>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QTimeEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QPainter>
#include <QPixmap>
#include <QEvent>
#include <QFileDialog>
#include <QDesktopServices>
#include <QFile>
#include <QUrl>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QTime>
#include <QFileInfo>

namespace {

// Карточка-группа с заголовком; body — куда класть строки.
QFrame* sectionCard(const QString& title, QVBoxLayout*& body) {
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(18, 16, 18, 18);
    v->setSpacing(4);
    if (!title.isEmpty()) {
        auto* t = new QLabel(title);
        t->setObjectName(QStringLiteral("cardTitle"));
        v->addWidget(t);
        auto* sp = new QSpacerItem(1, 6, QSizePolicy::Minimum, QSizePolicy::Fixed);
        v->addSpacerItem(sp);
    }
    body = v;
    return card;
}

// Строка «подпись слева — контрол справа», 1:1 с .settings-row веба:
// просторная (мин. 46px), между строками — hairline-разделитель.
QWidget* rowLabeled(const QString& label, QWidget* control, QVBoxLayout* body = nullptr) {
    if (body && body->count() > 0) {
        auto* sep = new QFrame();
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet(QStringLiteral("background:rgba(255,255,255,0.055);border:none;max-height:1px;"));
        body->addWidget(sep);
    }
    auto* w = new QWidget();
    w->setObjectName(QStringLiteral("stRow"));
    w->setMinimumHeight(46);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(2, 10, 2, 10);
    h->setSpacing(12);
    auto* l = new QLabel(label);
    l->setObjectName(QStringLiteral("rowLabel"));
    l->setWordWrap(true);
    h->addWidget(l, 1);
    h->addWidget(control, 0, Qt::AlignVCenter | Qt::AlignRight);
    if (body) body->addWidget(w);
    return w;
}

// Селект настроек: фикс. ширина, выравнивание вправо (как .settings-select).
QComboBox* settingsSelect() {
    auto* c = new QComboBox();
    c->setMinimumWidth(190);
    c->setMaximumHeight(38);
    return c;
}

// Заметка под карточкой (как .settings-note).
QLabel* settingsNote(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("hint"));
    l->setWordWrap(true);
    return l;
}

QComboBox* visibilityCombo(const QString& current) {
    auto* c = new QComboBox();
    c->addItem(QStringLiteral("Все"), QStringLiteral("everyone"));
    c->addItem(QStringLiteral("Контакты"), QStringLiteral("contacts"));
    c->addItem(QStringLiteral("Никто"), QStringLiteral("nobody"));
    const int idx = c->findData(current.isEmpty() ? QStringLiteral("everyone") : current);
    c->setCurrentIndex(idx < 0 ? 0 : idx);
    return c;
}

QScrollArea* scrollPage(QWidget* content) {
    auto* sa = new QScrollArea();
    sa->setObjectName(QStringLiteral("settingsScroll"));
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setWidget(content);
    return sa;
}

// Ширина карточки под размер окна: на больших окнах — шире (правая часть просторнее),
// на маленьких — ужимается, чтобы не вылезать за края.
[[maybe_unused]] int settingsCardWidth(QWidget* parent) {
    const int pw = parent ? parent->width() : 1000;
    return qBound(660, pw - 90, 920);
}

// Цветной чип-иконка (как в iOS/Telegram настройках).
QLabel* iconChip(int kind, const QColor& color, int box = 30) {
    auto* l = new QLabel();
    l->setFixedSize(box, box);
    QPixmap pm(box * 2, box * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color); p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(0, 0, box, box), 8, 8);
    const int ic = 18;
    p.drawPixmap((box - ic) / 2, (box - ic) / 2, Icons::pixmap(Icons::Kind(kind), ic, QColor(0xFF,0xFF,0xFF)));
    p.end();
    l->setPixmap(pm);
    return l;
}

} // namespace

SettingsDialog::SettingsDialog(ApiClient* api, QWidget* parent)
    : ModalOverlay(parent, 1060), api_(api) {
    card()->setFixedHeight(840);
    // Токены 1:1 с .settings-panel веба (--st-* из css/settings.css).
    // Токены 1:1 с .settings-panel веба (--st-* из css/settings.css).
    card()->setStyleSheet(QStringLiteral(R"QSS(
#modalCard{background:#0b0a0e;border:1px solid rgba(255,255,255,0.07);border-radius:20px;}
QLabel{color:#f3f1f8;}
/* ── Topbar ── */
#stTopbar{background:#0b0a0e;border-bottom:1px solid rgba(255,255,255,0.07);}
#stTitle{font-size:19px;font-weight:700;color:#f3f1f8;letter-spacing:0.2px;}
#stIconBtn{border:none;background:transparent;border-radius:19px;color:#aca6bd;font-size:17px;padding:0;}
#stIconBtn:hover{background:rgba(255,255,255,0.05);color:#f3f1f8;}
/* ── Nav ── */
#stNav{background:#08070b;border-right:1px solid rgba(255,255,255,0.07);}
#stNavScroll,#stNavScroll>QWidget{background:transparent;border:none;}
#stNavGroup{color:#726c82;font-size:11px;font-weight:700;letter-spacing:1px;padding:12px 10px 4px;}
#stNavItem{text-align:left;padding:8px 10px;border:none;border-radius:10px;background:transparent;color:#aca6bd;font-size:13px;font-weight:500;}
#stNavItem:hover{background:rgba(255,255,255,0.05);color:#f3f1f8;}
#stNavItem:checked{background:rgba(139,92,246,0.14);color:#8b5cf6;font-weight:600;}
/* ── Аккаунт: hero + tiles ── */
#acctHero{background:#16141d;border:1px solid rgba(255,255,255,0.07);border-radius:20px;}
#acctName{font-size:17px;font-weight:700;color:#f3f1f8;}
#acctUname{color:#8b5cf6;font-size:13px;font-weight:600;}
#acctCam{background:rgba(11,10,14,0.75);border:1px solid rgba(255,255,255,0.16);border-radius:14px;color:#f3f1f8;font-size:13px;padding:0;}
#acctCam:hover{background:#8b5cf6;border-color:#8b5cf6;}
#acctTile{text-align:left;background:#16141d;border:1px solid rgba(255,255,255,0.07);border-radius:14px;padding:2px 4px;}
#acctTile:hover{background:rgba(255,255,255,0.05);border-color:rgba(255,255,255,0.12);}
#acctTileLbl{color:#aca6bd;font-size:12px;}
#acctTileVal{color:#f3f1f8;font-size:13px;font-weight:600;}
#acctGrouptitle{color:#726c82;font-size:11px;font-weight:700;letter-spacing:1px;}
/* ── Карточки/поля/кнопки ── */
#card{background:#16141d;border:1px solid rgba(255,255,255,0.07);border-radius:16px;}
#cardTitle{font-size:11px;font-weight:700;color:#726c82;letter-spacing:0.8px;}
#heroName{font-size:17px;font-weight:700;color:#f3f1f8;}
#rowLabel{font-size:14px;color:#f3f1f8;}
#hint{font-size:12px;color:#726c82;}
QLineEdit,QPlainTextEdit,QComboBox,QSpinBox,QTimeEdit{
  background:#1a1822;border:1px solid rgba(255,255,255,0.07);border-radius:10px;
  min-height:36px;padding:0 12px;color:#f3f1f8;selection-background-color:#8b5cf6;}
QPlainTextEdit{padding:8px 12px;}
QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus,QSpinBox:focus,QTimeEdit:focus{
  border:1px solid #8b5cf6;}
QLineEdit:disabled{color:#726c82;}
QComboBox::drop-down{border:none;width:22px;}
QComboBox QAbstractItemView{background:#1a1822;border:1px solid rgba(255,255,255,0.12);
  color:#f3f1f8;selection-background-color:rgba(139,92,246,0.30);outline:none;}
QSpinBox::up-button,QSpinBox::down-button{width:0;border:none;}
#primaryBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8b5cf6,stop:1 #7a4ae6);
  color:#fff;border:none;border-radius:10px;min-height:38px;padding:0 22px;font-weight:700;}
#primaryBtn:hover{background:#9B72F8;}
#ghostBtn{background:#1a1822;color:#f3f1f8;border:1px solid rgba(255,255,255,0.07);
  border-radius:10px;min-height:34px;padding:0 16px;}
#ghostBtn:hover{background:rgba(255,255,255,0.05);}
#dangerBtn{background:transparent;color:#E26A63;border:1px solid rgba(226,106,99,0.4);
  border-radius:10px;min-height:34px;padding:0 16px;}
#dangerBtn:hover{background:rgba(226,106,99,0.12);}
#statusOk{color:#46B98A;font-size:12px;}
#pill{background:rgba(139,92,246,0.14);color:#BBA4FF;border-radius:10px;padding:3px 10px;font-size:12px;font-weight:700;}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:rgba(255,255,255,0.12);border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
/* ── Xipher Pulse ── */
#pulseChip{background:rgba(139,92,246,0.22);border:1px solid rgba(139,92,246,0.34);
  border-radius:10px;padding:4px 11px;font-size:12px;font-weight:600;color:#F3F1F8;}
#pulseChipGhost{background:#100F15;border:1px solid rgba(255,255,255,0.10);
  border-radius:10px;padding:4px 11px;font-size:12px;color:#ACA6BD;}
#pulseChipWait{background:rgba(217,160,91,0.14);border:1px solid #D9A05B;
  border-radius:10px;padding:4px 11px;font-size:12px;font-weight:600;color:#F3F1F8;}
#pulseCta{border:none;border-radius:14px;color:#fff;font-size:19px;font-weight:700;
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #7A4AE6,stop:1 #6D28D9);}
#pulseCta:hover{filter:brightness(1.06);}
#pulseCta:disabled{background:#2B2737;color:#726C82;}
#pulseCtaGhost{background:transparent;border:1px solid rgba(255,255,255,0.16);
  border-radius:14px;color:#ACA6BD;font-size:15px;padding:0 22px;}
#pulseCtaGhost:hover{border-color:#E26A63;color:#E26A63;}
#pulseMeter{background:#100F15;border:1px solid rgba(255,255,255,0.055);border-radius:14px;}
#pulseMeterAccent{background:rgba(139,92,246,0.14);border:1px solid rgba(139,92,246,0.34);border-radius:14px;}
#pulsePerk{background:#131218;border:1px solid rgba(255,255,255,0.055);border-radius:16px;}
#pulsePerk:hover{border-color:rgba(139,92,246,0.34);}
#themeTile{background:#131218;border:1px solid rgba(255,255,255,0.10);border-radius:14px;padding:4px;}
#themeTile:hover{border-color:rgba(139,92,246,0.4);}
#themeTile:disabled{background:#100F15;}
)QSS"));

    buildChrome();

    connect(api_, &ApiClient::profileLoaded, this, &SettingsDialog::onProfileLoaded);
    api_->getMyProfile();
}

namespace {

// Баннер acct-hero: радиальное свечение акцента у верхнего левого угла
// поверх тёмного линейного градиента — 1:1 с .acct-banner веба.
class AcctBanner : public QWidget {
public:
    using QWidget::QWidget;
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QRect r = rect();
        QLinearGradient lg(r.topLeft(), r.bottomRight());
        lg.setColorAt(0.0,  QColor(0x2a, 0x25, 0x36));
        lg.setColorAt(0.55, QColor(0x1b, 0x18, 0x26));
        lg.setColorAt(1.0,  QColor(0x16, 0x14, 0x1d));
        p.fillRect(r, lg);
        QRadialGradient rg(QPointF(r.width() * 0.12, -r.height() * 0.10), r.height() * 1.5);
        rg.setColorAt(0.0, QColor(139, 92, 246, 87));
        rg.setColorAt(1.0, QColor(139, 92, 246, 0));
        p.fillRect(r, rg);
    }
};

// Hero аккаунта: баннер + блок аватара масштабируются под ширину карточки.
class AcctHero : public QFrame {
public:
    AcctBanner* banner = nullptr;
    QWidget*    head   = nullptr;

    void reposition() {
        if (banner) banner->setGeometry(1, 1, width() - 2, 92);
        if (head)   head->setGeometry(21, 46, width() - 42, height() - 46);
    }

protected:
    void resizeEvent(QResizeEvent*) override { reposition(); }
};

} // namespace

void SettingsDialog::buildChrome() {
    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ── Topbar 64px: «Настройки» + ⋮ + ✕ (1:1 с .settings-topbar веба) ──────
    auto* top = new QWidget();
    top->setObjectName(QStringLiteral("stTopbar"));
    top->setFixedHeight(64);
    auto* th = new QHBoxLayout(top);
    th->setContentsMargins(22, 0, 17, 0);
    th->setSpacing(6);
    auto* title = new QLabel(QStringLiteral("Настройки"));
    title->setObjectName(QStringLiteral("stTitle"));
    th->addWidget(title);
    th->addStretch();
    auto* moreBtn = new QPushButton(QStringLiteral("⋮"));
    moreBtn->setObjectName(QStringLiteral("stIconBtn"));
    moreBtn->setCursor(Qt::PointingHandCursor);
    moreBtn->setFixedSize(38, 38);
    auto* closeBtn = new QPushButton(QStringLiteral("✕"));
    closeBtn->setObjectName(QStringLiteral("stIconBtn"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(38, 38);
    connect(closeBtn, &QPushButton::clicked, this, &ModalOverlay::closeAnimated);
    // Кнопка «Разделы» — видна только на узкой панели, когда nav спрятан.
    sectionsBtn_ = new QPushButton(QStringLiteral("☰ Разделы"));
    sectionsBtn_->setObjectName(QStringLiteral("stIconBtn"));
    sectionsBtn_->setCursor(Qt::PointingHandCursor);
    sectionsBtn_->setVisible(false);
    connect(sectionsBtn_, &QPushButton::clicked, this, [this]() {
        QMenu m(sectionsBtn_);
        for (QAbstractButton* b : navBtnGroup_->buttons()) {
            QAction* a = m.addAction(b->text());
            a->setData(navBtnGroup_->id(b));
        }
        connect(&m, &QMenu::triggered, this, [this](QAction* a) {
            const int idx = a->data().toInt();
            stack_->setCurrentIndex(idx);
        });
        m.exec(sectionsBtn_->mapToGlobal(QPoint(0, sectionsBtn_->height() + 4)));
    });
    th->addWidget(sectionsBtn_);
    th->addWidget(moreBtn);
    th->addWidget(closeBtn);
    lay->addWidget(top);

    // Меню ⋮: Редактировать профиль / Выйти из аккаунта — как #settingsMoreMenu.
    connect(moreBtn, &QPushButton::clicked, this, [this, moreBtn]() {
        QMenu m(moreBtn);
        QAction* editProfile = m.addAction(QStringLiteral("Редактировать профиль"));
        QAction* logout      = m.addAction(QStringLiteral("Выйти из аккаунта"));
        connect(logout, &QAction::triggered, this, [this]() {
            closeAnimated();
            emit logoutRequested();
        });
        QAction* chosen = m.exec(moreBtn->mapToGlobal(QPoint(0, moreBtn->height() + 4)));
        if (chosen == editProfile) stack_->setCurrentIndex(accountIdx_);
    });

    // ── Body: nav 256px + контент (1:1 с .settings-body веба) ────────────────
    auto* body = new QWidget();
    auto* bh = new QHBoxLayout(body);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->setSpacing(0);

    navWidget_ = new QWidget();
    navWidget_->setObjectName(QStringLiteral("stNav"));
    auto* nav = navWidget_;
    nav->setFixedWidth(256);
    auto* navOuter = new QVBoxLayout(nav);
    navOuter->setContentsMargins(0, 0, 0, 0);
    auto* navScroll = new QScrollArea();
    navScroll->setObjectName(QStringLiteral("stNavScroll"));
    navScroll->setWidgetResizable(true);
    navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navScroll->setFrameShape(QFrame::NoFrame);
    auto* navW = new QWidget();
    navLayout_ = new QVBoxLayout(navW);
    navLayout_->setContentsMargins(10, 12, 10, 16);
    navLayout_->setSpacing(1);
    navLayout_->addStretch();
    navScroll->setWidget(navW);
    navOuter->addWidget(navScroll);

    stack_ = new QStackedWidget();
    bh->addWidget(nav);
    bh->addWidget(stack_, 1);
    lay->addWidget(body, 1);

    navBtnGroup_ = new QButtonGroup(this);
    navBtnGroup_->setExclusive(true);
    connect(navBtnGroup_, &QButtonGroup::idClicked, stack_, &QStackedWidget::setCurrentIndex);

    // Группы и разделы — как в вебе (chat.html settings-nav).
    navGroup(QStringLiteral("АККАУНТ"));
    addSection(QStringLiteral("Мой аккаунт"),                Icons::User,   buildAccountPage());
    navGroup(QStringLiteral("ОБЩЕЕ"));
    addSection(QStringLiteral("Уведомления и звуки"),        Icons::Bell,   buildNotificationsPage());
    addSection(QStringLiteral("Звонки"),                     Icons::Phone,  buildCallsPage());
    addSection(QStringLiteral("Язык"),                       Icons::Globe,  buildLanguagePage());
    addSection(QStringLiteral("Экспорт данных"),             Icons::File,   buildExportPage());
    appearanceIdx_ = stack_->count();
    addSection(QStringLiteral("Оформление"),                 Icons::Image,  buildAppearancePage());
    navGroup(QStringLiteral("БЕЗОПАСНОСТЬ"));
    addSection(QStringLiteral("Приватность и безопасность"), Icons::Shield, buildPrivacyPage());
    addSection(QStringLiteral("Активные сеансы"),            Icons::Device, buildSessionsPage());
    addSection(QStringLiteral("Заблокированные пользователи"), Icons::Block, buildBlockedPage());
    addSection(QStringLiteral("Login email"),                Icons::Lock,   buildEmailPage());
    navGroup(QStringLiteral("XIPHER"));
    addSection(QStringLiteral("Xipher Premium"),             Icons::Star,   buildPremiumPage());
    addSection(QStringLiteral("Xipher FAQ"),                 Icons::Gear,   buildAboutPage());

    // Загрузка аватара обновляет hero (как settingsChangeAvatarBtn).
    connect(api_, &ApiClient::avatarUploaded, this, [this](bool ok, const QString& url) {
        if (!ok) return;
        if (!url.isEmpty()) avatarUrl_ = url;
        if (avatar_)
            Avatar::setRound(avatar_, avatarUrl_,
                             heroName_->text().isEmpty() ? Session::instance().username
                                                         : heroName_->text(), 84);
    });

    if (QAbstractButton* b = navBtnGroup_->button(accountIdx_)) b->setChecked(true);
    stack_->setCurrentIndex(accountIdx_);
}

QWidget* SettingsDialog::wrapPage(QWidget* content) {
    auto* inner = new QWidget();
    auto* v = new QVBoxLayout(inner);
    v->setContentsMargins(28, 22, 28, 30);
    v->setSpacing(0);
    // Контент секции — колонка 680px по центру (.settings-section веба).
    auto* h = new QHBoxLayout();
    h->setSpacing(0);
    h->addStretch();
    content->setMaximumWidth(680);
    h->addWidget(content, 0, Qt::AlignTop);
    h->addStretch();
    v->addLayout(h);
    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(inner);
    return sa;
}


void SettingsDialog::resizeEvent(QResizeEvent* e) {
    ModalOverlay::resizeEvent(e);
    relayoutForWidth(card() ? card()->width() : width());
}

void SettingsDialog::relayoutForWidth(int w) {
    if (!navWidget_) return;
    // Навигация остаётся, пока на контент остаётся ≥ 544px (768 − 224 мержей).
    const bool navOn = w >= 800;
    if (navWidget_->isVisible() != navOn) navWidget_->setVisible(navOn);
    if (sectionsBtn_) sectionsBtn_->setVisible(!navOn);
    if (w == lastLayoutW_) return;
    lastLayoutW_ = w;

    // Тарифы/перки/темы перестраиваются по ширине КОНТЕНТА, не карточки.
    const int contentW = w - (navOn ? 256 : 0) - 56;
    relayoutGrids(contentW);
}

void SettingsDialog::relayoutGrids(int w) {
    const bool narrow = w < 640;
    auto reflow = [](QGridLayout* g, const QList<QWidget*>& items, int cols) {
        if (!g) return;
        for (QWidget* it : items) g->removeWidget(it);
        for (int i = 0; i < items.size(); ++i)
            g->addWidget(items[i], i / cols, i % cols);
    };
    QList<QWidget*> items;
    if (plansGrid_) {
        items.clear();
        for (int r = 0; r < plansGrid_->rowCount(); ++r)
            for (int c = 0; c < plansGrid_->columnCount(); ++c)
                if (auto* it = plansGrid_->itemAtPosition(r, c))
                    if (it->widget()) items += it->widget();
        reflow(plansGrid_, items, narrow ? 1 : 3);
    }
    if (perksGrid_) {
        items.clear();
        for (int r = 0; r < perksGrid_->rowCount(); ++r)
            for (int c = 0; c < perksGrid_->columnCount(); ++c)
                if (auto* it = perksGrid_->itemAtPosition(r, c))
                    if (it->widget()) items += it->widget();
        reflow(perksGrid_, items, narrow ? 1 : 2);
    }
    if (themeGrid_) {
        items.clear();
        for (int r = 0; r < themeGrid_->rowCount(); ++r)
            for (int c = 0; c < themeGrid_->columnCount(); ++c)
                if (auto* it = themeGrid_->itemAtPosition(r, c))
                    if (it->widget()) items += it->widget();
        reflow(themeGrid_, items, narrow ? 2 : 3);
    }
}

void SettingsDialog::navGroup(const QString& title) {
    auto* l = new QLabel(title);
    l->setObjectName(QStringLiteral("stNavGroup"));
    navLayout_->insertWidget(navLayout_->count() - 1, l);
}

void SettingsDialog::addSection(const QString& title, int iconKind, QWidget* page) {
    const int idx = stack_->addWidget(wrapPage(page));
    auto* b = new QPushButton(Icons::icon(static_cast<Icons::Kind>(iconKind), 19,
                                          QColor(0xAC, 0xA6, 0xBD)), title);
    b->setObjectName(QStringLiteral("stNavItem"));
    b->setIconSize(QSize(19, 19));
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    navBtnGroup_->addButton(b, idx);
    navLayout_->insertWidget(navLayout_->count() - 1, b);
    if (idx == 0) accountIdx_ = idx;   // «Мой аккаунт» — первый и стартовый
}

// ── Мой аккаунт ───────────────────────────────────────────────────────────────
void SettingsDialog::pickAvatar() {
    const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите фото"),
        QString(), QStringLiteral("Изображения (*.jpg *.jpeg *.png *.gif)"));
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) return;
    api_->uploadAvatar(f.readAll(), QFileInfo(fn).fileName());
}

QWidget* SettingsDialog::buildAccountPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);

    // ── acct-hero: баннер 92px + аватар внахлёст + имя/@username (как в вебе) ──
    auto* hero = new AcctHero();
    hero->setObjectName(QStringLiteral("acctHero"));
    hero->setMaximumWidth(676);
    hero->setMinimumHeight(158);
    auto* banner = new AcctBanner(hero);
    banner->setGeometry(1, 1, 674, 92);
    auto* head = new QWidget(hero);
    head->setGeometry(21, 46, 634, 96);
    hero->banner = banner;
    hero->head = head;
    head->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(0, 0, 0, 0);
    hh->setSpacing(16);

    auto* avWrap = new QWidget(head);
    avWrap->setFixedSize(84, 84);
    avatar_ = new QLabel(avWrap);
    avatar_->setGeometry(0, 0, 84, 84);
    avatar_->setCursor(Qt::PointingHandCursor);
    avatar_->setToolTip(QStringLiteral("Сменить фото"));
    Avatar::setRound(avatar_, QString(), Session::instance().username, 84);
    auto* cam = new QPushButton(avWrap);
    cam->setObjectName(QStringLiteral("acctCam"));
    cam->setCursor(Qt::PointingHandCursor);
    cam->setGeometry(84 - 30, 84 - 30, 28, 28);
    cam->setToolTip(QStringLiteral("Сменить фото"));
    connect(cam, &QPushButton::clicked, this, &SettingsDialog::pickAvatar);
    hh->addWidget(avWrap, 0, Qt::AlignBottom);

    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(2);
    heroName_ = new QLabel(Session::instance().username);
    heroName_->setObjectName(QStringLiteral("acctName"));
    heroUser_ = new QLabel(QStringLiteral("@") + Session::instance().username);
    heroUser_->setObjectName(QStringLiteral("acctUname"));
    idCol->addStretch();
    idCol->addWidget(heroName_);
    idCol->addWidget(heroUser_);
    hh->addLayout(idCol, 1);

    v->addWidget(hero);

    // ── acct-tiles: Ксифы + NFT (как в вебе; «Stars» в нашем лексиконе — «Ксифы») ──
    auto* tilesRow = new QHBoxLayout();
    tilesRow->setSpacing(11);
    auto makeTile = [&](const QString& emoji, const QString& lbl, const QString& val) {
        auto* t = new QPushButton();
        t->setObjectName(QStringLiteral("acctTile"));
        t->setCursor(Qt::PointingHandCursor);
        t->setFixedHeight(64);
        auto* tl = new QHBoxLayout(t);
        tl->setContentsMargins(12, 8, 12, 8);
        tl->setSpacing(10);
        auto* ic = new QLabel(emoji, t);
        ic->setFixedSize(34, 34);
        ic->setAlignment(Qt::AlignCenter);
        ic->setStyleSheet(QStringLiteral(
            "background:rgba(139,92,246,0.14);border-radius:10px;font-size:16px;"));
        tl->addWidget(ic);
        auto* col = new QVBoxLayout();
        col->setSpacing(1);
        auto* l1 = new QLabel(lbl, t);
        l1->setObjectName(QStringLiteral("acctTileLbl"));
        auto* l2 = new QLabel(val, t);
        l2->setObjectName(QStringLiteral("acctTileVal"));
        col->addWidget(l1);
        col->addWidget(l2);
        tl->addLayout(col, 1);
        return t;
    };
    tilesRow->addWidget(makeTile(QStringLiteral("⭐"), QStringLiteral("Ксифы"),
                                 QStringLiteral("0 ⭐")));
    auto* nft = makeTile(QStringLiteral("🖼️"), QStringLiteral("NFT Имена"),
                         QStringLiteral("Скоро ›"));
    nft->setEnabled(false);
    tilesRow->addWidget(nft);
    v->addLayout(tilesRow);

    // ── Основные данные ────────────────────────────────────────────────────────
    auto* gt = new QLabel(QStringLiteral("ОСНОВНЫЕ ДАННЫЕ"));
    gt->setObjectName(QStringLiteral("acctGrouptitle"));
    v->addWidget(gt);

    QVBoxLayout* body = nullptr;
    auto* card = sectionCard(QString(), body);
    firstName_ = new QLineEdit(); firstName_->setMaxLength(255);
    lastName_  = new QLineEdit(); lastName_->setMaxLength(255);
    bio_       = new QPlainTextEdit(); bio_->setFixedHeight(70);
    usernameRO_= new QLineEdit(); usernameRO_->setDisabled(true);
    // Поля с лейблом НАД полем — как .acct-editor веба.
    auto addField = [body](const QString& label, QWidget* w) {
        auto* l = new QLabel(label);
        l->setObjectName(QStringLiteral("rowLabel"));
        body->addWidget(l);
        body->addWidget(w);
    };
    addField(QStringLiteral("Имя"), firstName_);
    addField(QStringLiteral("Фамилия"), lastName_);
    addField(QStringLiteral("О себе"), bio_);
    addField(QStringLiteral("Имя пользователя"), usernameRO_);
    v->addWidget(card);

    // День рождения.
    QVBoxLayout* bd = nullptr;
    auto* bdCard = sectionCard(QStringLiteral("День рождения"), bd);
    auto* bdRow = new QWidget();
    auto* bdh = new QHBoxLayout(bdRow); bdh->setContentsMargins(0,0,0,0); bdh->setSpacing(8);
    bDay_   = new QSpinBox(); bDay_->setRange(0, 31);   bDay_->setSpecialValueText(QStringLiteral("—"));
    bMonth_ = new QSpinBox(); bMonth_->setRange(0, 12); bMonth_->setSpecialValueText(QStringLiteral("—"));
    bYear_  = new QSpinBox(); bYear_->setRange(0, 2100);bYear_->setSpecialValueText(QStringLiteral("—"));
    bdh->addWidget(new QLabel(QStringLiteral("День"))); bdh->addWidget(bDay_);
    bdh->addWidget(new QLabel(QStringLiteral("Месяц")));bdh->addWidget(bMonth_);
    bdh->addWidget(new QLabel(QStringLiteral("Год")));  bdh->addWidget(bYear_);
    bdh->addStretch();
    bd->addWidget(bdRow);
    auto* bdHint = new QLabel(QStringLiteral("0 — не указывать")); bdHint->setObjectName(QStringLiteral("hint"));
    bd->addWidget(bdHint);
    v->addWidget(bdCard);

    // Сохранить.
    auto* save = new QPushButton(QStringLiteral("Сохранить"));
    save->setObjectName(QStringLiteral("primaryBtn"));
    save->setCursor(Qt::PointingHandCursor);
    auto* status = new QLabel(); status->setObjectName(QStringLiteral("statusOk"));
    connect(save, &QPushButton::clicked, this, [this, status]() {
        api_->updateMyProfile(firstName_->text(), lastName_->text(), bio_->toPlainText(),
                              bDay_->value(), bMonth_->value(), bYear_->value());
        status->setText(QStringLiteral("Сохраняем…"));
    });
    connect(api_, &ApiClient::profileUpdated, this, [status](bool ok, const QString& m) {
        status->setStyleSheet(ok ? QString() : QStringLiteral("color:#E26A63;font-size:12px;"));
        status->setText(ok ? QStringLiteral("Сохранено ✓")
                           : (QStringLiteral("Ошибка: ") + m));
    });
    auto* saveRow = new QWidget();
    auto* srh = new QHBoxLayout(saveRow); srh->setContentsMargins(0,0,0,0);
    srh->addWidget(status); srh->addStretch(); srh->addWidget(save);
    v->addWidget(saveRow);

    v->addStretch();
    return page;
}

void SettingsDialog::onProfileLoaded(const QJsonObject& obj) {
    const QJsonObject u = obj.value(QStringLiteral("user")).toObject();
    if (u.value(QStringLiteral("id")).toString() != Session::instance().userId) return;

    const QString display = u.value(QStringLiteral("display_name")).toString();
    const QString uname   = u.value(QStringLiteral("username")).toString();
    avatarUrl_ = u.value(QStringLiteral("avatar_url")).toString();
    if (avatar_) Avatar::setRound(avatar_, avatarUrl_, display.isEmpty() ? uname : display, 92);
    if (heroName_) heroName_->setText(display.isEmpty() ? uname : display);
    if (heroUser_) heroUser_->setText(QStringLiteral("@") + uname);
    if (firstName_) firstName_->setText(u.value(QStringLiteral("first_name")).toString());
    if (lastName_)  lastName_->setText(u.value(QStringLiteral("last_name")).toString());
    if (bio_)       bio_->setPlainText(u.value(QStringLiteral("bio")).toString());
    if (usernameRO_) usernameRO_->setText(QStringLiteral("@") + uname);
    if (bDay_)   bDay_->setValue(u.value(QStringLiteral("birth_day")).toInt());
    if (bMonth_) bMonth_->setValue(u.value(QStringLiteral("birth_month")).toInt());
    if (bYear_)  bYear_->setValue(u.value(QStringLiteral("birth_year")).toInt());
}

// ── Уведомления ────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildNotificationsPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);

    auto addToggle = [](QVBoxLayout* body, const QString& label, const QString& key, bool def) {
        auto* sw = new ToggleSwitch();
        sw->setChecked(Prefs::getBool(key, def));
        QObject::connect(sw, &QAbstractButton::toggled, [key](bool on){ Prefs::setBool(key, on); });
        rowLabeled(label, sw, body);
    };

    QVBoxLayout* b1 = nullptr;
    auto* c1 = sectionCard(QStringLiteral("Уведомления"), b1);
    addToggle(b1, QStringLiteral("Уведомления рабочего стола"), QStringLiteral("xipher_notif_desktop"), true);
    addToggle(b1, QStringLiteral("Звук сообщений"), QStringLiteral("xipher_notif_sound"), true);
    addToggle(b1, QStringLiteral("Показывать превью сообщений"), QStringLiteral("xipher_notif_preview"), true);
    addToggle(b1, QStringLiteral("Звук звонков"), QStringLiteral("xipher_notif_call_sound"), true);
    v->addWidget(c1);

    // Рабочий стол (WIN-06/07): закрытие в трей + автозапуск с системой.
    {
        QVBoxLayout* b9 = nullptr;
        auto* c9 = sectionCard(QStringLiteral("Рабочий стол"), b9);
        auto* swTray = new ToggleSwitch();
        swTray->setChecked(Prefs::getBool(QStringLiteral("xipher_close_to_tray"), false));
        QObject::connect(swTray, &QAbstractButton::toggled, [](bool on) {
            Prefs::setBool(QStringLiteral("xipher_close_to_tray"), on);
        });
        rowLabeled(QStringLiteral("Закрывать окно в трей"), swTray, b9);
        auto* swAuto = new ToggleSwitch();
        swAuto->setChecked(Autostart::isOn());
        QObject::connect(swAuto, &QAbstractButton::toggled, [](bool on) {
            Autostart::set(on);
        });
        rowLabeled(QStringLiteral("Запускать вместе с системой"), swAuto, b9);
        v->addWidget(c9);
    }

    // Тихие часы: одна строка «Время тишины» + два time-поля (как в вебе).
    QVBoxLayout* b2 = nullptr;
    auto* c2 = sectionCard(QStringLiteral("Тихие часы"), b2);
    auto* from = new QTimeEdit(QTime::fromString(Prefs::getStr(QStringLiteral("xipher_quiet_hours_from"),
        QStringLiteral("23:00")), QStringLiteral("HH:mm")));
    auto* to = new QTimeEdit(QTime::fromString(Prefs::getStr(QStringLiteral("xipher_quiet_hours_to"),
        QStringLiteral("08:00")), QStringLiteral("HH:mm")));
    for (QTimeEdit* te : {from, to}) {
        te->setDisplayFormat(QStringLiteral("HH:mm"));
        te->setFixedSize(116, 38);
        te->setAlignment(Qt::AlignCenter);
        te->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    QObject::connect(from, &QTimeEdit::timeChanged, [](const QTime& t){
        Prefs::setStr(QStringLiteral("xipher_quiet_hours_from"), t.toString(QStringLiteral("HH:mm"))); });
    QObject::connect(to, &QTimeEdit::timeChanged, [](const QTime& t){
        Prefs::setStr(QStringLiteral("xipher_quiet_hours_to"), t.toString(QStringLiteral("HH:mm"))); });
    {
        auto* pair = new QWidget();
        auto* ph = new QHBoxLayout(pair);
        ph->setContentsMargins(0, 0, 0, 0);
        ph->setSpacing(8);
        ph->addWidget(from);
        ph->addWidget(to);
        rowLabeled(QStringLiteral("Время тишины"), pair, b2);
    }
    b2->addSpacing(8);
    b2->addWidget(settingsNote(QStringLiteral("Уведомления будут тихими в этом диапазоне")));
    v->addWidget(c2);

    QVBoxLayout* b3 = nullptr;
    auto* c3 = sectionCard(QStringLiteral("События"), b3);
    addToggle(b3, QStringLiteral("Контакт присоединился"), QStringLiteral("xipher_notif_contact_join"), true);
    addToggle(b3, QStringLiteral("Закреплённые сообщения"), QStringLiteral("xipher_notif_pinned"), true);
    v->addWidget(c3);

    v->addStretch();
    return page;
}

// ── Приватность ────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildPrivacyPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);

    // ── Авто-удалённые сообщения ──
    QVBoxLayout* b0 = nullptr;
    auto* card0 = sectionCard(QStringLiteral("Авто-удаленные сообщения"), b0);
    auto* autoDel = settingsSelect();
    autoDel->addItem(QStringLiteral("Выкл"), QStringLiteral("off"));
    autoDel->addItem(QStringLiteral("Через 1 день"), QStringLiteral("1d"));
    autoDel->addItem(QStringLiteral("Через 1 неделю"), QStringLiteral("1w"));
    autoDel->addItem(QStringLiteral("Через 1 месяц"), QStringLiteral("1m"));
    autoDel->addItem(QStringLiteral("Своё время"), QStringLiteral("custom"));
    {
        const QString saved = Prefs::getStr(QStringLiteral("xipher_auto_delete"), QStringLiteral("off"));
        int i = autoDel->findData(saved);
        autoDel->setCurrentIndex(i < 0 ? 0 : i);
    }
    rowLabeled(QStringLiteral("Интервал"), autoDel, b0);
    auto* autoDelCustom = new QLineEdit();
    autoDelCustom->setPlaceholderText(QStringLiteral("30"));
    autoDelCustom->setFixedWidth(190);
    autoDelCustom->setMaximumHeight(38);
    autoDelCustom->setText(Prefs::getStr(QStringLiteral("xipher_auto_delete_custom")));
    rowLabeled(QStringLiteral("Своё время (дни)"), autoDelCustom, b0);
    auto syncCustom = [autoDel, autoDelCustom]() {
        autoDelCustom->setVisible(autoDel->currentData().toString() == QStringLiteral("custom"));
    };
    QObject::connect(autoDel, &QComboBox::currentIndexChanged, [syncCustom, autoDel, autoDelCustom](int){
        Prefs::setStr(QStringLiteral("xipher_auto_delete"), autoDel->currentData().toString());
        syncCustom();
    });
    QObject::connect(autoDelCustom, &QLineEdit::textChanged, [autoDelCustom](const QString& t){
        Prefs::setStr(QStringLiteral("xipher_auto_delete_custom"), t); });
    syncCustom();
    v->addWidget(card0);

    // ── Персональные данные ──
    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Персональные данные"), b);
    auto visibility = [] {
        auto* c = settingsSelect();
        c->addItem(QStringLiteral("Все"), QStringLiteral("everyone"));
        c->addItem(QStringLiteral("Друзья"), QStringLiteral("contacts"));
        c->addItem(QStringLiteral("Никто"), QStringLiteral("nobody"));
        return c;
    };
    auto* lastSeen = visibility();
    rowLabeled(QStringLiteral("Последнее время входа и онлайн"), lastSeen, b);
    auto* forward = visibility();
    rowLabeled(QStringLiteral("Пересылать от тебя сообщения"), forward, b);
    auto* callsVis = visibility();
    rowLabeled(QStringLiteral("Звонить тебе"), callsVis, b);
    auto* voice = visibility();
    rowLabeled(QStringLiteral("Отправлять тебе голосовые сообщения"), voice, b);
    auto* messages = visibility();
    rowLabeled(QStringLiteral("Отправлять тебе сообщения"), messages, b);
    auto* readRcpt = new ToggleSwitch();
    readRcpt->setChecked(Prefs::getBool(QStringLiteral("xipher_read_receipts"), true));
    rowLabeled(QStringLiteral("Отправлять отчеты о прочтении"), readRcpt, b);
    auto* blur = new ToggleSwitch();
    blur->setChecked(Prefs::getBool(QStringLiteral("xipher_chat_privacy"), false));
    rowLabeled(QStringLiteral("Размывать превью в списке чатов"), blur, b);
    auto* birthday = visibility();
    rowLabeled(QStringLiteral("Смотреть твою дату рождения"), birthday, b);
    auto* bioVis = visibility();
    rowLabeled(QStringLiteral("Смотреть описание твоего профиля"), bioVis, b);
    auto* groups = visibility();
    rowLabeled(QStringLiteral("Добавлять тебя в группы и каналы"), groups, b);

    // Локальные «кто видит» (как xipher_privacy_extra в вебе).
    const QJsonObject extra = QJsonDocument::fromJson(
        Prefs::getStr(QStringLiteral("xipher_privacy_extra"), QStringLiteral("{}")).toUtf8()).object();
    auto applyVis = [extra](QComboBox* c, const QString& key) {
        const QString val = extra.value(key).toString(QStringLiteral("contacts"));
        int i = c->findData(val);
        if (i >= 0) c->setCurrentIndex(i);
    };
    applyVis(forward, QStringLiteral("forward"));
    applyVis(callsVis, QStringLiteral("calls"));
    applyVis(voice, QStringLiteral("voice"));
    applyVis(messages, QStringLiteral("messages"));
    applyVis(groups, QStringLiteral("groups"));
    auto saveExtra = [forward, callsVis, voice, messages, groups]() {
        QJsonObject e{
            {QStringLiteral("forward"), forward->currentData().toString()},
            {QStringLiteral("calls"), callsVis->currentData().toString()},
            {QStringLiteral("voice"), voice->currentData().toString()},
            {QStringLiteral("messages"), messages->currentData().toString()},
            {QStringLiteral("groups"), groups->currentData().toString()}};
        Prefs::setStr(QStringLiteral("xipher_privacy_extra"),
                      QString::fromUtf8(QJsonDocument(e).toJson(QJsonDocument::Compact)));
    };
    for (QComboBox* c : {forward, callsVis, voice, messages, groups})
        QObject::connect(c, &QComboBox::currentIndexChanged, saveExtra);

    v->addWidget(card);

    // ── Удалить аккаунт при неактивности ──
    QVBoxLayout* bd0 = nullptr;
    auto* delCard = sectionCard(QStringLiteral("Удалить мой аккаунт при неактивности"), bd0);
    auto* delSel = settingsSelect();
    delSel->addItem(QStringLiteral("Через 1 месяц"), QStringLiteral("1m"));
    delSel->addItem(QStringLiteral("Через 3 месяца"), QStringLiteral("3m"));
    delSel->addItem(QStringLiteral("Через 6 месяцев"), QStringLiteral("6m"));
    delSel->addItem(QStringLiteral("Через 12 месяцев"), QStringLiteral("12m"));
    delSel->addItem(QStringLiteral("Через 2 года"), QStringLiteral("24m"));
    {
        int i = delSel->findData(Prefs::getStr(QStringLiteral("xipher_delete_account"), QStringLiteral("6m")));
        delSel->setCurrentIndex(i < 0 ? 2 : i);
    }
    QObject::connect(delSel, &QComboBox::currentIndexChanged, [delSel](int){
        Prefs::setStr(QStringLiteral("xipher_delete_account"), delSel->currentData().toString()); });
    rowLabeled(QStringLiteral("Период"), delSel, bd0);
    v->addWidget(delCard);

    // ── Сохранить (серверные поля) ──
    auto* save = new QPushButton(QStringLiteral("Сохранить"));
    save->setObjectName(QStringLiteral("primaryBtn"));
    save->setCursor(Qt::PointingHandCursor);
    auto* status = new QLabel(); status->setObjectName(QStringLiteral("statusOk"));
    connect(save, &QPushButton::clicked, this, [this, lastSeen, birthday, bioVis, readRcpt, status]() {
        QJsonObject f{
            {QStringLiteral("last_seen_visibility"), lastSeen->currentData().toString()},
            {QStringLiteral("birth_visibility"), birthday->currentData().toString()},
            {QStringLiteral("bio_visibility"), bioVis->currentData().toString()},
            {QStringLiteral("send_read_receipts"), readRcpt->isChecked()}
        };
        Prefs::setBool(QStringLiteral("xipher_read_receipts"), readRcpt->isChecked());
        api_->updateMyPrivacy(f);
        status->setText(QStringLiteral("Сохраняем…"));
    });
    connect(api_, &ApiClient::privacyUpdated, this, [status](bool ok){
        status->setStyleSheet(ok ? QString() : QStringLiteral("color:#E26A63;font-size:12px;"));
        status->setText(ok ? QStringLiteral("Сохранено ✓") : QStringLiteral("Ошибка сохранения"));
    });
    // подтянуть текущие значения из профиля
    connect(api_, &ApiClient::profileLoaded, this, [lastSeen, birthday, bioVis, readRcpt](const QJsonObject& obj){
        const QJsonObject pr = obj.value(QStringLiteral("user")).toObject().value(QStringLiteral("privacy")).toObject();
        auto set = [](QComboBox* c, const QString& val){ int i = c->findData(val); if (i>=0) c->setCurrentIndex(i); };
        set(lastSeen, pr.value(QStringLiteral("last_seen_visibility")).toString());
        set(birthday, pr.value(QStringLiteral("birth_visibility")).toString());
        set(bioVis, pr.value(QStringLiteral("bio_visibility")).toString());
        if (pr.contains(QStringLiteral("send_read_receipts")))
            readRcpt->setChecked(pr.value(QStringLiteral("send_read_receipts")).toBool());
    });
    {
        auto* actions = new QWidget();
        auto* ah = new QHBoxLayout(actions);
        ah->setContentsMargins(0, 0, 0, 0);
        ah->addWidget(status);
        ah->addStretch();
        ah->addWidget(save);
        v->addWidget(actions);
    }

    v->addStretch();
    return page;
}

// ── Звонки ─────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildCallsPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);

    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Звонки"), b);
    auto* accept = new ToggleSwitch();
    accept->setChecked(Prefs::getBool(QStringLiteral("xipher_calls_allow"), true));
    QObject::connect(accept, &QAbstractButton::toggled, [](bool on){ Prefs::setBool(QStringLiteral("xipher_calls_allow"), on); });
    rowLabeled(QStringLiteral("Принимать звонки"), accept, b);

    auto* who = settingsSelect();
    who->addItem(QStringLiteral("Все"), QStringLiteral("everyone"));
    who->addItem(QStringLiteral("Друзья"), QStringLiteral("contacts"));
    who->addItem(QStringLiteral("Никто"), QStringLiteral("nobody"));
    { int i = who->findData(Prefs::getStr(QStringLiteral("xipher_calls_who"), QStringLiteral("everyone"))); who->setCurrentIndex(i<0?0:i); }
    QObject::connect(who, &QComboBox::currentIndexChanged, [who](int){ Prefs::setStr(QStringLiteral("xipher_calls_who"), who->currentData().toString()); });
    rowLabeled(QStringLiteral("Кто может звонить"), who, b);
    v->addWidget(card);

    // Качество: разрешение + FPS (как в вебе, 1080p/30/60 — Premium).
    QVBoxLayout* b2 = nullptr;
    auto* card2 = sectionCard(QStringLiteral("Качество"), b2);
    auto addQuality = [&](const QString& label, const QString& keyRes, const QString& keyFps, const QString& defRes, const QString& defFps) {
        auto* res = settingsSelect();
        res->addItem(QStringLiteral("360p"), QStringLiteral("360"));
        res->addItem(QStringLiteral("540p"), QStringLiteral("540"));
        res->addItem(QStringLiteral("720p"), QStringLiteral("720"));
        res->addItem(QStringLiteral("1080p"), QStringLiteral("1080"));
        int i = res->findData(Prefs::getStr(keyRes, defRes)); res->setCurrentIndex(i<0?1:i);
        QObject::connect(res, &QComboBox::currentIndexChanged, [res, keyRes](int){ Prefs::setStr(keyRes, res->currentData().toString()); });
        auto* fps = settingsSelect();
        fps->setMinimumWidth(140);
        fps->addItem(QStringLiteral("15 FPS"), QStringLiteral("15"));
        fps->addItem(QStringLiteral("30 FPS"), QStringLiteral("30"));
        fps->addItem(QStringLiteral("60 FPS"), QStringLiteral("60"));
        i = fps->findData(Prefs::getStr(keyFps, defFps)); fps->setCurrentIndex(i<0?1:i);
        QObject::connect(fps, &QComboBox::currentIndexChanged, [fps, keyFps](int){ Prefs::setStr(keyFps, fps->currentData().toString()); });
        auto* pair = new QWidget();
        auto* ph = new QHBoxLayout(pair);
        ph->setContentsMargins(0, 0, 0, 0);
        ph->setSpacing(8);
        ph->addWidget(res);
        ph->addWidget(fps);
        rowLabeled(label, pair, b2);
    };
    addQuality(QStringLiteral("Камера"), QStringLiteral("xipher_call_camera_quality"), QStringLiteral("xipher_call_camera_fps"), QStringLiteral("540"), QStringLiteral("15"));
    addQuality(QStringLiteral("Демонстрация экрана"), QStringLiteral("xipher_call_screen_quality"), QStringLiteral("xipher_call_screen_fps"), QStringLiteral("720"), QStringLiteral("15"));
    b2->addSpacing(8);
    b2->addWidget(settingsNote(QStringLiteral("HD (1080p) и повышенный FPS доступны в Premium. Видеозвонки на десктопе появятся позже.")));
    v->addWidget(card2);

    v->addStretch();
    return page;
}

// ── Активные сеансы ──────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildSessionsPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);

    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Активные сеансы"), b);
    b->addWidget(settingsNote(QStringLiteral("Текущий сеанс (этот девайс)")));
    currentSessionBox_ = new QVBoxLayout();
    currentSessionBox_->setSpacing(6);
    b->addLayout(currentSessionBox_);
    v->addWidget(card);

    QVBoxLayout* b2 = nullptr;
    auto* card2 = sectionCard(QStringLiteral("Другие устройства"), b2);
    othersBox_ = new QVBoxLayout();
    othersBox_->setSpacing(4);
    b2->addLayout(othersBox_);
    b2->addWidget(settingsNote(QStringLiteral("Нет других сеансов")));
    {
        auto* actions = new QWidget();
        auto* ah = new QHBoxLayout(actions);
        ah->setContentsMargins(0, 4, 0, 0);
        ah->setSpacing(10);
        endSelectedBtn_ = new QPushButton(QStringLiteral("Завершить выбранные"));
        endSelectedBtn_->setObjectName(QStringLiteral("ghostBtn"));
        endSelectedBtn_->setCursor(Qt::PointingHandCursor);
        endSelectedBtn_->setEnabled(false);
        auto* endAll = new QPushButton(QStringLiteral("Завершить все остальные"));
        endAll->setObjectName(QStringLiteral("ghostBtn"));
        endAll->setCursor(Qt::PointingHandCursor);
        connect(endSelectedBtn_, &QPushButton::clicked, this, [this]() {
            for (const QString& sid : std::as_const(selectedSessions_))
                api_->revokeSession(sid);
            selectedSessions_.clear();
            endSelectedBtn_->setEnabled(false);
        });
        connect(endAll, &QPushButton::clicked, this, [this]() { api_->revokeOtherSessions(); });
        ah->addWidget(endSelectedBtn_);
        ah->addWidget(endAll);
        ah->addStretch();
        b2->addWidget(actions);
    }
    v->addWidget(card2);

    v->addStretch();

    connect(api_, &ApiClient::sessionsLoaded, this, [this](const QList<SessionInfo>& list) {
        if (!currentSessionBox_ || !othersBox_) return;
        while (QLayoutItem* it = currentSessionBox_->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
        while (QLayoutItem* it = othersBox_->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
        selectedSessions_.clear();
        if (endSelectedBtn_) endSelectedBtn_->setEnabled(false);
        bool emptyOthers = true;
        for (const SessionInfo& si : list) {
            const QString dev = si.userAgent.isEmpty() ? QStringLiteral("Устройство") : si.userAgent;
            auto* row = new QWidget();
            row->setMinimumHeight(46);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(2, 8, 2, 8);
            h->setSpacing(10);
            auto* info = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#726C82;font-size:11px;'>%2</span>")
                                    .arg(dev.toHtmlEscaped(), si.lastSeen.left(16).toHtmlEscaped()));
            info->setTextFormat(Qt::RichText);
            info->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:13px;"));
            h->addWidget(info, 1);
            if (si.current) {
                currentSessionBox_->addWidget(row);
            } else {
                emptyOthers = false;
                auto* cb = new QCheckBox();
                cb->setCursor(Qt::PointingHandCursor);
                const QString sid = si.id;
                connect(cb, &QCheckBox::toggled, this, [this, sid, cb](bool on) {
                    if (on) selectedSessions_.insert(sid); else selectedSessions_.remove(sid);
                    if (endSelectedBtn_) endSelectedBtn_->setEnabled(!selectedSessions_.isEmpty());
                });
                h->addWidget(cb);
                othersBox_->addWidget(row);
            }
        }
        // Скрываем «нет сеансов» через parent visibility.
        if (othersBox_->count() == 0) {
            auto* e = new QLabel(QStringLiteral("Нет других сеансов"));
            e->setObjectName(QStringLiteral("hint"));
            othersBox_->addWidget(e);
        }
    });
    connect(api_, &ApiClient::sessionsChanged, this, [this]() { refreshSessions(); });
    refreshSessions();
    return page;
}

void SettingsDialog::refreshSessions() { api_->getSessions(); }

// ── Заблокированные ──────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildBlockedPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 4, 12, 12);
    v->setSpacing(12);
    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Заблокированные пользователи"), b);

    const QJsonArray arr = QJsonDocument::fromJson(
        Prefs::getStr(QStringLiteral("xipher_blocked_users"), QStringLiteral("[]")).toUtf8()).array();
    if (arr.isEmpty()) {
        auto* e = new QLabel(QStringLiteral("Список пуст"));
        e->setObjectName(QStringLiteral("hint"));
        b->addWidget(e);
    } else {
        for (const QJsonValue& it : arr) {
            const QJsonObject o = it.toObject();
            auto* row = new QWidget();
            auto* h = new QHBoxLayout(row); h->setContentsMargins(0,0,0,0);
            auto* nm = new QLabel(o.value(QStringLiteral("name")).toString(
                o.value(QStringLiteral("username")).toString(QStringLiteral("Пользователь"))));
            nm->setStyleSheet(QStringLiteral("color:#CFC9DC;font-size:13px;"));
            h->addWidget(nm, 1);
            b->addWidget(row);
        }
    }
    v->addWidget(card);
    v->addStretch();
    return page;
}

// ── Язык ──────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildLanguagePage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 4, 12, 12);
    v->setSpacing(12);
    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Язык"), b);
    auto* lang = new QComboBox();
    lang->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    lang->addItem(QStringLiteral("English (скоро)"), QStringLiteral("en"));
    { int i = lang->findData(Prefs::getStr(QStringLiteral("xipher_language"), QStringLiteral("ru"))); lang->setCurrentIndex(i<0?0:i); }
    QObject::connect(lang, &QComboBox::currentIndexChanged, [lang](int){ Prefs::setStr(QStringLiteral("xipher_language"), lang->currentData().toString()); });
    b->addWidget(rowLabeled(QStringLiteral("Язык интерфейса"), lang));
    auto* h = new QLabel(QStringLiteral("Полный перевод интерфейса появится в обновлении."));
    h->setObjectName(QStringLiteral("hint")); h->setWordWrap(true);
    b->addWidget(h);
    v->addWidget(card);
    v->addStretch();
    return page;
}

// ── Email для входа ──────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildEmailPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 4, 12, 12);
    v->setSpacing(12);
    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Email для восстановления"), b);
    email_ = new QLineEdit();
    email_->setPlaceholderText(QStringLiteral("you@example.com"));
    b->addWidget(email_);
    auto* h = new QLabel(QStringLiteral("Используется для восстановления пароля."));
    h->setObjectName(QStringLiteral("hint"));
    b->addWidget(h);
    auto* save = new QPushButton(QStringLiteral("Сохранить"));
    save->setObjectName(QStringLiteral("primaryBtn"));
    save->setCursor(Qt::PointingHandCursor);
    auto* status = new QLabel(); status->setObjectName(QStringLiteral("statusOk"));
    connect(save, &QPushButton::clicked, this, [this, status]() {
        api_->setRecoveryEmail(email_->text().trimmed());
        status->setText(QStringLiteral("Сохраняем…"));
    });
    connect(api_, &ApiClient::recoveryEmailSaved, this, [status](bool ok, const QString& m){
        status->setStyleSheet(ok ? QString() : QStringLiteral("color:#E5687A;font-size:12px;"));
        status->setText(ok ? QStringLiteral("Сохранено ✓") : (QStringLiteral("Ошибка: ") + m));
    });
    connect(api_, &ApiClient::recoveryEmailLoaded, this, [this](const QString& e){ if (email_) email_->setText(e); });
    auto* row = new QWidget(); auto* rh = new QHBoxLayout(row); rh->setContentsMargins(0,0,0,0);
    rh->addWidget(status); rh->addStretch(); rh->addWidget(save);
    b->addWidget(row);
    v->addWidget(card);
    v->addStretch();
    api_->getRecoveryEmail();
    return page;
}

QWidget* SettingsDialog::buildExportPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);

    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Экспорт данных"), b);
    b->addWidget(settingsNote(QStringLiteral(
        "Вся переписка одним JSON-файлом: личные чаты, группы и каналы "
        "(до 1000 последних сообщений на чат). Файл собирается на сервере "
        "и сохраняется на этот компьютер.")));
    b->addSpacing(10);
    auto* go = new QPushButton(QStringLiteral("Экспортировать"));
    go->setObjectName(QStringLiteral("primaryBtn"));
    go->setCursor(Qt::PointingHandCursor);
    auto* status = new QLabel();
    status->setObjectName(QStringLiteral("hint"));
    status->setWordWrap(true);
    connect(go, &QPushButton::clicked, this, [this, go, status]() {
        go->setEnabled(false);
        status->setText(QStringLiteral("Собираем данные на сервере…"));
        api_->exportData();
    });
    connect(api_, &ApiClient::dataExported, this, [this, go, status](bool ok, const QByteArray& bytes) {
        go->setEnabled(true);
        if (!ok) { status->setText(QStringLiteral("Не удалось собрать экспорт")); return; }
        const QString defaultName = QStringLiteral("xipher-export-%1.json")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm")));
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить экспорт"),
                                                          defaultName, QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) { status->clear(); return; }
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(bytes);
            f.close();
            status->setText(QStringLiteral("Готово: %1 (%2 КБ)")
                .arg(QFileInfo(path).fileName()).arg(bytes.size() / 1024));
        } else {
            status->setText(QStringLiteral("Не удалось сохранить файл"));
        }
    });
    {
        auto* row = new QWidget();
        auto* rh = new QHBoxLayout(row);
        rh->setContentsMargins(0, 0, 0, 0);
        rh->setSpacing(12);
        rh->addWidget(go);
        rh->addWidget(status, 1);
        b->addWidget(row);
    }
    v->addWidget(card);
    v->addStretch();
    return page;
}

// ── Оформление: галерея тем (мгновенное применение, как theme-gallery веба) ──
QWidget* SettingsDialog::buildAppearancePage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);

    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("Тема оформления"), b);
    b->addWidget(settingsNote(QStringLiteral(
        "Выберите тему — применяется мгновенно и сохраняется на этом устройстве.")));
    b->addSpacing(10);

    const auto presets = ThemePreset::all();
    const QString currentId = ThemePreset::current().id;
    themeGrid_ = new QGridLayout();
    QGridLayout* grid = themeGrid_;
    grid->setSpacing(10);
    int col = 0, row = 0;
    for (const auto& t : presets) {
        auto* tile = new QPushButton();
        tile->setObjectName(QStringLiteral("themeTile"));
        tile->setCursor(Qt::PointingHandCursor);
        tile->setFixedSize(150, 96);
        tile->setEnabled(t.available);
        tile->setToolTip(t.id);
        auto* tv = new QVBoxLayout(tile);
        tv->setContentsMargins(10, 10, 10, 8);
        tv->setSpacing(8);
        // Свотчи: фон / поверхность / акцент.
        auto* swRow = new QHBoxLayout();
        swRow->setSpacing(5);
        const QColor border(255, 255, 255, 30);
        for (const QColor& c : {t.bgBase, t.surface2, QColor(t.accent)}) {
            auto* sw = new QLabel(tile);
            sw->setFixedSize(26, 26);
            sw->setAlignment(Qt::AlignCenter);
            sw->setStyleSheet(QStringLiteral(
                "background:%1;border:1px solid rgba(255,255,255,0.18);border-radius:7px;")
                .arg(c.name()));
            swRow->addWidget(sw);
        }
        swRow->addStretch();
        if (t.id == currentId) {
            auto* chk = new QLabel(QStringLiteral("✓"), tile);
            chk->setStyleSheet(QStringLiteral("color:#46B98A;font-size:16px;font-weight:700;"));
            swRow->addWidget(chk);
        }
        tv->addLayout(swRow);
        auto* nm = new QLabel(t.name, tile);
        nm->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:13px;font-weight:600;"));
        tv->addWidget(nm);
        if (!t.available) {
            tile->setToolTip(QStringLiteral("Скоро"));
            nm->setText(t.name + QStringLiteral("  ·  скоро"));
            nm->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
        }
        const QString id = t.id;
        connect(tile, &QPushButton::clicked, this, [this, id]() {
            if (id == ThemePreset::current().id) return;
            ThemePreset::save(id);
            emit themeChanged();
            rebuildAppearanceGallery();   // переместить отметку «✓»
        });
        grid->addWidget(tile, row, col);
        if (++col == 3) { ++row; col = 0; }
    }
    b->addLayout(grid);
    v->addWidget(card);

    v->addStretch();
    return page;
}

void SettingsDialog::rebuildAppearanceGallery() {
    // Перезагружаем страницу оформления — отметка «✓» переедет.
    if (appearanceIdx_ < 0) return;
    QWidget* old = stack_->widget(appearanceIdx_);
    stack_->removeWidget(old);
    old->deleteLater();
    stack_->insertWidget(appearanceIdx_, wrapPage(buildAppearancePage()));
    stack_->setCurrentIndex(appearanceIdx_);
}
namespace {

// Иконки карточек возможностей Pulse (штриховые, accent-text).
static QPixmap pulsePerkIcon(const QString& kind, const ThemePreset::Tokens& th) {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(th.accent).lighter(125), 3.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath path;
    if (kind == QLatin1String("layers")) {           // стопка слоёв
        path.moveTo(8, 12); path.lineTo(20, 6); path.lineTo(32, 12); path.lineTo(20, 18); path.closeSubpath();
        p.drawPath(path);
        p.drawPath(QPainterPath()); 
        QPainterPath l2; l2.moveTo(8, 16); l2.lineTo(20, 22); l2.lineTo(32, 16);
        p.drawPath(l2);
        QPainterPath l3; l3.moveTo(8, 20); l3.lineTo(20, 26); l3.lineTo(32, 20);
        p.drawPath(l3);
    } else if (kind == QLatin1String("pulse")) {      // пульс с точкой
        path.moveTo(6, 20); path.lineTo(13, 20); path.lineTo(16, 14); path.lineTo(20, 26);
        path.lineTo(24, 14); path.lineTo(27, 20); path.lineTo(34, 20);
        p.drawPath(path);
        p.drawEllipse(QPointF(33, 8), 3, 3);
    } else if (kind == QLatin1String("bot")) {        // голова бота
        p.drawRoundedRect(QRect(8, 14, 24, 16), 5, 5);
        p.drawEllipse(QPointF(15, 22), 2, 2);
        p.drawEllipse(QPointF(25, 22), 2, 2);
        p.drawLine(QPointF(20, 14), QPointF(20, 8));
        p.drawLine(QPointF(16, 8), QPointF(24, 8));
    } else if (kind == QLatin1String("avatar")) {     // живой аватар
        p.drawRoundedRect(QRect(8, 12, 24, 18), 5, 5);
        QPainterPath tri; tri.moveTo(17, 17); tri.lineTo(25, 21); tri.lineTo(17, 25); tri.closeSubpath();
        p.drawPath(tri);
    } else if (kind == QLatin1String("globe")) {      // глобус
        p.drawEllipse(QRect(7, 7, 26, 26));
        p.drawLine(QPointF(7, 20), QPointF(33, 20));
        path.moveTo(20, 7); path.cubicTo(26, 13, 26, 27, 20, 33);
        path.moveTo(20, 7); path.cubicTo(14, 13, 14, 27, 20, 33);
        p.drawPath(path);
    } else {                                          // портфель
        p.drawRoundedRect(QRect(7, 14, 26, 18), 4, 4);
        path.moveTo(15, 14); path.lineTo(15, 11); path.cubicTo(15, 8, 17, 7, 20, 7);
        path.cubicTo(23, 7, 25, 8, 25, 11); path.lineTo(25, 14);
        p.drawPath(path);
        p.drawLine(QPointF(7, 22), QPointF(33, 22));
    }
    p.end();
    return pm;
}

// ── Обложка Xipher Pulse: градиент + аврора + анимированная линия пульса ────
class PulseHero : public QFrame {
public:
    explicit PulseHero(QWidget* parent = nullptr) : QFrame(parent) {
        setFixedHeight(210);
        anim_.setStartValue(1490.0);
        anim_.setEndValue(0.0);
        anim_.setDuration(3600);
        anim_.setLoopCount(-1);
        connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant&) { update(); });
    }
    void setVisible(bool v) override {
        QFrame::setVisible(v);
        if (v) anim_.start(); else anim_.stop();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect r = rect();
        const auto th = ThemePreset::current();
        const QColor accent(th.accent);

        // Фон: линейный градиент surface-2 → surface-1 (160°).
        QLinearGradient lg(0, 0, r.width() * 0.42, r.height());
        lg.setColorAt(0, th.surface2);
        lg.setColorAt(1, th.surface1);
        p.fillRect(r, lg);
        // Радиальное свечение акцента у верхнего левого угла.
        QRadialGradient rg(QPointF(r.width() * 0.15, -r.height() * 0.2), r.height() * 1.4);
        rg.setColorAt(0, QColor(th.accentR, th.accentG, th.accentB, 56));
        rg.setColorAt(1, QColor(th.accentR, th.accentG, th.accentB, 0));
        p.fillRect(r, rg);
        // «Аврора» — мягкая полоса свечения по верху.
        QRadialGradient au(QPointF(r.width() * 0.5, 0), r.width() * 0.6);
        au.setColorAt(0, QColor(th.accentR, th.accentG, th.accentB, 40));
        au.setColorAt(1, QColor(th.accentR, th.accentG, th.accentB, 0));
        p.fillRect(QRect(0, 0, r.width(), 110), au);

        // Линия пульса: путь из разметки веба (viewBox 600x120).
        QPainterPath trace;
        trace.moveTo(0, 60);
        trace.lineTo(150, 60); trace.lineTo(168, 26); trace.lineTo(182, 94);
        trace.lineTo(198, 42); trace.lineTo(210, 76); trace.lineTo(224, 60);
        trace.lineTo(284, 60); trace.lineTo(302, 20); trace.lineTo(318, 100);
        trace.lineTo(332, 42); trace.lineTo(344, 74); trace.lineTo(360, 56);
        trace.lineTo(540, 56); trace.lineTo(540 + 60 * 0 + 0, 56);
        // масштаб под ширину карточки (600 → width)
        p.save();
        p.scale(width() / 600.0, 1.0);
        // Постоянный след.
        QPen tpen(accent, 1.4);
        tpen.setColor(QColor(th.accentR, th.accentG, th.accentB, 71));
        p.setPen(tpen);
        p.drawPath(trace);
        // Бегущий отрезок (spark): штрих 90 из шага 1490, офсет анимируется.
        QPen spen(QColor(th.accent).lighter(125), 2.2);
        spen.setCapStyle(Qt::RoundCap);
        const qreal dash = 1490.0;
        QVector<qreal> pattern { 90.0, dash - 90.0 };
        spen.setDashPattern(pattern);
        spen.setDashOffset(dash - anim_.currentValue().toReal());
        p.setPen(spen);
        p.drawPath(trace);
        p.restore();
    }

private:
    QVariantAnimation anim_;
};

// ── Плитка тарифа: рамка/градиент при выборе + полоса сверху ────────────────
class PulsePlanButton : public QPushButton {
public:
    PulsePlanButton(const QString& tag, const QString& tagColor,
                    const QString& price, const QString& period,
                    const QString& note, QWidget* parent = nullptr)
        : QPushButton(parent) {
        setCheckable(true);
        setMinimumHeight(128);
        setCursor(Qt::PointingHandCursor);
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(14, 12, 12, 12);
        v->setSpacing(2);
        auto* tagL = new QLabel(tag.toUpper(), this);
        tagL->setStyleSheet(QStringLiteral(
            "color:%1;font-size:10px;font-weight:700;letter-spacing:1px;").arg(tagColor));
        v->addWidget(tagL);
        auto* priceL = new QLabel(price, this);
        priceL->setStyleSheet(QStringLiteral("font-size:24px;font-weight:800;color:#F3F1F8;"));
        v->addWidget(priceL);
        auto* periodL = new QLabel(period, this);
        periodL->setStyleSheet(QStringLiteral("font-size:13px;color:#ACA6BD;"));
        v->addWidget(periodL);
        auto* noteL = new QLabel(note, this);
        noteL->setWordWrap(true);
        noteL->setStyleSheet(QStringLiteral("font-size:12px;color:#ACA6BD;"));
        v->addWidget(noteL);
        connect(this, &QPushButton::toggled, this, [this](bool) { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect r = rect();
        const auto th = ThemePreset::current();
        p.setRenderHint(QPainter::Antialiasing, false);
        if (isChecked()) {
            QLinearGradient lg(0, 0, r.width() * 0.4, r.height());
            lg.setColorAt(0, QColor(th.accentR, th.accentG, th.accentB, 56));
            lg.setColorAt(1, th.surface1);
            p.fillRect(r, lg);
        } else {
            p.fillRect(r, th.surface1);
        }
        p.setPen(QPen(isChecked() ? QColor(th.accent)
                                  : QColor(255, 255, 255, 26), 1));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(th.accent));
            p.drawRect(9, 0, r.width() - 18, 3);
        }
    }
};

} // namespace

// ── Premium ──────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildPremiumPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(17);

    const auto th = ThemePreset::current();
    const bool active = Session::instance().isPremium;
    const QString plan = Session::instance().premiumPlan.isEmpty()
        ? QStringLiteral("month") : Session::instance().premiumPlan;
    planSel_ = plan;

    // ── Обложка ──
    auto* hero = new PulseHero(page);
    hero->setObjectName(QStringLiteral("pulseHero"));
    auto* hv = new QVBoxLayout(hero);
    hv->setContentsMargins(26, 30, 26, 22);
    hv->setSpacing(8);

    auto* mark = new QLabel(hero);
    mark->setFixedSize(60, 34);
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(QStringLiteral(
        "background:rgba(139,92,246,0.14);border:1px solid rgba(139,92,246,0.34);border-radius:12px;"));
    {
        const QColor c = QColor(th.accent).lighter(125);
        QPixmap pm(80, 44);
        pm.fill(Qt::transparent);
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::Antialiasing);
        QPen pen(c, 4.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        pp.setPen(pen);
        QPainterPath path;
        path.moveTo(4, 22); path.lineTo(18, 22); path.lineTo(26, 6); path.lineTo(36, 38);
        path.lineTo(44, 16); path.lineTo(50, 28); path.lineTo(56, 22); path.lineTo(74, 22);
        pp.drawPath(path);
        mark->setPixmap(pm.scaled(80, 44));
    }
    hv->addWidget(mark);

    auto* title = new QLabel(QStringLiteral("Xipher Pulse"), hero);
    title->setStyleSheet(QStringLiteral("font-size:30px;font-weight:800;color:#F3F1F8;letter-spacing:-0.4px;"));
    hv->addWidget(title);
    auto* sub = new QLabel(QStringLiteral(
        "Мессенджер, который держит ваш ритм: больше папок, чище звонки, свои боты."), hero);
    sub->setWordWrap(true);
    sub->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:14px;"));
    hv->addWidget(sub);

    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    statusChip_ = new QLabel(active ? QStringLiteral("Pulse активен") : QStringLiteral("Бесплатный план"), hero);
    statusChip_->setObjectName(active ? QStringLiteral("pulseChipPaid") : QStringLiteral("pulseChip"));
    expiryChip_ = new QLabel(hero);
    expiryChip_->setObjectName(QStringLiteral("pulseChipGhost"));
    paymentChip_ = new QLabel(hero);
    paymentChip_->setObjectName(QStringLiteral("pulseChipWait"));
    paymentChip_->hide();
    chips->addWidget(statusChip_);
    chips->addWidget(expiryChip_);
    chips->addWidget(paymentChip_);
    chips->addStretch();
    hv->addLayout(chips);
    v->addWidget(hero);

    auto restyleChips = [this]() {
        const auto th = ThemePreset::current();
        const bool act = Session::instance().isPremium;
        statusChip_->setText(act ? QStringLiteral("Pulse активен") : QStringLiteral("Бесплатный план"));
        statusChip_->setStyleSheet(QStringLiteral(
            "background:rgba(%1,%2,%3,0.22);border:1px solid rgba(%1,%2,%3,0.34);"
            "border-radius:10px;padding:4px 11px;font-size:12px;font-weight:600;color:#F3F1F8;")
            .arg(th.accentR).arg(th.accentG).arg(th.accentB));
        const QString exp = Session::instance().premiumExpiresAt;
        if (act && !exp.isEmpty()) {
            expiryChip_->setText(QStringLiteral("До ") + exp.left(10));
            expiryChip_->show();
        } else expiryChip_->hide();
    };
    restyleChips();

    // ── Тарифы ──
    plansGrid_ = new QGridLayout();
    plansGrid_->setSpacing(11);
    auto* plansRow = plansGrid_;
    auto* planTrial = new PulsePlanButton(QStringLiteral("Попробовать"), th.accent,
        QStringLiteral("9 ₽"), QStringLiteral("за 7 дней"),
        QStringLiteral("Все возможности на неделю"), page);
    auto* planYear  = new PulsePlanButton(QStringLiteral("Выгодно · −58%"), QStringLiteral("#46B98A"),
        QStringLiteral("499 ₽"), QStringLiteral("за год"),
        QStringLiteral("42 ₽ в месяц"), page);
    auto* planMonth = new PulsePlanButton(QStringLiteral("Гибко"), th.accent,
        QStringLiteral("99 ₽"), QStringLiteral("в месяц"),
        QStringLiteral("Отключить можно в любой момент"), page);
    const QList<QPair<PulsePlanButton*, QString>> planBtns = {
        {planTrial, QStringLiteral("trial")}, {planYear, QStringLiteral("year")},
        {planMonth, QStringLiteral("month")}};
    for (int i = 0; i < planBtns.size(); ++i) {
        planBtns[i].first->setChecked(planBtns[i].second == planSel_);
        plansRow->addWidget(planBtns[i].first, i / 3, i % 3);
    }
    auto pickPlan = [planBtns, this](const QString& id) {
        planSel_ = id;
        for (const auto& pb : planBtns) pb.first->setChecked(pb.second == id);
        updatePulseCta();
    };
    for (const auto& pb : planBtns)
        connect(pb.first, &QPushButton::clicked, this, [pickPlan, pb]() { pickPlan(pb.second); });
    v->addLayout(plansRow);

    // ── CTA ──
    auto* ctaRow = new QHBoxLayout();
    ctaRow->setSpacing(10);
    subscribeBtn_ = new QPushButton(page);
    subscribeBtn_->setObjectName(QStringLiteral("pulseCta"));
    subscribeBtn_->setCursor(Qt::PointingHandCursor);
    subscribeBtn_->setFixedHeight(52);
    connect(subscribeBtn_, &QPushButton::clicked, this, &SettingsDialog::openPulsePayment);
    manageBtn_ = new QPushButton(QStringLiteral("Отключить Pulse"), page);
    manageBtn_->setObjectName(QStringLiteral("pulseCtaGhost"));
    manageBtn_->setCursor(Qt::PointingHandCursor);
    manageBtn_->setFixedHeight(52);
    connect(manageBtn_, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, QStringLiteral("Xipher Pulse"),
            QStringLiteral("Подписка не продлевается автоматически — вы продлеваете её сами. "
                           "После истечения срока аккаунт вернётся на бесплатный план."));
    });
    ctaRow->addWidget(subscribeBtn_, 1);
    ctaRow->addWidget(manageBtn_);
    v->addLayout(ctaRow);

    auto* fine = new QLabel(QStringLiteral(
        "Оплата на защищённой странице провайдера. Подписка не продлевается "
        "автоматически — вы продлеваете её сами."), page);
    fine->setWordWrap(true);
    fine->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:12px;"));
    v->addWidget(fine);

    // ── Счётчики папок (реальные: лимит 3/20, использовано из сервера) ──
    auto* metersRow = new QHBoxLayout();
    metersRow->setSpacing(10);
    auto makeMeter = [this](const QString& label, bool accentTile) {
        auto* m = new QFrame();
        m->setObjectName(accentTile ? QStringLiteral("pulseMeterAccent") : QStringLiteral("pulseMeter"));
        m->setFixedHeight(76);
        auto* mv = new QVBoxLayout(m);
        mv->setContentsMargins(10, 12, 10, 12);
        mv->setSpacing(1);
        auto* val = new QLabel(QStringLiteral("0"), m);
        val->setAlignment(Qt::AlignCenter);
        val->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:#F3F1F8;"));
        auto* lab = new QLabel(label, m);
        lab->setAlignment(Qt::AlignCenter);
        lab->setStyleSheet(QStringLiteral("font-size:12px;color:#ACA6BD;"));
        mv->addWidget(val);
        mv->addWidget(lab);
        if (label == QStringLiteral("Лимит папок")) meterLimit_ = val;
        else if (label == QStringLiteral("Использовано")) meterUsed_ = val;
        else meterFree_ = val;
        return m;
    };
    metersRow->addWidget(makeMeter(QStringLiteral("Лимит папок"), false), 1);
    metersRow->addWidget(makeMeter(QStringLiteral("Использовано"), false), 1);
    metersRow->addWidget(makeMeter(QStringLiteral("Свободно"), true), 1);
    v->addLayout(metersRow);
    connect(api_, &ApiClient::foldersLoaded, this, [this]() { refreshPulseMeters(); });
    refreshPulseMeters();

    // ── Возможности (6 карточек с иконками) ──
    perksGrid_ = new QGridLayout();
    QGridLayout* perksGrid = perksGrid_;
    perksGrid->setHorizontalSpacing(11);
    perksGrid->setVerticalSpacing(11);
    struct Perk { QString icon; QString title; QString text; };
    const QList<Perk> perks = {
        {QStringLiteral("layers"), QStringLiteral("Лимиты вдвое выше"),
         QStringLiteral("До 1000 каналов, 10 закреплённых чатов, 20 публичных ссылок и 20 папок.")},
        {QStringLiteral("pulse"), QStringLiteral("Звонки в 1080p и 60 FPS"),
         QStringLiteral("Камера и демонстрация экрана без потери деталей — видно код на экране собеседника.")},
        {QStringLiteral("bot"), QStringLiteral("Свои боты и Bot IDE"),
         QStringLiteral("Пишите ботов в браузере, берите шаблоны — мы держим их на своём хостинге.")},
        {QStringLiteral("avatar"), QStringLiteral("Живые аватары"),
         QStringLiteral("GIF в профиле проигрывается прямо в списке чатов, а не замирает картинкой.")},
        {QStringLiteral("globe"), QStringLiteral("Перевод в одно касание"),
         QStringLiteral("Русский и английский прямо в сообщении, без копирования в переводчик.")},
        {QStringLiteral("case"), QStringLiteral("Часы работы для дела"),
         QStringLiteral("Профиль сам показывает, открыты вы сейчас или ответите утром.")},
    };
    for (int i = 0; i < perks.size(); ++i) {
        auto* perk = new QFrame();
        perk->setObjectName(QStringLiteral("pulsePerk"));
        auto* pv = new QVBoxLayout(perk);
        pv->setContentsMargins(16, 14, 16, 14);
        pv->setSpacing(6);
        auto* ic = new QLabel(perk);
        ic->setFixedSize(38, 38);
        ic->setAlignment(Qt::AlignCenter);
        ic->setStyleSheet(QStringLiteral(
            "background:rgba(%1,%2,%3,0.14);border:1px solid rgba(%1,%2,%3,0.34);border-radius:11px;")
            .arg(th.accentR).arg(th.accentG).arg(th.accentB));
        ic->setPixmap(pulsePerkIcon(perks[i].icon, th));
        pv->addWidget(ic);
        auto* h3 = new QLabel(perks[i].title, perk);
        h3->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;color:#F3F1F8;"));
        pv->addWidget(h3);
        auto* txt = new QLabel(perks[i].text, perk);
        txt->setWordWrap(true);
        txt->setStyleSheet(QStringLiteral("font-size:13px;color:#ACA6BD;"));
        pv->addWidget(txt);
        perksGrid->addWidget(perk, i / 2, i % 2);
    }
    v->addLayout(perksGrid);

    // ── Оплата: создание платежа и ожидание ──
    connect(api_, &ApiClient::premiumPaymentReady, this, [this](bool ok, const QJsonObject& data, const QString& msg) {
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Xipher Pulse"),
                                 msg.isEmpty() ? QStringLiteral("Не удалось создать платёж") : msg);
            return;
        }
        const QString provider = data.value(QStringLiteral("provider")).toString();
        if (provider == QLatin1String("stripe")) {
            const QString url = data.value(QStringLiteral("checkout_url")).toString();
            if (url.isEmpty()) return;
            QDesktopServices::openUrl(QUrl(url));
        } else {
            // ЮMoney quickpay: GET-подтверждение эквивалентно форме веба.
            QUrl u(data.value(QStringLiteral("form_action")).toString());
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("receiver"), data.value(QStringLiteral("receiver")).toString());
            q.addQueryItem(QStringLiteral("quickpay-form"), QStringLiteral("button"));
            q.addQueryItem(QStringLiteral("sum"), data.value(QStringLiteral("sum")).toString());
            q.addQueryItem(QStringLiteral("paymentType"), QStringLiteral("AC"));
            q.addQueryItem(QStringLiteral("label"), data.value(QStringLiteral("label")).toString());
            if (!data.value(QStringLiteral("success_url")).toString().isEmpty())
                q.addQueryItem(QStringLiteral("successURL"), data.value(QStringLiteral("success_url")).toString());
            u.setQuery(q);
            QDesktopServices::openUrl(u);
            pendingLabel_ = data.value(QStringLiteral("label")).toString();
            Prefs::setStr(QStringLiteral("xipher_pulse_pending_label"), pendingLabel_);
            Prefs::setStr(QStringLiteral("xipher_pulse_pending_plan"), planSel_);
        }
        Prefs::setStr(QStringLiteral("xipher_pulse_pending"), QStringLiteral("1"));
        showPulsePending();
        startPulsePolling();
    });
    connect(api_, &ApiClient::premiumStatusRefreshed, this, [this, restyleChips](bool act, const QString& expiresAt) {
        if (!act) return;
        stopPulsePolling();
        Prefs::setStr(QStringLiteral("xipher_pulse_pending"), QStringLiteral("0"));
        Session& st = Session::instance();
        st.isPremium = true;
        if (!expiresAt.isEmpty()) st.premiumExpiresAt = expiresAt;
        st.save();
        restyleChips();
        updatePulseCta();
        refreshPulseMeters();
        QMessageBox::information(this, QStringLiteral("Xipher Pulse"),
                                 QStringLiteral("Pulse активен! Спасибо за поддержку."));
    });

    // Зависший платёж с прошлого раза → показать ожидание и продолжить опрос.
    if (Prefs::getStr(QStringLiteral("xipher_pulse_pending")) == QLatin1String("1")) {
        pendingLabel_ = Prefs::getStr(QStringLiteral("xipher_pulse_pending_label"));
        showPulsePending();
        startPulsePolling();
    }
    updatePulseCta();

    v->addStretch();
    return page;
}

void SettingsDialog::updatePulseCta() {
    if (!subscribeBtn_ || !manageBtn_) return;
    const bool act = Session::instance().isPremium;
    subscribeBtn_->setEnabled(!act);
    subscribeBtn_->setText(act ? QStringLiteral("Pulse уже подключён")
        : (QStringLiteral("Подключить Pulse · ")
           + (planSel_ == QLatin1String("year") ? QStringLiteral("499 ₽")
            : planSel_ == QLatin1String("trial") ? QStringLiteral("9 ₽")
            : QStringLiteral("99 ₽"))));
    manageBtn_->setVisible(act);
}

void SettingsDialog::refreshPulseMeters() {
    if (!meterLimit_ || !meterUsed_ || !meterFree_) return;
    const int limit = Session::instance().isPremium ? 20 : 3;
    const int used = api_->folderCount();
    const int free = qMax(0, limit - used);
    meterLimit_->setText(QString::number(limit));
    meterUsed_->setText(QString::number(used));
    meterFree_->setText(QString::number(free));
}

void SettingsDialog::showPulsePending() {
    if (!paymentChip_) return;
    paymentChip_->setText(QStringLiteral("●  Ожидание оплаты"));
    paymentChip_->setStyleSheet(QStringLiteral(
        "#pulseChipWait{background:rgba(217,160,91,0.14);border:1px solid #D9A05B;"
        "border-radius:10px;padding:4px 11px;font-size:12px;font-weight:600;color:#F3F1F8;}"));
    paymentChip_->show();
    if (blinkTimer_) return;
    blinkTimer_ = new QTimer(this);
    blinkTimer_->setInterval(550);
    bool on = true;
    connect(blinkTimer_, &QTimer::timeout, this, [this, on]() mutable {
        on = !on;
        paymentChip_->setStyleSheet(paymentChip_->styleSheet().replace(
            on ? QStringLiteral("opacity:0.4;") : QStringLiteral(""),
            on ? QStringLiteral("") : QStringLiteral("opacity:0.4;")));
    });
    blinkTimer_->start();
}

void SettingsDialog::startPulsePolling() {
    stopPulsePolling();
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(5000);
    connect(pollTimer_, &QTimer::timeout, this, [this]() {
        int attempts = 0;   // ограничение как в вебе: не вечно
        Q_UNUSED(attempts);
        api_->premiumRefreshStatus();
    });
    pollTimer_->start();
}

void SettingsDialog::stopPulsePolling() {
    if (pollTimer_) { pollTimer_->stop(); pollTimer_->deleteLater(); pollTimer_ = nullptr; }
}

void SettingsDialog::openPulsePayment() {
    auto* pay = new ModalOverlay(window(), 400);
    pay->card()->setFixedHeight(320);
    pay->card()->setStyleSheet(card()->styleSheet());
    auto* vl = pay->cardLayout();
    vl->setContentsMargins(20, 18, 20, 18);
    vl->setSpacing(12);
    auto* t = new QLabel(QStringLiteral("Оплата Xipher Pulse"));
    t->setStyleSheet(QStringLiteral("font-size:17px;font-weight:800;color:#F3F1F8;"));
    vl->addWidget(t);
    vl->addWidget(settingsNote(QStringLiteral("План: ") + (planSel_ == QLatin1String("year")
        ? QStringLiteral("год · 499 ₽") : planSel_ == QLatin1String("trial")
        ? QStringLiteral("7 дней · 9 ₽") : QStringLiteral("месяц · 99 ₽"))));
    auto* provider = settingsSelect();
    provider->addItem(QStringLiteral("ЮMoney"), QStringLiteral("yoomoney"));
    provider->addItem(QStringLiteral("Stripe"), QStringLiteral("stripe"));
    vl->addWidget(rowLabeled(QStringLiteral("Способ оплаты"), provider));
    vl->addStretch();
    auto* go = new QPushButton(QStringLiteral("Перейти к оплате"));
    go->setObjectName(QStringLiteral("primaryBtn"));
    go->setCursor(Qt::PointingHandCursor);
    connect(go, &QPushButton::clicked, this, [this, pay, provider]() {
        api_->premiumCreatePayment(planSel_, provider->currentData().toString());
        pay->closeAnimated();
    });
    vl->addWidget(go, 0, Qt::AlignRight);
    pay->showAnimated();
}

// ── О приложении / FAQ ───────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildAboutPage() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 4, 12, 12);
    v->setSpacing(14);

    QVBoxLayout* b = nullptr;
    auto* card = sectionCard(QStringLiteral("О приложении"), b);
    auto* name = new QLabel(QStringLiteral("Xipher Desktop")); name->setObjectName(QStringLiteral("heroName"));
    auto* ver = new QLabel(QStringLiteral("Версия 0.1 · нативный клиент (Qt)")); ver->setObjectName(QStringLiteral("hint"));
    b->addWidget(name); b->addWidget(ver);
    v->addWidget(card);

    QVBoxLayout* fb = nullptr;
    auto* faq = sectionCard(QStringLiteral("FAQ"), fb);
    auto addQA = [&](const QString& q, const QString& a) {
        auto* qq = new QLabel(q); qq->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:13px;font-weight:700;"));
        auto* aa = new QLabel(a); aa->setObjectName(QStringLiteral("hint")); aa->setWordWrap(true);
        fb->addWidget(qq); fb->addWidget(aa);
    };
    addQA(QStringLiteral("Как сменить фото профиля?"),
          QStringLiteral("Настройки → Мой аккаунт → «Сменить фото»."));
    addQA(QStringLiteral("Почему звонок соединяется не сразу?"),
          QStringLiteral("При VPN соединение идёт через TURN-сервер и занимает несколько секунд."));
    addQA(QStringLiteral("Где включить приватность?"),
          QStringLiteral("Настройки → Приватность: кто видит онлайн, ДР, отчёты о прочтении."));
    v->addWidget(faq);
    v->addStretch();
    return page;
}
