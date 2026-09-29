#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class QHBoxLayout;

// ─────────────────────────────────────────────────────────────────────────────
//  CallMinimizedBar — плавающая плашка свёрнутого звонка (#minimizedCall веба):
//  правый нижний угол, аватар + имя + таймер, кнопки «развернуть» и «завершить».
// ─────────────────────────────────────────────────────────────────────────────
class CallMinimizedBar : public QWidget {
    Q_OBJECT
public:
    explicit CallMinimizedBar(QWidget* parent);
    void setPeer(const QString& name, const QString& avatarUrl);
    void setTimerText(const QString& text);
signals:
    void restoreRequested();
    void endRequested();
private:
    QLabel* avatar_;
    QLabel* name_;
    QLabel* timer_;
};

// ─────────────────────────────────────────────────────────────────────────────
//  CallOverlay — экран звонка 1:1 с веб-клиентом (css/calls.css):
//  полноэкранный градиент #050508→#0d0515, шапка (аватар/имя/статус/таймер/
//  сворачивание), центральная заглушка с аватаром 160px, кнопки состояний:
//    исходящий — [Отменить]; входящий — [Принять][Отклонить];
//    активный  — [Микрофон][Звук] | [Завершить].
//  Тумблеры 56px: тёмные в покое, фиолетовый градиент с glow во «вкл».
//  Сворачивание прячет экран и показывает CallMinimizedBar.
// ─────────────────────────────────────────────────────────────────────────────
class CallOverlay : public QWidget {
    Q_OBJECT
public:
    enum class State { Outgoing, Incoming, Active };

    explicit CallOverlay(QWidget* parent);

    void setPeer(const QString& name, const QString& avatarUrl);
    void setState(State st);
    // Подпись поверх шаблонной («Соединение…» на установке ответа).
    void setStatusHint(const QString& text);
    void setTimerText(const QString& text);   // «00:12» из контроллера
    void startCallTimer();
    void setMuted(bool muted);                // вид кнопки из контроллера
    void setDeaf(bool deaf);
    CallMinimizedBar* minimizedBar() const { return miniBar_; }

signals:
    void hangup();        // отменить/отклонить/завершить — решает контроллер
    void accept();
    void decline();
    void muteToggled(bool muted);
    void deafToggled(bool deaf);
    void minimizeRequested();
    void restoreRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void applyState();
    QPushButton* makeToggle(const QString& tooltip);

    QLabel*      headerAvatar_;
    QLabel*      headerName_;
    QLabel*      headerStatus_;
    QLabel*      headerTimer_;
    QPushButton* minimizeBtn_;

    QLabel*      bigAvatar_;
    QLabel*      bigName_;
    QLabel*      bigStatus_;

    QWidget*     outgoingBtns_;
    QWidget*     incomingBtns_;
    QWidget*     activeBtns_;
    QPushButton* micBtn_;
    QPushButton* spkBtn_;

    CallMinimizedBar* miniBar_ = nullptr;
    QTimer*      timer_;
    int          secs_ = 0;
    State        state_ = State::Outgoing;
    bool         muted_ = false;
    bool         deaf_  = false;
};
