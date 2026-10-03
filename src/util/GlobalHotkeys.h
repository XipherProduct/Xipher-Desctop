#pragma once
#include <QThread>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  GlobalHotkeys — глобальные сочетания вне окна приложения (CAL-05/DSC-02):
//  X11 XGrabKey на своём Display* в отдельном потоке: PTT (push-to-talk),
//  mute/deaf/accept. Работает в ЛЮБОМ приложении. Не-X11 (Windows) — заглушка:
//  isAvailable()=false, вызовы тихо неактивны.
//  Бинды в Prefs: xipher_bind_ptt / mute / deaf / accept (Ctrl+Alt+T по умолч.).
// ─────────────────────────────────────────────────────────────────────────────
class GlobalHotkeys : public QObject {
    Q_OBJECT
public:
    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    static bool isAvailable();          // X11 и удалось открыть дисплей
    bool start();                       // захватить бинды из Prefs
    void stop();
    bool running() const;

    // Разбор/сборка «Ctrl+Alt+T» ↔ X11 modifiers+keysym (для тестов).
    static bool parseBinding(const QString& text, unsigned& mods, unsigned& keysym);
    static QString defaultBinding(const QString& action);

signals:
    void pttPressed();
    void pttReleased();
    void muteToggled();
    void deafToggled();
    void acceptCall();

private:
    void* thread_ = nullptr;   // GrabThread*
};
