// profile-live — рендер профиля на НАСТОЯЩИХ данных: реальный токен из
// QSettings, реальный /api/profile/view, тот же ProfilePanel, что в окне
// приложения. Пишет /tmp/profile-live.png и дампит ответ сервера: видно
// ровно то, что видит пользователь, без ручных скриншотов.
#include <QApplication>
#include <QTimer>
#include <QWidget>
#include <QJsonDocument>
#include <QLabel>
#include <cstdio>

#include "net/ApiClient.h"
#include "net/Session.h"
#include "ui/ProfilePanel.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Desktop"));
    app.setOrganizationName(QStringLiteral("Xipher"));

    Session::instance().load();
    if (Session::instance().token.isEmpty()) {
        fprintf(stderr, "нет сохранённой сессии — сначала войди в приложении\n");
        return 2;
    }
    fprintf(stderr, "user=%s id=%s\n",
            Session::instance().username.toUtf8().constData(),
            Session::instance().userId.toUtf8().constData());

    QWidget host;
    host.resize(1400, 1000);
    host.show();

    ApiClient api;
    api.setBaseUrl(QStringLiteral("https://messenger.xipher.pro"));

    ProfilePanel prof(&api, &host);
    QObject::connect(&api, &ApiClient::profileViewLoaded,
                     &app, [](qint64, const QJsonObject& data, bool ok, const QString& err) {
        if (!ok || data.isEmpty()) {
            fprintf(stderr, "[profile/view] ошибка: %s\n", err.toUtf8().constData());
            return;
        }
        const QJsonObject p = data.value(QStringLiteral("profile")).toObject();
        fprintf(stderr, "[profile/view] display=%s username=%s\n            avatar_url=%s\n            banner_url=%s personal_color=%s\n            is_online=%s last_seen=%s\n",
                p.value(QStringLiteral("display_name")).toString().toUtf8().constData(),
                p.value(QStringLiteral("username")).toString().toUtf8().constData(),
                p.value(QStringLiteral("avatar_url")).toString().toUtf8().constData(),
                p.value(QStringLiteral("banner_url")).toString().toUtf8().constData(),
                p.value(QStringLiteral("personal_color")).toString().toUtf8().constData(),
                p.value(QStringLiteral("is_online")).toBool() ? "yes" : "no",
                p.value(QStringLiteral("last_seen")).toString().toUtf8().constData());
        fprintf(stderr, "relation=%s\n",
                QJsonDocument(data.value(QStringLiteral("relation")).toObject())
                    .toJson(QJsonDocument::Compact).constData());
        fprintf(stderr, "gifts=%s\n",
                QJsonDocument(data.value(QStringLiteral("gifts")).toObject())
                    .toJson(QJsonDocument::Compact).constData());
    });
    QObject::connect(&api, &ApiClient::mediaCountLoaded, &app,
                     [](const QString& id, int total) {
        fprintf(stderr, "[media/list] %s → %d\n", id.toUtf8().constData(), total);
    });

    // Себя открываем: у пользователя именно свой профиль под рукой.
    prof.openFor(Session::instance().userId);

    // Есть ли секция «Подарки» в отрисованном профиле.
    QTimer::singleShot(3000, &app, [&]() {
        int giftLabels = 0;
        for (QLabel* l : prof.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Подарки")) ++giftLabels;
        fprintf(stderr, "[widgets] меток «Подарки»: %d\n", giftLabels);
        int giftCards = 0;
        for (QWidget* w : prof.findChildren<QWidget*>())
            if (w->objectName() == QStringLiteral("profGift")) ++giftCards;
        fprintf(stderr, "[widgets] карточек подарков: %d\n", giftCards);
    });
    int shots = 0;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &app, [&]() {
        const QPixmap pm = prof.grab();
        pm.save(QStringLiteral("/tmp/profile-live-%1.png").arg(shots));
        fprintf(stderr, "[grab %d] %dx%d → /tmp/profile-live-%1.png\n",
                shots, pm.width(), pm.height());
        if (++shots >= 4) app.quit();
    });
    t.start(2500);

    return app.exec();
}
