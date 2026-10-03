#include "app/MainWindow.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Session.h"
#include "net/ChatCache.h"
#include "net/FileCache.h"
#include "ui/LoginPage.h"
#include "ui/RegisterPage.h"
#include "ui/ChatPage.h"
#include "ui/BackgroundWidget.h"
#include "ui/Theme.h"
#include "net/CallController.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QMenu>
#include <QCloseEvent>
#include "net/Prefs.h"
#include "util/Autostart.h"
#include "util/Accounts.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
// Тёмный нативный заголовок окна под цвет приложения (Windows 10 2004+ / 11).
static void applyDarkTitleBar(WId winId) {
    HWND hwnd = reinterpret_cast<HWND>(winId);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF caption = RGB(0x13, 0x12, 0x18);   // #131218 — как сайдбар/шапки
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    COLORREF text = RGB(0xF3, 0xF1, 0xF8);
    DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof(text));
    COLORREF border = RGB(0x13, 0x12, 0x18);
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
}
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Xipher"));
    resize(1100, 760);
    setMinimumSize(720, 600);

#ifdef _WIN32
    applyDarkTitleBar(winId());   // winId() создаёт нативный хэндл
#endif

    api_ = new ApiClient(this);
    ws_  = new WsClient(this);

    stack_    = new QStackedWidget(this);
    splash_   = buildSplash();
    login_    = new LoginPage(api_, this);
    register_ = new RegisterPage(api_, this);
    chat_     = new ChatPage(api_, ws_, this);

    stack_->addWidget(splash_);    // PageSplash
    stack_->addWidget(login_);     // PageLogin
    stack_->addWidget(register_);  // PageRegister
    stack_->addWidget(chat_);      // PageChat
    setCentralWidget(stack_);

    // Навигация между вход ↔ регистрация
    connect(login_, &LoginPage::goToRegister, this, [this]() {
        stack_->setCurrentIndex(PageRegister);
    });
    connect(register_, &RegisterPage::goToLogin, this, [this]() {
        stack_->setCurrentIndex(PageLogin);
    });

    // Успешная регистрация → как в вебе, уводим на экран входа.
    connect(register_, &RegisterPage::registeredOk, this, [this](const QString&) {
        stack_->setCurrentIndex(PageLogin);
    });

    // Успешный вход → мессенджер (+профиль в реестр мультиаккаунта, DSC-04).
    connect(login_, &LoginPage::loginSucceeded, this, [this]() {
        rememberCurrentProfile();
        enterChat();
    });
    // DSC-04: меню приложения просит переключить/добавить аккаунт.
    connect(chat_, &ChatPage::switchAccountRequested, this,
            [this](const QString& userId) { switchToAccount(userId); });
    connect(chat_, &ChatPage::addAccountRequested, this, [this]() {
        // Новый аккаунт: не выкидывая старый (он в реестре) — вход заново.
        ws_->stop();
        if (callPoll_) callPoll_->stop();
        Session::instance().clear();   // токен не удаляем из реестра
        stack_->setCurrentIndex(PageLogin);
    });

    // Трей (WIN-06): иконка + меню (открыть/опции/выход), клик — разворот,
    // опция «закрывать в трей» — закрытие окна прячет приложение.
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(this);
        tray_->setIcon(windowIcon().isNull() ? style()->standardIcon(QStyle::SP_MessageBoxInformation)
                                             : windowIcon());
        tray_->setToolTip(QStringLiteral("Xipher"));
        auto* trayMenu = new QMenu(this);
        auto* openAct = trayMenu->addAction(QStringLiteral("Открыть Xipher"));
        connect(openAct, &QAction::triggered, this, [this]() { activateWindowFromTray(); });
        auto* trayClose = trayMenu->addAction(QStringLiteral("Закрывать окно в трей"));
        trayClose->setCheckable(true);
        trayClose->setChecked(Prefs::getBool(QStringLiteral("xipher_close_to_tray"), false));
        connect(trayClose, &QAction::toggled, this, [](bool on) {
            Prefs::setBool(QStringLiteral("xipher_close_to_tray"), on);
        });
        auto* autoAct = trayMenu->addAction(QStringLiteral("Запускать с системой"));
        autoAct->setCheckable(true);
        autoAct->setChecked(Autostart::isOn());
        connect(autoAct, &QAction::toggled, this, [](bool on) { Autostart::set(on); });
        trayMenu->addSeparator();
        auto* quitAct = trayMenu->addAction(QStringLiteral("Выход"));
        connect(quitAct, &QAction::triggered, qApp, []() {
            Prefs::setBool(QStringLiteral("__quitting"), true);
            qApp->quit();
        });
        tray_->setContextMenu(trayMenu);
        connect(tray_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
                activateWindowFromTray();
        });
        tray_->show();
    }
    connect(chat_, &ChatPage::notify, this, [this](const QString& title, const QString& body) {
        if (!tray_ || !Prefs::getBool(QStringLiteral("xipher_notif_desktop"), true)) return;
        if (isActiveWindow()) return;   // окно в фокусе — не отвлекаем
        tray_->showMessage(title, body, QSystemTrayIcon::NoIcon, 5000);
    });

    // Звонки: оркестратор + поллинг входящих.
    callCtl_ = new CallController(api_, ws_, this, this);
    connect(chat_, &ChatPage::callRequested, this,
            [this](const QString& id, const QString& name, const QString& avatar) {
        callCtl_->startOutgoing(id, name, avatar);
    });
    connect(api_, &ApiClient::incomingCall, this,
            [this](const QString& id, const QString& name, const QString& type) {
        callCtl_->onIncoming(id, name, type);
    });
    // Бейдж пропущенных звонков: при старте, после call_missed и раз в минуту.
    connect(api_, &ApiClient::callsMissedLoaded, this, [this](int count) {
        setProperty("missedCalls", count);
        chat_->setProperty("missedCalls", count);
        const QString tip = count > 0
            ? QStringLiteral("Xipher — пропущенных звонков: %1").arg(count)
            : QStringLiteral("Xipher");
        if (tray_) tray_->setToolTip(tip);
    });
    api_->callsMissedCount();

    callPoll_ = new QTimer(this);
    callPoll_->setInterval(2500);
    connect(callPoll_, &QTimer::timeout, this, [this]() {
        if (Session::instance().isAuthenticated()) api_->checkIncomingCalls();
    });

    // Выход → останавливаем realtime/поллинг, чистим сессию и локальные
    // зашифрованные кэши (истории + медиа), назад на вход.
    connect(chat_, &ChatPage::logoutRequested, this, [this]() {
        ws_->stop();
        if (callPoll_) callPoll_->stop();
        ChatCache::instance().clearAll();
        FileCache::instance().clearAll();
        Session::instance().clear();
        stack_->setCurrentIndex(PageLogin);
    });

    // Восстановление сессии по сохранённому токену (+профиль в реестр).
    connect(api_, &ApiClient::tokenValidated, this, [this](const AuthResult& r) {
        if (r.success) {
            Session& s = Session::instance();
            if (!r.userId.isEmpty())   s.userId = r.userId;
            if (!r.username.isEmpty()) s.username = r.username;
            s.isPremium = r.isPremium;
            s.premiumPlan = r.premiumPlan;
            s.premiumExpiresAt = r.premiumExpiresAt;
            s.save();
            rememberCurrentProfile();
            enterChat();
        } else {
            // Токен текущего профиля умер: выкинуть из реестра, есть другие —
            // предложить переключиться, иначе экран входа (DSC-04).
            Accounts::remove(Accounts::activeId());
            if (!Accounts::all().isEmpty()) {
                switchToAccount(Accounts::all().first().userId);
                return;
            }
            Session::instance().clear();
            stack_->setCurrentIndex(PageLogin);
        }
    });

    // Минимальный размер окна: ниже раскладка не сжимается, а перестраивается.
    setMinimumSize(680, 480);
    resize(1280, 800);
    restoreGeometryFromPrefs();   // WIN-08: прошлая геометрия окна
    tryRestoreSession();
}

