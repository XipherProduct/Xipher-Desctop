#include "net/CallController.h"
#include "util/GlobalHotkeys.h"
#include "net/Prefs.h"
#include "net/CallEngine.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Session.h"
#include "ui/CallOverlay.h"
#include "ui/CallSounds.h"

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
// Веб шлёт offer/answer как JSON {"type","sdp"}, ICE как {"candidate","sdpMid",...}.
QString wrapSdp(const QString& type, const QString& sdp) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("type"), type}, {QStringLiteral("sdp"), sdp}}).toJson(QJsonDocument::Compact));
}
QString wrapCandidate(const QString& cand) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("candidate"), cand}, {QStringLiteral("sdpMid"), QStringLiteral("0")},
        {QStringLiteral("sdpMLineIndex"), 0}}).toJson(QJsonDocument::Compact));
}
QString unwrapSdp(const QString& payload) {
    QString s = payload.trimmed();
    QJsonParseError e{};
    QJsonDocument d = QJsonDocument::fromJson(s.toUtf8(), &e);
    if (e.error == QJsonParseError::NoError && d.isObject()) {
        const QString sdp = d.object().value(QStringLiteral("sdp")).toString();
        if (!sdp.isEmpty()) return sdp;
    }
    if (!s.contains('\n') && !s.contains(' ')) {
        const QByteArray dec = QByteArray::fromBase64(s.toUtf8());
        QJsonDocument d2 = QJsonDocument::fromJson(dec);
        if (d2.isObject()) {
            const QString sdp = d2.object().value(QStringLiteral("sdp")).toString();
            if (!sdp.isEmpty()) return sdp;
        }
        const QString ds = QString::fromUtf8(dec);
        if (ds.contains(QStringLiteral("v=0"))) return ds;
    }
    return s;   // уже сырой SDP
}
// → {candidate, sdpMid}
QPair<QString, QString> unwrapCandidate(const QString& payload) {
    QString s = payload.trimmed();
    QJsonDocument d = QJsonDocument::fromJson(s.toUtf8());
    if (d.isObject()) {
        const QJsonObject o = d.object();
        return { o.value(QStringLiteral("candidate")).toString(),
                 o.value(QStringLiteral("sdpMid")).toString(QStringLiteral("0")) };
    }
    if (!s.contains(' ') && !s.startsWith(QStringLiteral("candidate"))) {
        const QByteArray dec = QByteArray::fromBase64(s.toUtf8());
        QJsonDocument d2 = QJsonDocument::fromJson(dec);
        if (d2.isObject()) {
            const QJsonObject o = d2.object();
            return { o.value(QStringLiteral("candidate")).toString(),
                     o.value(QStringLiteral("sdpMid")).toString(QStringLiteral("0")) };
        }
    }
    return { s, QStringLiteral("0") };
}

// Гудки длится не вечно: как в мобильных сетях, ~60 с без ответа — отбой.
constexpr int kRingTimeoutMs = 60'000;
} // namespace

