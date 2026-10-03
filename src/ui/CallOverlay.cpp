#include "ui/CallOverlay.h"
#include "net/Prefs.h"
#include "ui/AvatarUtil.h"
#include "ui/Icons.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QEvent>

// Палитра css/calls.css (--call-*).
namespace {
const QColor kBgA(0x05, 0x05, 0x08);
const QColor kBgB(0x0D, 0x05, 0x15);
const QColor kBgC(0x0A, 0x08, 0x10);
const QColor kAccent(0x8B, 0x5C, 0xF6);
const QColor kAccentDark(0x6D, 0x28, 0xD9);
const QColor kGreen(0x22, 0xC5, 0x5E);
const QColor kGreenDark(0x15, 0x80, 0x3D);
const QColor kRed(0xEF, 0x44, 0x44);
const QColor kRedDark(0xB9, 0x1C, 0x1C);
const QColor kToggleBg(24, 24, 36, 230);        // rgba(24,24,36,.9)
const QColor kText1(0xFA, 0xFA, 0xFA);
const QColor kText2(0xA1, 0xA1, 0xAA);

const char* kToggleQss = R"QSS(
QPushButton { border:1px solid rgba(255,255,255,0.12); border-radius:28px;
              background:rgba(24,24,36,0.9); }
QPushButton:hover { background:rgba(36,36,52,0.95); }
)QSS";
const char* kToggleActiveQss = R"QSS(
QPushButton { border:1px solid #9B72F8; border-radius:28px;
              background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9); }
)QSS";
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  CallMinimizedBar
// ─────────────────────────────────────────────────────────────────────────────
CallMinimizedBar::CallMinimizedBar(QWidget* parent) : QWidget(parent) {
    setFixedSize(308, 68);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "background:rgba(10,10,15,0.97); border:1px solid rgba(255,255,255,0.12);"
        "border-radius:24px;"));

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 10, 10, 10);
    lay->setSpacing(10);

    avatar_ = new QLabel(this);
    avatar_->setFixedSize(40, 40);
    avatar_->setAlignment(Qt::AlignCenter);
    name_ = new QLabel(this);
    name_->setStyleSheet(QStringLiteral(
        "color:#FAFAFA;font-size:14px;font-weight:600;background:transparent;"));
    timer_ = new QLabel(QStringLiteral("00:00"), this);
    timer_->setStyleSheet(QStringLiteral(
        "color:#22C55E;font-size:13px;font-weight:600;background:transparent;"));
    auto* col = new QVBoxLayout();
    col->setSpacing(1);
    col->addWidget(name_);
    col->addWidget(timer_);
    lay->addWidget(avatar_);
    lay->addLayout(col, 1);

    auto mk = [this](Icons::Kind kind, const QColor& color) {
        auto* b = new QPushButton(this);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(32, 32);
        b->setIcon(Icons::icon(kind, 16, color));
        b->setIconSize(QSize(16, 16));
        b->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;border-radius:16px;background:rgba(255,255,255,0.06);}"
            "QPushButton:hover{background:rgba(255,255,255,0.12);}"));
        return b;
    };
    auto* restore = mk(Icons::ChevronRight, kText1);
    auto* end = mk(Icons::Hangup, kRed);
    connect(restore, &QPushButton::clicked, this, &CallMinimizedBar::restoreRequested);
    connect(end, &QPushButton::clicked, this, &CallMinimizedBar::endRequested);
    lay->addWidget(restore);
    lay->addWidget(end);
    hide();
}

void CallMinimizedBar::setPeer(const QString& name, const QString& avatarUrl) {
    name_->setText(name);
    Avatar::setRound(avatar_, avatarUrl, name, 40);
}

void CallMinimizedBar::setTimerText(const QString& text) { timer_->setText(text); }