QWidget* MainWindow::buildSplash() {
    // Нейтральный экран загрузки (как у Telegram при старте), чтобы при
    // автологине не мелькало окно входа.
    auto* page = new BackgroundWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addStretch();

    auto* icon = new QLabel(QStringLiteral("X"), page);
    icon->setObjectName(QStringLiteral("brandIcon"));
    icon->setFixedSize(64, 64);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("font-size:30px;"));

    auto* name = new QLabel(QStringLiteral("Xipher"), page);
    name->setObjectName(QStringLiteral("brandName"));
    name->setStyleSheet(QStringLiteral("font-size:24px;"));
    name->setAlignment(Qt::AlignHCenter);

    auto* loading = new QLabel(QStringLiteral("Загрузка…"), page);
    loading->setStyleSheet(QStringLiteral("color:#9aa4c6;font-size:14px;"));
    loading->setAlignment(Qt::AlignHCenter);

    lay->addWidget(icon, 0, Qt::AlignHCenter);
    lay->addSpacing(16);
    lay->addWidget(name);
    lay->addSpacing(8);
    lay->addWidget(loading);
    lay->addStretch();
    return page;
}

void MainWindow::enterChat() {
    stack_->setCurrentIndex(PageChat);
    chat_->load();   // грузим чаты + запускаем realtime
    if (callPoll_) callPoll_->start();   // следим за входящими звонками
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // WIN-06: опция «закрывать в трей» — окно прячется, приложение живёт.
    if (tray_ && Prefs::getBool(QStringLiteral("xipher_close_to_tray"), false)
        && !property("forceQuit").toBool()) {
        hide();
        e->ignore();
        return;
    }
    saveGeometryToPrefs();
    QMainWindow::closeEvent(e);
}

