#pragma once
#include <QWidget>
#include <QPointer>
#include <QHash>
#include <QSet>

class QLabel;
class QVBoxLayout;
class ApiClient;
class QTimer;

// ─────────────────────────────────────────────────────────────────────────────
//  LinkPreviewBar — карточка превью ссылки над композером (как
//  composerLinkPreview в веб-чате): при вводе URL в сообщение тянем
//  /api/link-preview и показываем заголовок/описание/картинку.
// ─────────────────────────────────────────────────────────────────────────────
class LinkPreviewBar : public QWidget {
    Q_OBJECT
public:
    explicit LinkPreviewBar(ApiClient* api, QWidget* parent);
    // Вызывается при каждом изменении текста композера; сам решает,
    // показывать ли карточку (первая ссылка в тексте, как в вебе).
    void updateForText(const QString& text);
    void dismiss();

signals:
    void linkActivated(const QString& url);   // клик по карточке → открыть в браузере

private slots:
    void onPreview(const QString& url, const QJsonObject& preview);

private:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void setLoading(const QString& url);
    void showPreview(const QJsonObject& p, const QString& fallbackUrl);

    ApiClient* api_;
    QWidget*   card_ = nullptr;
    QLabel*    thumb_ = nullptr;
    QLabel*    site_ = nullptr;
    QLabel*    title_ = nullptr;
    QLabel*    desc_ = nullptr;
    QString    currentUrl_;
    QString    dismissedUrl_;
    int        seq_ = 0;
    QHash<QString, QJsonObject> cache_;       // url → превью (положительный ответ)
    QSet<QString> negativeCache_;             // url → превью нет, больше не спрашиваем
};