// ─────────────────────────────────────────────────────────────────────────────
//  CallOverlay
// ─────────────────────────────────────────────────────────────────────────────
CallOverlay::CallOverlay(QWidget* parent) : QWidget(parent) {
    setGeometry(parent->rect());
    parent->installEventFilter(this);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ── Шапка (56px): аватар, имя+статус, таймер, «свернуть» ────────────────
    auto* header = new QWidget(this);
    header->setFixedHeight(64);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 10, 14, 10);
    hl->setSpacing(12);
    headerAvatar_ = new QLabel(header);
    headerAvatar_->setFixedSize(44, 44);
    headerAvatar_->setAlignment(Qt::AlignCenter);
    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(1);
    headerName_ = new QLabel(header);
    headerName_->setStyleSheet(QStringLiteral(
        "color:#FAFAFA;font-size:17px;font-weight:700;background:transparent;"));
    headerStatus_ = new QLabel(header);
    headerStatus_->setStyleSheet(QStringLiteral(
        "color:#A1A1AA;font-size:13px;background:transparent;"));
    nameCol->addWidget(headerName_);
    nameCol->addWidget(headerStatus_);
    hl->addWidget(headerAvatar_);
    hl->addLayout(nameCol, 1);
    headerTimer_ = new QLabel(QStringLiteral("00:00"), header);
    headerTimer_->setStyleSheet(QStringLiteral(
        "color:#22C55E;font-size:16px;font-weight:600;background:transparent;"));
    hl->addWidget(headerTimer_);
    minimizeBtn_ = new QPushButton(header);
    minimizeBtn_->setCursor(Qt::PointingHandCursor);
    minimizeBtn_->setFixedSize(36, 36);
    minimizeBtn_->setIcon(Icons::icon(Icons::ArrowLeft, 16, kText1)); // «‹» = свернуть
    minimizeBtn_->setIconSize(QSize(16, 16));
    minimizeBtn_->setToolTip(QStringLiteral("Свернуть"));
    minimizeBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{border:1px solid rgba(255,255,255,0.12);border-radius:18px;"
        "background:rgba(24,24,36,0.9);}"
        "QPushButton:hover{background:rgba(36,36,52,0.95);}"));
    connect(minimizeBtn_, &QPushButton::clicked, this, &CallOverlay::minimizeRequested);
    hl->addWidget(minimizeBtn_);
    lay->addWidget(header);

    // ── Центр: заглушка с крупным аватаром (как .call-video-placeholder) ────
    lay->addStretch(3);
    bigAvatar_ = new QLabel(this);
    bigAvatar_->setFixedSize(160, 160);
    bigAvatar_->setAlignment(Qt::AlignCenter);
    bigName_ = new QLabel(this);
    bigName_->setAlignment(Qt::AlignHCenter);
    bigName_->setStyleSheet(QStringLiteral(
        "color:#FAFAFA;font-size:26px;font-weight:800;background:transparent;"));
    bigStatus_ = new QLabel(this);
    bigStatus_->setAlignment(Qt::AlignHCenter);
    bigStatus_->setStyleSheet(QStringLiteral(
        "color:#A1A1AA;font-size:15px;background:transparent;"));
    lay->addWidget(bigAvatar_, 0, Qt::AlignHCenter);
    lay->addSpacing(24);
    lay->addWidget(bigName_, 0, Qt::AlignHCenter);
    lay->addSpacing(6);
    lay->addWidget(bigStatus_, 0, Qt::AlignHCenter);
    lay->addStretch(4);

    // ── Кнопки состояний ─────────────────────────────────────────────────────
    auto circleBtn = [this](int size, const QColor& a, const QColor& b, Icons::Kind kind) {
        auto* btn = new QPushButton(this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(size, size);
        btn->setIcon(Icons::icon(kind, int(size * 0.38), QColor(0xFF, 0xFF, 0xFF)));
        btn->setIconSize(QSize(int(size * 0.38), int(size * 0.38)));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;border-radius:%1px;"
            "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %2,stop:1 %3);}"
            "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %4,stop:1 %3);}")
            .arg(size / 2).arg(a.name(), b.name(), a.lighter(112).name()));
        return btn;
    };
    auto labeled = [this](QPushButton* btn, const QString& label) {
        auto* wrap = new QWidget(this);
        auto* l = new QVBoxLayout(wrap);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(8);
        l->addWidget(btn, 0, Qt::AlignHCenter);
        auto* t = new QLabel(label, wrap);
        t->setAlignment(Qt::AlignHCenter);
        t->setStyleSheet(QStringLiteral(
            "color:#A1A1AA;font-size:13px;background:transparent;"));
        l->addWidget(t);
        return wrap;
    };

    // Исходящий: [Отменить].
    outgoingBtns_ = new QWidget(this);
    auto* ol = new QHBoxLayout(outgoingBtns_);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(24);
    auto* cancel = circleBtn(68, kRed, kRedDark, Icons::Hangup);
    connect(cancel, &QPushButton::clicked, this, &CallOverlay::hangup);
    ol->addStretch();
    ol->addWidget(labeled(cancel, QStringLiteral("Отменить")));
    ol->addStretch();

    // Входящий: [Принять] [Отклонить].
    incomingBtns_ = new QWidget(this);
    auto* il = new QHBoxLayout(incomingBtns_);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(24);
    auto* accept = circleBtn(68, kGreen, kGreenDark, Icons::Phone);
    auto* reject = circleBtn(68, kRed, kRedDark, Icons::Hangup);
    connect(accept, &QPushButton::clicked, this, &CallOverlay::accept);
    connect(reject, &QPushButton::clicked, this, &CallOverlay::decline);
    il->addStretch();
    il->addWidget(labeled(reject, QStringLiteral("Отклонить")));
    il->addWidget(labeled(accept, QStringLiteral("Принять")));
    il->addStretch();

    // Активный: [Микрофон][Звук] | [Завершить] (64×48, radius 24).
    activeBtns_ = new QWidget(this);
    auto* al = new QHBoxLayout(activeBtns_);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(16);
    micBtn_ = makeToggle(QStringLiteral("Микрофон"));
    connect(micBtn_, &QPushButton::clicked, this, [this]() {
        setMuted(!muted_);
        emit muteToggled(muted_);
    });
    // Шумодав (CAL-04): A/B на живом звонке — фоновый гул уходит.
    nrBtn_ = makeToggle(QStringLiteral("Шумодав"));
    const bool nrOn = Prefs::getBool(QStringLiteral("xipher_call_noise_suppression"), true);
    nrBtn_->setStyleSheet(nrOn ? QLatin1String(kToggleActiveQss) : QLatin1String(kToggleQss));
    connect(nrBtn_, &QPushButton::clicked, this, [this]() {
        const bool on = nrBtn_->styleSheet() == QLatin1String(kToggleActiveQss);
        nrBtn_->setStyleSheet(on ? QLatin1String(kToggleQss) : QLatin1String(kToggleActiveQss));
        emit noiseSuppressionToggled(!on);
    });
    spkBtn_ = makeToggle(QStringLiteral("Звук"));
    spkBtn_->setStyleSheet(QLatin1String(kToggleActiveQss));   // звук по умолчанию вкл
    connect(spkBtn_, &QPushButton::clicked, this, [this]() {
        setDeaf(!deaf_);
        emit deafToggled(deaf_);
    });
    auto* end = new QPushButton(this);
    end->setCursor(Qt::PointingHandCursor);
    end->setFixedSize(64, 48);
    end->setIcon(Icons::icon(Icons::Hangup, 22, QColor(0xFF, 0xFF, 0xFF)));
    end->setIconSize(QSize(22, 22));
    end->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:24px;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #EF4444,stop:1 #B91C1C);}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #F87171,stop:1 #B91C1C);}"));
    connect(end, &QPushButton::clicked, this, &CallOverlay::hangup);
    al->addStretch();
    al->addWidget(micBtn_);
    al->addWidget(nrBtn_);
    al->addWidget(spkBtn_);
    al->addSpacing(12);
    al->addWidget(end);
    al->addStretch();

    auto* btnsWrap = new QWidget(this);
    auto* bwl = new QVBoxLayout(btnsWrap);
    bwl->setContentsMargins(0, 0, 0, 36);
    bwl->setSpacing(12);
    bwl->addWidget(outgoingBtns_);
    bwl->addWidget(incomingBtns_);
    bwl->addWidget(activeBtns_);
    lay->addWidget(btnsWrap);

    miniBar_ = new CallMinimizedBar(window());
    connect(miniBar_, &CallMinimizedBar::restoreRequested, this, [this]() {
        show();
        raise();
        miniBar_->hide();
        emit restoreRequested();
    });
    connect(miniBar_, &CallMinimizedBar::endRequested, this, &CallOverlay::hangup);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, [this]() {
        ++secs_;
        const QString t = QStringLiteral("%1:%2")
            .arg(secs_ / 60, 2, 10, QLatin1Char('0'))
            .arg(secs_ % 60, 2, 10, QLatin1Char('0'));
        setTimerText(t);
    });

    setState(State::Outgoing);
}