CallController::CallController(ApiClient* api, WsClient* ws, QWidget* window, QObject* parent)
    : QObject(parent), api_(api), ws_(ws), window_(window) {

    poll_ = new QTimer(this);
    poll_->setInterval(900);   // опрос сигналинга (надёжнее, чем только WS)
    connect(poll_, &QTimer::timeout, this, [this]() {
        if (peerId_.isEmpty()) return;
        if (caller_) {
            if (engine_ && !answerApplied_) api_->getCallAnswer(peerId_);
            if (engine_) api_->getCallIce(peerId_);
        } else {
            if (!engine_ && !offerFetched_ && incomingOffer_.isEmpty()) api_->getCallOffer(peerId_);
            if (engine_) api_->getCallIce(peerId_);
        }
    });

    ringTimeout_ = new QTimer(this);
    ringTimeout_->setSingleShot(true);
    ringTimeout_->setInterval(kRingTimeoutMs);
    connect(ringTimeout_, &QTimer::timeout, this, [this]() {
        // Вызывающий не дождался — вешаем трубку и пишем «без ответа» (в вебе
        // это _wasCaller && !_wasAnswered → sendMissedCallMessage "missed").
        if (peerId_.isEmpty() || connected_) return;
        qInfo() << "[call] ring timeout";
        if (caller_) {
            finish(QStringLiteral("missed"));
        } else {
            // Входящий всё ещё звенит, а вызывающий пропал — тихо убираем.
            ws_->sendCallEnd(peerId_);
            cleanup();
        }
    });

    // ICE-серверы пришли → запускаем отложенное действие.
    connect(api_, &ApiClient::turnConfigReady, this, [this](const QList<IceServerCfg>& servers) {
        qInfo() << "[call] turn-config servers count:" << servers.size();
        if (onIce_) { auto fn = onIce_; onIce_ = nullptr; fn(servers); }
    });

    // Поллинг-результаты.
    connect(api_, &ApiClient::callOfferReady, this, [this](const QString& id, const QString& sdp) { applyOffer(id, sdp); });
    connect(api_, &ApiClient::callAnswerReady, this, [this](const QString& id, const QString& sdp) { applyAnswer(id, sdp); });
    connect(api_, &ApiClient::callIceBatch, this, [this](const QString& id, const QStringList& c) { addCandidates(id, c); });

    // WS (real-time, дублирует поллинг — дедуп ниже).
    connect(ws_, &WsClient::callAnswerReceived, this, [this](const QString& from, const QString& sdp) { applyAnswer(from, sdp); });
    connect(ws_, &WsClient::callIceReceived, this, [this](const QString& from, const QString& cand) { addCandidates(from, {cand}); });
    connect(ws_, &WsClient::callEnded, this, [this](const QString& from) {
        if (from != peerId_) return;
        qWarning() << "[call] call_end от собеседника";
        if (connected_) closeWithStatus(QStringLiteral("Звонок завершён"));
        else cleanup();   // не дозвонились — мгновенно, как в вебе
    });
    // Push входящего звонка: offer уже здесь — поллинг не нужен.
    // Собеседник замьютился — видно в шапке звонка.
    connect(ws_, &WsClient::callMediaStateReceived, this,
            [this](const QString& from, const QString& mediaType, bool enabled) {
        if (from != peerId_ || mediaType != QStringLiteral("audio") || !overlay_) return;
        if (connected_)
            overlay_->setStatusHint(enabled ? QStringLiteral("Голосовой звонок")
                                            : QStringLiteral("🔇 Собеседник в мьюте"));
    });
    connect(ws_, &WsClient::callOfferArrived, this,
            [this](const QString& from, const QString& fromName, const QString& avatar,
                   const QString& callType, const QString& offer) {
        onIncoming(from, fromName, callType, offer, avatar);
    });
    // Трубку взяли в другом клиенте (веб/Android): показываем пояснение,
    // иначе экран «молча исчезал» и выглядело как падение.
    connect(ws_, &WsClient::callAnsweredElsewhere, this, [this](const QString& peer) {
        if (peer == peerId_ || peer.isEmpty()) {
            qInfo() << "[call] answered elsewhere";
            suppressPeer(peerId_);   // поллинг ещё принесёт этот звонок
            CallSounds::instance().stopRingtone();
            closeWithStatus(QStringLiteral("Трубку взяли на другом устройстве"));
        }
    });
    connect(ws_, &WsClient::callMissed, this, [this](const QString& peer) {
        Q_UNUSED(peer);   // бейдж пропущенных обновит список чатов своим ходом
    });
}