void MainWindow::saveGeometryForTest() { saveGeometryToPrefs(); }
bool MainWindow::restoreGeometryForTest() { return restoreGeometryFromPrefs(); }

void MainWindow::saveGeometryToPrefs() {
    Prefs::store().setValue(QStringLiteral("xipher_window_geometry"), saveGeometry());
}

bool MainWindow::restoreGeometryFromPrefs() {
    const QByteArray geo = Prefs::store()
        .value(QStringLiteral("xipher_window_geometry")).toByteArray();
    return !geo.isEmpty() && restoreGeometry(geo);
}

void MainWindow::activateWindowFromTray() {
    showNormal();
    raise();
    activateWindow();
}

// ── Мультиаккаунт (DSC-04) ─────────────────────────────────────────────────────

void MainWindow::rememberCurrentProfile() {
    const Session& s = Session::instance();
    if (!s.isAuthenticated()) return;
    Accounts::Profile p;
    p.userId = s.userId;
    p.username = s.username;
    p.token = s.token;
    Accounts::upsertActive(p);
}

// Переключение: сессионные кэши привязаны к пользователю — чистим, как при
// выходе, затем валидируем токен нового профиля (роутинг сделает tokenValidated).
void MainWindow::switchToAccount(const QString& userId) {
    for (const Accounts::Profile& p : Accounts::all()) {
        if (p.userId != userId) continue;
        ws_->stop();
        if (callPoll_) callPoll_->stop();
        ChatCache::instance().clearAll();
        FileCache::instance().clearAll();
        Session::instance().clear();
        Session& s = Session::instance();
        s.token = p.token;
        s.userId = p.userId;
        s.username = p.username;
        s.save();
        Prefs::setStr(QStringLiteral("xipher_active_account"), p.userId);
        stack_->setCurrentIndex(PageSplash);
        api_->validateToken(p.token);
        return;
    }
}

QList<Accounts::Profile> MainWindow::otherProfiles() const {
    QList<Accounts::Profile> out;
    const QString cur = Accounts::activeId();
    for (const Accounts::Profile& p : Accounts::all())
        if (p.userId != cur) out.append(p);
    return out;
}

void MainWindow::tryRestoreSession() {
    Session& s = Session::instance();
    s.load();
    if (s.isAuthenticated()) {
        // Есть токен — показываем загрузку и проверяем его в фоне.
        // Окно входа НЕ мелькает: роутинг сделает tokenValidated.
        stack_->setCurrentIndex(PageSplash);
        api_->validateToken(s.token);
    } else {
        stack_->setCurrentIndex(PageLogin);
    }
}