QPushButton* CallOverlay::makeToggle(const QString& tooltip) {
    auto* b = new QPushButton(this);
    b->setCursor(Qt::PointingHandCursor);
    b->setFixedSize(56, 56);
    b->setIconSize(QSize(22, 22));
    b->setToolTip(tooltip);
    b->setStyleSheet(QLatin1String(kToggleQss));
    return b;
}

void CallOverlay::setPeer(const QString& name, const QString& avatarUrl) {
    headerName_->setText(name);
    bigName_->setText(name);
    Avatar::setRound(headerAvatar_, avatarUrl, name, 44);
    Avatar::setRound(bigAvatar_, avatarUrl, name, 160);
    miniBar_->setPeer(name, avatarUrl);
}

void CallOverlay::setState(State st) {
    state_ = st;
    applyState();
}

void CallOverlay::applyState() {
    outgoingBtns_->setVisible(state_ == State::Outgoing);
    incomingBtns_->setVisible(state_ == State::Incoming);
    activeBtns_->setVisible(state_ == State::Active);
    const QString p = QStringLiteral("Голосовой звонок");
    QString s;
    switch (state_) {
    case State::Outgoing: s = QStringLiteral("Исходящий %1...").arg(p.toLower()); break;
    case State::Incoming: s = QStringLiteral("Входящий %1").arg(p.toLower()); break;
    case State::Active:   s = p; break;
    }
    headerStatus_->setText(s);
    bigStatus_->setText(s);
    // Таймер в шапке горит только в активном звонке (в вебе — то же самое).
    headerTimer_->setVisible(state_ == State::Active);
    if (state_ != State::Active) { secs_ = 0; setTimerText(QStringLiteral("00:00")); }
}