CallEngine* CallController::createEngine() {
    auto* e = new CallEngine(this);
    connect(e, &CallEngine::localOffer, this, [this](const QString& sdp) {
        qInfo() << "[call] sending offer (WS), len" << sdp.size();
        ws_->sendCallOffer(peerId_, wrapSdp(QStringLiteral("offer"), sdp), QStringLiteral("audio"));
    });
    connect(e, &CallEngine::localAnswer, this, [this](const QString& sdp) {
        qInfo() << "[call] sending answer (WS), len" << sdp.size();
        ws_->sendCallAnswer(peerId_, wrapSdp(QStringLiteral("answer"), sdp));
    });
    connect(e, &CallEngine::localCandidate, this, [this](const QString& cand, const QString&) {
        ws_->sendCallIce(peerId_, wrapCandidate(cand));
    });
    connect(e, &CallEngine::connected, this, [this]() {
        if (connected_) return;
        connected_ = true;
        ringTimeout_->stop();
        CallSounds::instance().stopRingtone();
        if (overlay_) {
            overlay_->setState(CallOverlay::State::Active);
            overlay_->startCallTimer();
        }
    });
    connect(e, &CallEngine::ended, this, [this]() {
        qWarning() << "[call] engine ended (state Disconnected/Closed)";
        // Резкое исчезновение экрана без объяснения читается как краш:
        // показываем причину полторы секунды.
        if (connected_) closeWithStatus(QStringLiteral("Соединение потеряно"));
        else closeWithStatus(QStringLiteral("Звонок завершён"));
    });
    connect(e, &CallEngine::failed, this, [this](const QString& r) {
        qWarning() << "[call] engine FAILED:" << r;
        // Соединение не сложилось: вызывающий пишет «без ответа» (как в вебе),
        // принимающему показываем причину — экран не исчезает молча.
        if (caller_ && !connected_) finish(QStringLiteral("missed"));
        else closeWithStatus(QStringLiteral("Не удалось соединиться: ") + r);
    });
    return e;
}

void CallController::wireOverlay() {
    connect(overlay_, &CallOverlay::hangup, this, [this]() {
        // Исходящий до соединения — «без ответа» при таймауте, «отменён»
        // сознательно: веб в обоих случаях пишет статус "missed".
        if (!connected_ && caller_) finish(QStringLiteral("missed"));
        else if (!connected_) finish(QStringLiteral("rejected"));
        else finish(QString());
    });
    connect(overlay_, &CallOverlay::accept, this, [this]() { acceptIncoming(); });
    connect(overlay_, &CallOverlay::decline, this, [this]() {
        finish(QStringLiteral("rejected"));
    });
    connect(overlay_, &CallOverlay::muteToggled, this, [this](bool m) { if (engine_) engine_->setMuted(m); });
    // CAL-04: A/B шумодава прямо в звонке.
    connect(overlay_, &CallOverlay::noiseSuppressionToggled, this,
            [this](bool on) { if (engine_) engine_->setNoiseSuppression(on); });
    // CAL-02: 🔴 — кольцевой буфер 60 с эфира → WAV в загрузки.
    connect(overlay_, &CallOverlay::clipRequested, this, [this]() {
        if (!engine_) return;
        const QString path = engine_->saveClip();
        overlay_->setInfo(path.isEmpty()
            ? QStringLiteral("Клип пуст (эфир <1 с)")
            : QStringLiteral("Клип сохранён: %1").arg(path));
    });
    // CAL-01: звёзды пишутся локально (серверного API оценки нет — блокер ТЗ).
    connect(overlay_, &CallOverlay::rated, this, [this](int stars) {
        Prefs::setStr(QStringLiteral("xipher_call_rating_last"),
                      QString::number(stars));
    });
    // CAL-05/DSC-02: глобальные хоткеи (PTT/mute/deaf/accept) — вне окна.
    hotkeys_ = new GlobalHotkeys(this);
    connect(hotkeys_, &GlobalHotkeys::pttPressed, this, [this]() {
        if (engine_ && engine_->inCall())
            engine_->setMuted(false);
    });
    connect(hotkeys_, &GlobalHotkeys::pttReleased, this, [this]() {
        if (engine_ && engine_->inCall())
            engine_->setMuted(true);
    });
    connect(hotkeys_, &GlobalHotkeys::muteToggled, this, [this]() {
        if (engine_) engine_->setMuted(!engine_->isMuted());
    });
    connect(hotkeys_, &GlobalHotkeys::deafToggled, this, [this]() {
        if (engine_) engine_->setDeaf(!engine_->isDeaf());
    });
    connect(hotkeys_, &GlobalHotkeys::acceptCall, this, [this]() {
        if (overlay_ && overlay_->isVisible() && overlay_->state() == CallOverlay::State::Incoming)
            acceptIncoming();
    });
    hotkeys_->start();
    connect(overlay_, &CallOverlay::deafToggled, this, [this](bool d) { if (engine_) engine_->setDeaf(d); });
    connect(overlay_, &CallOverlay::minimizeRequested, this, [this]() {
        if (!overlay_) return;
        overlay_->hide();
        overlay_->minimizedBar()->show();
        overlay_->minimizedBar()->raise();
    });
}

