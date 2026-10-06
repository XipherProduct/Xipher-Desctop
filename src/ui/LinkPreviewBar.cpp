#include "ui/LinkPreviewBar.h"
#include "net/ApiClient.h"

#include <QDesktopServices>
#include <QEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmapCache>
#include <QPushButton>
#include <QJsonObject>
#include <QLabel>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {

// Кандидат-URL — порт LINK_PREVIEW_CANDIDATE_REGEX из chat.js веба.
const QRegularExpression& candidateRegex() {
    static const QRegularExpression re(QStringLiteral(
        "(?:https?://)?(?:www\\.)?(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z]{2,63}"
        "(?::\\d{2,5})?(?:/[^\\s<]*)?"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Хосты, которые превью не заслуживают (SSRF-гигиена веба: isLinkPreviewHostAllowed).
bool hostAllowed(const QString& host) {
    const QString h = host.trimmed().toLower();
    if (h.isEmpty() || h == QLatin1String("localhost") || h == QLatin1String("0.0.0.0")
        || h == QLatin1String("::") || h == QLatin1String("::1"))
        return false;
    const QStringList badSuffix = {
        QStringLiteral(".localhost"), QStringLiteral(".local"), QStringLiteral(".internal"),
        QStringLiteral(".lan"), QStringLiteral(".home.arpa") };
    for (const QString& s : badSuffix) if (h.endsWith(s)) return false;
    // Приватные IPv4.
    static const QRegularExpression ipv4(QStringLiteral("^((\\d{1,3})\\.){3}(\\d{1,3})$"));
    const auto m = ipv4.match(h);
    if (m.hasMatch()) {
        const QStringList parts = h.split(QLatin1Char('.'));
        const int a = parts[0].toInt();
        if (a == 10 || a == 127 || (a == 192 && parts[1] == QLatin1String("168"))
            || (a == 172 && parts[1].toInt() >= 16 && parts[1].toInt() <= 31)
            || (a == 169 && parts[1] == QLatin1String("254"))) return false;
    }
    return true;
}

// Нормализация URL кандидата (порт normalizeLinkPreviewUrl).
QString normalizeUrl(const QString& raw) {
    QString s = raw.trimmed();
    if (s.isEmpty()) return {};
    if (!s.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !s.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        s = QStringLiteral("https://") + s;
    QUrl u(s, QUrl::StrictMode);
    if (!u.isValid() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https")))
        return {};
    u.setFragment(QString());
    if (!hostAllowed(u.host())) return {};
    return u.toString();
}

QString extractFirstUrl(const QString& text) {
    auto it = candidateRegex().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int idx = m.capturedStart();
        if (idx > 0 && text[idx - 1] == QLatin1Char('@')) continue;   // не упоминание
        const QString norm = normalizeUrl(m.captured(0));
        if (!norm.isEmpty()) return norm;
    }
    return {};
}

QString hostOf(const QString& url) {
    return QUrl(url).host().remove(QLatin1String("www."));
}

} // namespace

LinkPreviewBar::LinkPreviewBar(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {
    setObjectName(QStringLiteral("linkPreviewPanel"));
    setVisible(false);

    card_ = new QWidget(this);
    card_->setObjectName(QStringLiteral("lpCard"));
    card_->setStyleSheet(QStringLiteral(
        "#lpCard { background:#1A1822; border:1px solid rgba(255,255,255,10%);"
        "  border-left:3px solid #8B5CF6; border-radius:14px; }"
        "#lpSite { color:#8B5CF6; font-size:11px; font-weight:700; }"
        "#lpTitle { color:#F3F1F8; font-size:13px; font-weight:600; }"
        "#lpDesc { color:#ACA6BD; font-size:12px; }"
        "#lpClose { background:transparent; border:none; color:#726C82; font-size:14px; }"
        "#lpClose:hover { color:#F3F1F8; }"));

    auto* cl = new QHBoxLayout(card_);
    cl->setContentsMargins(10, 8, 8, 8);
    cl->setSpacing(10);

    thumb_ = new QLabel(card_);
    thumb_->setFixedSize(56, 56);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setStyleSheet(QStringLiteral(
        "background:rgba(255,255,255,5%);border-radius:8px;color:#726C82;font-size:20px;"));
    thumb_->setText(QStringLiteral("🔗"));
    cl->addWidget(thumb_);

    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(2);
    site_  = new QLabel(card_);
    title_ = new QLabel(card_);
    desc_  = new QLabel(card_);
    desc_->setWordWrap(true);
    desc_->setMaximumHeight(30);
    textCol->addWidget(site_);
    textCol->addWidget(title_);
    textCol->addWidget(desc_);
    cl->addLayout(textCol, 1);

    auto* close = new QPushButton(QStringLiteral("✕"), card_);
    close->setObjectName(QStringLiteral("lpClose"));
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(24, 24);
    connect(close, &QPushButton::clicked, this, &LinkPreviewBar::dismiss);
    cl->addWidget(close);

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 6);
    outer->addWidget(card_);

    card_->setCursor(Qt::PointingHandCursor);
    // Клик по карточке — открыть ссылку (как openLinkPreviewUrl в вебе).
    card_->installEventFilter(this);
    connect(api_, &ApiClient::linkPreviewFetched, this, &LinkPreviewBar::onPreview);
}

bool LinkPreviewBar::eventFilter(QObject* obj, QEvent* e) {
    if (obj == card_ && e->type() == QEvent::MouseButtonRelease
        && !currentUrl_.isEmpty()) {
        emit linkActivated(currentUrl_);
        QDesktopServices::openUrl(QUrl(currentUrl_));
    }
    return QWidget::eventFilter(obj, e);
}

void LinkPreviewBar::updateForText(const QString& text) {
    const QString url = extractFirstUrl(text);
    if (url.isEmpty() || url == dismissedUrl_) { setVisible(false); currentUrl_.clear(); return; }
    if (url == currentUrl_ && isVisible()) return;
    currentUrl_ = url;

    if (cache_.contains(url)) { showPreview(cache_.value(url), url); return; }
    if (negativeCache_.contains(url)) { setVisible(false); return; }
    setLoading(url);
    api_->fetchLinkPreview(url);
}

void LinkPreviewBar::dismiss() {
    dismissedUrl_ = currentUrl_;   // как composerLinkPreviewDismissedUrl в вебе
    setVisible(false);
}

void LinkPreviewBar::setLoading(const QString& url) {
    setVisible(true);
    site_->setText(hostOf(url));
    title_->setText(QStringLiteral("Загрузка превью…"));
    title_->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;"));
    desc_->clear();
    thumb_->setText(QStringLiteral("🔗"));
}

void LinkPreviewBar::onPreview(const QString& url, const QJsonObject& preview) {
    if (url != currentUrl_) return;
    const bool ok = preview.value(QStringLiteral("success")).toBool(false)
        && (preview.value(QStringLiteral("preview_available")).toBool()
            || preview.value(QStringLiteral("preview_available")).toString() == QLatin1String("true"));
    if (!ok) { negativeCache_.insert(url); setVisible(false); return; }
    cache_.insert(url, preview);
    showPreview(preview, url);
}

void LinkPreviewBar::showPreview(const QJsonObject& p, const QString& fallbackUrl) {
    const QString host = p.value(QStringLiteral("hostname")).toString(hostOf(fallbackUrl));
    const QString title = p.value(QStringLiteral("title")).toString(host);
    const QString desc = p.value(QStringLiteral("description")).toString();
    const QString imageUrl = p.value(QStringLiteral("image_url")).toString();

    site_->setText(host);
    title_->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:13px;font-weight:600;"));
    title_->setText(title.isEmpty() ? host : title);
    title_->setVisible(!title.isEmpty());
    desc_->setText(desc);
    desc_->setVisible(!desc.isEmpty());

    if (!imageUrl.isEmpty()) {
        QPixmap cached;
        const bool hit = QPixmapCache::find(imageUrl, &cached);
        if (hit && !cached.isNull()) {
            thumb_->setPixmap(cached.scaled(56, 56, Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation));
        } else {
            // Картинка превью — по сети напрямую (публичный CDN), QPointer-гард.
            QPointer<QLabel> guard(thumb_);
            auto* nam = new QNetworkAccessManager(this);
            QNetworkRequest req{QUrl(imageUrl)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply* reply = nam->get(req);
            connect(reply, &QNetworkReply::finished, this, [this, guard, reply, imageUrl, nam]() {
                reply->deleteLater();
                nam->deleteLater();
                if (reply->error() != QNetworkReply::NoError) return;
                QPixmap pm;
                if (!pm.loadFromData(reply->readAll())) return;
                QPixmapCache::insert(imageUrl, pm);
                if (guard)
                    guard->setPixmap(pm.scaled(56, 56, Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation));
            });
        }
    } else {
        thumb_->setText(QStringLiteral("🔗"));
    }
    setVisible(true);
}
