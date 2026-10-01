#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QList>
#include <QHash>
#include <functional>

#include "net/Models.h"

class ApiClient;
class WsClient;
class CallEngine;
class CallOverlay;
class QWidget;
class QTimer;

// ─────────────────────────────────────────────────────────────────────────────
//  CallController — оркестрация звонка 1:1 (как calls.js веба):
//    • сигналинг: WS (call_offer/answer/ice/end) + REST-поллинг как резерв;
//    • рингтон входящего (CallSounds), таймер разговора, таймаут гудков 60 с;
//    • статусы лога [[XIPHER_CALL_EVENT]] 1:1 с вебом: missed / rejected;
//    • answered_elsewhere, дедупликация ICE, сворачивание экрана.
// ─────────────────────────────────────────────────────────────────────────────
class CallController : public QObject {
    Q_OBJECT
public:
    CallController(ApiClient* api, WsClient* ws, QWidget* window, QObject* parent = nullptr);

    void startOutgoing(const QString& peerId, const QString& peerName, const QString& avatarUrl);
    // Входящий: offer может прити сразу (WS push) или добираться поллингом.
    void onIncoming(const QString& callerId, const QString& callerName, const QString& callType,
                    const QString& offerSdp = QString(), const QString& avatarUrl = QString());
    bool busy() const { return !peerId_.isEmpty(); }

private:
    CallEngine* createEngine();
    void acceptIncoming();
    void finish(const QString& logStatus);   // end + лог + очистка
    void cleanup();
    void closeWithStatus(const QString& text);   // показать причину и закрыть
    void sendCallEvent(const QString& status);

    ApiClient*   api_;
    WsClient*    ws_;
    QWidget*     window_;
    CallEngine*  engine_ = nullptr;
    CallOverlay* overlay_ = nullptr;
    QString      peerId_, peerName_, avatarUrl_;
    QString      incomingOffer_;      // offer из WS-push (может быть пуст)
    bool         caller_ = false;
    bool         answerApplied_ = false;
    bool         offerFetched_ = false;
    bool         connected_ = false;
    QTimer*      poll_ = nullptr;
    QTimer*      ringTimeout_ = nullptr;   // 60 с без ответа — «без ответа»
    QSet<QString> addedCandidates_;
    std::function<void(const QList<IceServerCfg>&)> onIce_;

    void suppressPeer(const QString& peerId);
    QHash<QString, qint64> suppressed_;   // peer → время подавления повторного звона
    void applyAnswer(const QString& calleeId, const QString& sdp);
    void applyOffer(const QString& callerId, const QString& sdp);
    void addCandidates(const QString& otherId, const QStringList& cands);
    void wireOverlay();
};