void CallController::startOutgoing(const QString& peerId, const QString& peerName, const QString& avatarUrl) {
    if (busy() || peerId.isEmpty()) return;
    if (peerId == Session::instance().userId) return;
    peerId_ = peerId; peerName_ = peerName; avatarUrl_ = avatarUrl; caller_ = true;
    answerApplied_ = false; offerFetched_ = false; connected_ = false;
    addedCandidates_.clear(); incomingOffer_.clear();

    overlay_ = new CallOverlay(window_);
    overlay_->setPeer(peerName, avatarUrl);
    overlay_->setState(CallOverlay::State::Outgoing);
    wireOverlay();
    overlay_->show();
    overlay_->raise();

    qInfo() << "[call] outgoing to" << peerId_;
    engine_ = createEngine();
    api_->callNotify(peerId_, QStringLiteral("audio"));
    onIce_ = [this](const QList<IceServerCfg>& servers) {
        if (engine_) { engine_->setIceServers(servers); engine_->startAsCaller(); }
    };
    api_->getTurnConfig();
    poll_->start();
    ringTimeout_->start();
}

// Недавно завершённый звонок поллинг приносит снова (сервер держит его
// «ringing» до ответа любой из сторон) — подавляем на минуту.
void CallController::suppressPeer(const QString& peerId) {
    if (peerId.isEmpty()) return;
    suppressed_[peerId] = QDateTime::currentMSecsSinceEpoch();
}

void CallController::onIncoming(const QString& callerId, const QString& callerName,
                                const QString& /*callType*/, const QString& offerSdp,
                                const QString& avatarUrl) {
    if (callerId.isEmpty() || peerId_ == callerId) return;
    // Свежезавершённый/отвеченный в другом месте — повторный звонок того же
    // человека в течение минуты не дёргает экран.
    const qint64 sup = suppressed_.value(callerId, 0);
    if (sup > 0 && QDateTime::currentMSecsSinceEpoch() - sup < 60'000) {
        qInfo() << "[call] suppressed re-ring from" << callerId;
        return;
    }
    if (busy()) {
        // Уже в звонке — сразу отбой (как «Already in call, rejecting» в вебе).
        ws_->sendCallEnd(callerId);
        api_->callEnd(callerId);
        return;
    }
    qInfo() << "[call] incoming from" << callerId << callerName
            << "offer attached:" << !offerSdp.isEmpty();
    peerId_ = callerId;
    peerName_ = callerName.isEmpty() ? callerId : callerName;
    avatarUrl_ = avatarUrl;
    caller_ = false;
    answerApplied_ = false; offerFetched_ = false; connected_ = false;
    addedCandidates_.clear();
    incomingOffer_ = offerSdp;

    overlay_ = new CallOverlay(window_);
    overlay_->setPeer(peerName_, avatarUrl_);
    overlay_->setState(CallOverlay::State::Incoming);
    wireOverlay();
    overlay_->show();
    overlay_->raise();

    // Рингтон — двухнотный chime, цикл 2.4 с (playCallRingtone веба).
    CallSounds::instance().startRingtone();
    ringTimeout_->start();
}

void CallController::acceptIncoming() {
    if (!overlay_) return;
    CallSounds::instance().stopRingtone();
    overlay_->setState(CallOverlay::State::Active);
    overlay_->setStatusHint(QStringLiteral("Соединение…"));
    poll_->start();
    // Offer уже пришёл с WS-push — сразу строим answer; иначе ждём поллинг.
    if (!incomingOffer_.isEmpty()) applyOffer(peerId_, incomingOffer_);
    else api_->getCallOffer(peerId_);
}