void CallOverlay::setStatusHint(const QString& text) {
    bigStatus_->setText(text);
    headerStatus_->setText(text);
}

void CallOverlay::setTimerText(const QString& text) {
    headerTimer_->setText(text);
    miniBar_->setTimerText(text);
}

void CallOverlay::startCallTimer() {
    secs_ = 0;
    setTimerText(QStringLiteral("00:00"));
    timer_->start();
}

void CallOverlay::setMuted(bool muted) {
    muted_ = muted;
    micBtn_->setIcon(Icons::icon(Icons::Mic, 22,
        muted ? QColor(0xE2, 0x6A, 0x63) : QColor(0xFF, 0xFF, 0xFF)));
    // Выключенный микрофон — состояние «выкл»: тёмная плитка без подсветки.
    micBtn_->setStyleSheet(muted ? QLatin1String(kToggleQss)
                                  : QLatin1String(kToggleActiveQss));
}

void CallOverlay::setDeaf(bool deaf) {
    deaf_ = deaf;
    spkBtn_->setIcon(Icons::icon(Icons::Speaker, 22,
        deaf ? QColor(0xE2, 0x6A, 0x63) : QColor(0xFF, 0xFF, 0xFF)));
    spkBtn_->setStyleSheet(deaf ? QLatin1String(kToggleQss)
                                : QLatin1String(kToggleActiveQss));
}

void CallOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // 145°: #050508 → #0d0515 (35%) → #0a0810 (70%) → #050508 (.call-modal-overlay).
    QLinearGradient g(0, 0, width(), height());
    g.setColorAt(0.0, kBgA);
    g.setColorAt(0.35, kBgB);
    g.setColorAt(0.70, kBgC);
    g.setColorAt(1.0, kBgA);
    p.fillRect(rect(), g);
}

bool CallOverlay::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize && isVisible())
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(obj, e);
}
