#include "util/GlobalHotkeys.h"
#include "net/Prefs.h"

#include <QKeySequence>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <QList>

// Поток владеет своим Display*: XGrabKey глобален на сервере — события
// приходят только нашему соединению, XNextEvent не мешает GUI-треду.
class GrabThread : public QThread {
    Q_OBJECT
public:
    GrabThread(GlobalHotkeys* owner) : owner_(owner) {}

    struct Binding {
        unsigned mods = 0;
        unsigned keysym = 0;
        int action = 0;   // 0 ptt, 1 mute, 2 deaf, 3 accept
    };
    QList<Binding> bindings;

    void run() override {
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) return;
        const Window root = DefaultRootWindow(dpy_);
        // События нажатия/отпускания по grab-клавишам.
        for (const Binding& b : bindings)
            if (b.keysym)
                XGrabKey(dpy_, XKeysymToKeycode(dpy_, b.keysym), b.mods, root,
                         False, GrabModeAsync, GrabModeAsync);
        XSync(dpy_, False);
        running_ = true;
        XEvent ev;
        while (running_ && !stopFlag) {
            // XPending с маленьким таймаутом через select — чтобы поток можно было остановить.
            if (XPending(dpy_) > 0) {
                XNextEvent(dpy_, &ev);
                if (ev.type == KeyPress || ev.type == KeyRelease) {
                    const unsigned ks = XkbKeycodeToKeysym(dpy_, ev.xkey.keycode, 0, 0);
                    for (const Binding& b : bindings)
                        if (b.keysym == ks && b.mods == ev.xkey.state) {
                            if (b.action == 0) {
                                if (ev.type == KeyPress) emit pttPressed();
                                else emit pttReleased();
                            } else if (ev.type == KeyPress && b.action == 1) {
                                emit muteToggled();
                            } else if (ev.type == KeyPress && b.action == 2) {
                                emit deafToggled();
                            } else if (ev.type == KeyPress && b.action == 3) {
                                emit acceptCall();
                            }
                        }
                }
            } else {
                usleep(20000);   // 20 мс — не жжём CPU
            }
        }
        for (const Binding& b : bindings)
            if (b.keysym)
                XUngrabKey(dpy_, XKeysymToKeycode(dpy_, b.keysym), b.mods, root);
        XSync(dpy_, False);
        XCloseDisplay(dpy_);
        dpy_ = nullptr;
        running_ = false;
    }

    void stopSoon() { stopFlag = 1; }

    std::atomic<int> stopFlag{0};
    std::atomic<bool> running_{false};
    Display* dpy_ = nullptr;
    GlobalHotkeys* owner_;
signals:
    void pttPressed();
    void pttReleased();
    void muteToggled();
    void deafToggled();
    void acceptCall();
};
#endif

GlobalHotkeys::GlobalHotkeys(QObject* parent) : QObject(parent) {}

GlobalHotkeys::~GlobalHotkeys() { stop(); }

bool GlobalHotkeys::isAvailable() {
#ifdef Q_OS_UNIX
    return qEnvironmentVariable("DISPLAY") != nullptr
        || qEnvironmentVariable("WAYLAND_DISPLAY") != nullptr;   // XWayland
#else
    return false;
#endif
}

QString GlobalHotkeys::defaultBinding(const QString& action) {
    if (action == QLatin1String("ptt")) return QStringLiteral("Ctrl+Alt+T");
    if (action == QLatin1String("mute")) return QStringLiteral("Ctrl+Alt+M");
    if (action == QLatin1String("deaf")) return QStringLiteral("Ctrl+Alt+D");
    if (action == QLatin1String("accept")) return QStringLiteral("Ctrl+Alt+A");
    return QString();
}

// «Ctrl+Alt+T» → (ShiftMask|ControlMask|Mod1Mask, XK_t).
bool GlobalHotkeys::parseBinding(const QString& text, unsigned& mods, unsigned& keysym) {
#ifdef Q_OS_UNIX
    mods = 0;
    const QKeySequence seq(text);
    if (seq.count() != 1) return false;
    const int combo = seq[0].toCombined();
    if (combo & Qt::ShiftModifier) mods |= ShiftMask;
    if (combo & Qt::ControlModifier) mods |= ControlMask;
    if (combo & Qt::AltModifier) mods |= Mod1Mask;
    if (combo & Qt::MetaModifier) mods |= Mod4Mask;
    const int key = combo & ~Qt::KeyboardModifierMask;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        keysym = XK_a + (key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        keysym = XK_0 + (key - Qt::Key_0);
    else if (key == Qt::Key_Space)
        keysym = XK_space;
    else
        return false;
    return mods != 0;
#else
    Q_UNUSED(text); Q_UNUSED(mods); Q_UNUSED(keysym);
    return false;
#endif
}

bool GlobalHotkeys::start() {
#ifdef Q_OS_UNIX
    if (thread_) stop();
    auto* t = new GrabThread(this);
    struct { const char* pref; int action; } defs[] = {
        {"xipher_bind_ptt", 0}, {"xipher_bind_mute", 1},
        {"xipher_bind_deaf", 2}, {"xipher_bind_accept", 3},
    };
    const char* names[] = {"ptt", "mute", "deaf", "accept"};
    for (auto& d : defs) {
        const QString text = Prefs::getStr(QLatin1String(d.pref),
                                           defaultBinding(QLatin1String(names[d.action])));
        unsigned mods = 0, ks = 0;
        if (parseBinding(text, mods, ks))
            t->bindings.append({mods, ks, d.action});
    }
    if (t->bindings.isEmpty()) { delete t; return false; }
    connect(t, &GrabThread::pttPressed, this, &GlobalHotkeys::pttPressed);
    connect(t, &GrabThread::pttReleased, this, &GlobalHotkeys::pttReleased);
    connect(t, &GrabThread::muteToggled, this, &GlobalHotkeys::muteToggled);
    connect(t, &GrabThread::deafToggled, this, &GlobalHotkeys::deafToggled);
    connect(t, &GrabThread::acceptCall, this, &GlobalHotkeys::acceptCall);
    t->start();
    thread_ = t;
    return true;
#else
    return false;
#endif
}

void GlobalHotkeys::stop() {
#ifdef Q_OS_UNIX
    if (auto* t = static_cast<GrabThread*>(thread_)) {
        t->stopSoon();
        t->wait(1500);
        delete t;
        thread_ = nullptr;
    }
#endif
}

bool GlobalHotkeys::running() const {
#ifdef Q_OS_UNIX
    return thread_ != nullptr;
#else
    return false;
#endif
}

#include "GlobalHotkeys.moc"