void CallController::applyOffer(const QString& callerId, const QString& payload) {
    if (caller_ || engine_ || callerId != peerId_ || payload.isEmpty()) return;
    const QString sdp = unwrapSdp(payload);
    if (sdp.isEmpty()) return;
    qInfo() << "[call] got remote offer, sdp len" << sdp.size();
    offerFetched_ = true;
    incomingOffer_.clear();
    engine_ = createEngine();
    const QString sdpCopy = sdp;
    onIce_ = [this, sdpCopy](const QList<IceServerCfg>& servers) {
        if (engine_) { engine_->setIceServers(servers); engine_->startAsCallee(sdpCopy); }
    };
    api_->getTurnConfig();
}

void CallController::applyAnswer(const QString& calleeId, const QString& payload) {
    if (!engine_ || answerApplied_ || calleeId != peerId_ || payload.isEmpty()) return;
    const QString sdp = unwrapSdp(payload);
    if (sdp.isEmpty()) return;
    qInfo() << "[call] got remote answer, sdp len" << sdp.size();
    answerApplied_ = true;
    engine_->setRemoteAnswer(sdp);
}

void CallController::addCandidates(const QString& otherId, const QStringList& cands) {
    if (!engine_ || otherId != peerId_) return;
    int added = 0;
    for (const QString& c : cands) {
        if (c.isEmpty() || addedCandidates_.contains(c)) continue;
        addedCandidates_.insert(c);
        const QPair<QString, QString> cm = unwrapCandidate(c);
        if (!cm.first.isEmpty()) { engine_->addRemoteCandidate(cm.first, cm.second); ++added; }
    }
    if (added > 0) qInfo() << "[call] added remote candidates:" << added << "total" << addedCandidates_.size();
}

void CallController::sendCallEvent(const QString& status) {
    if (peerId_.isEmpty()) return;
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    const QString content = QStringLiteral("[[XIPHER_CALL_EVENT]]{\"status\":\"%1\",\"ts\":%2,\"v\":1}")
                                .arg(status).arg(ts);
    api_->sendRaw(peerId_, content, QStringLiteral("text"),
                  QStringLiteral("ce_%1").arg(ts));
}

void CallController::finish(const QString& logStatus) {
    CallSounds::instance().stopRingtone();
    ws_->sendCallEnd(peerId_);
    api_->callEnd(peerId_);
    if (!logStatus.isEmpty()) sendCallEvent(logStatus);
    cleanup();
}

// Закрытие с последней подписью: человек видит, ЧТО произошло, а не
// «экран мигнул и пропал».
void CallController::closeWithStatus(const QString& text) {
    if (!overlay_) { cleanup(); return; }
    overlay_->setStatusHint(text);
    overlay_->setState(CallOverlay::State::Active);
    overlay_->minimizedBar()->hide();
    if (overlay_->isVisible()) overlay_->raise();
    // CAL-01: звёзды на экране завершения (локально; API оценки на сервере нет).
    if (connected_) overlay_->showRatingStars();
    QTimer::singleShot(1500, this, [this]() { cleanup(); });
}

void CallController::cleanup() {
    static bool inCleanup = false;
    if (inCleanup) return;
    inCleanup = true;
    CallSounds::instance().stopRingtone();
    if (poll_) poll_->stop();
    if (ringTimeout_) ringTimeout_->stop();
    onIce_ = nullptr;
    answerApplied_ = false; offerFetched_ = false; connected_ = false;
    addedCandidates_.clear();
    incomingOffer_.clear();
    if (engine_) { CallEngine* e = engine_; engine_ = nullptr; e->hangup(); e->deleteLater(); }
    if (overlay_) {
        CallOverlay* o = overlay_; overlay_ = nullptr;
        // Бар — ребёнок ОКНА, а не оверлея: без явного удаления копился бы.
        if (o->minimizedBar()) o->minimizedBar()->deleteLater();
        o->hide();
        o->deleteLater();
    }
    if (!peerId_.isEmpty()) suppressPeer(peerId_);
    peerId_.clear(); peerName_.clear(); avatarUrl_.clear();
    inCleanup = false;
}
