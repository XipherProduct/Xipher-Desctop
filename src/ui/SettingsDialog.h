#pragma once
#include "ui/ModalOverlay.h"
#include "net/Models.h"

#include <QList>
#include <QSet>
#include <QColor>

class ApiClient;
class QButtonGroup;
class QPushButton;
class QTimer;
class QGridLayout;
class QListWidget;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QVBoxLayout;
class QJsonObject;

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsDialog — панель настроек 1:1 с веб-клиентом (css/settings.css):
//  1060px, topbar с ⋮-меню, слева nav 256px (#08070b) с группами
//  Аккаунт/Общее/Безопасность/Xipher, справа контент с секциями-колонками 680px.
//  Аккаунт/приватность/сеансы/email ходят на сервер; уведомления/звонки/язык/
//  блокировки — локально (Prefs).
// ─────────────────────────────────────────────────────────────────────────────
class SettingsDialog : public ModalOverlay {
    Q_OBJECT
public:
    SettingsDialog(ApiClient* api, QWidget* parent);

signals:
    void logoutRequested();   // из ⋮-меню (как settingsLogoutBtn в вебе)
    void themeChanged();      // смена темы «Оформление» — мгновенное применение

private:
    void buildChrome();
    void navGroup(const QString& title);
    void addSection(const QString& title, int iconKind, QWidget* page);
    QWidget* wrapPage(QWidget* content);   // scroll + колонка 680px по центру
    void pickAvatar();

    QWidget* buildAccountPage();
    QWidget* buildNotificationsPage();
    QWidget* buildPrivacyPage();
    QWidget* buildCallsPage();
    QWidget* buildSessionsPage();
    QWidget* buildBlockedPage();
    QWidget* buildLanguagePage();
    QWidget* buildEmailPage();
    QWidget* buildPremiumPage();
    QWidget* buildAboutPage();
    QWidget* buildExportPage();
    QWidget* buildAppearancePage();
    void rebuildAppearanceGallery();
    // Xipher Pulse
    void updatePulseCta();
    void refreshPulseMeters();
    void showPulsePending();
    void startPulsePolling();
    void stopPulsePolling();
    void openPulsePayment();
    void relayoutForWidth(int w);   // адаптация узкой панели
    void relayoutGrids(int w);      // перестройка сеток тарифов/перков/тем

    void onProfileLoaded(const QJsonObject& obj);
    void refreshSessions();

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    ApiClient*      api_;
    QStackedWidget* stack_ = nullptr;
    QButtonGroup*   navBtnGroup_ = nullptr;
    QVBoxLayout*    navLayout_   = nullptr;
    int             accountIdx_  = 0;
    int             appearanceIdx_ = -1;

    // Аккаунт.
    QLabel*         avatar_    = nullptr;
    QLabel*         heroName_  = nullptr;
    QLabel*         heroUser_  = nullptr;
    QLineEdit*      firstName_ = nullptr;
    QLineEdit*      lastName_  = nullptr;
    QPlainTextEdit* bio_       = nullptr;
    QSpinBox*       bDay_      = nullptr;
    QSpinBox*       bMonth_    = nullptr;
    QSpinBox*       bYear_     = nullptr;
    QLineEdit*      usernameRO_= nullptr;
    QString         avatarUrl_;

    // Email.
    QLineEdit*      email_     = nullptr;

    // Xipher Pulse.
    QLabel*      statusChip_ = nullptr;
    QLabel*      expiryChip_ = nullptr;
    QLabel*      paymentChip_ = nullptr;
    QPushButton* subscribeBtn_ = nullptr;
    QPushButton* manageBtn_ = nullptr;
    QLabel*      meterLimit_ = nullptr;
    QLabel*      meterUsed_ = nullptr;
    QLabel*      meterFree_ = nullptr;
    QTimer*      pollTimer_ = nullptr;
    QTimer*      blinkTimer_ = nullptr;
    QString      planSel_;
    QString      pendingLabel_;

    // Адаптация под узкое окно.
    QWidget*     navWidget_   = nullptr;
    QPushButton* sectionsBtn_ = nullptr;
    QGridLayout* plansGrid_   = nullptr;
    QGridLayout* perksGrid_   = nullptr;
    QGridLayout* themeGrid_   = nullptr;
    int          lastLayoutW_ = 0;

    // Сеансы (текущий + другие, мультивыбор).
    QVBoxLayout*    currentSessionBox_ = nullptr;
    QVBoxLayout*    othersBox_ = nullptr;
    QPushButton*    endSelectedBtn_ = nullptr;
    QSet<QString>   selectedSessions_;

    // Privacy controls собираются в лямбдах; сохранение через updateMyPrivacy.
};
