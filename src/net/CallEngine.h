#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QPair>
#include <memory>

#include "net/Models.h"

namespace rtc { class PeerConnection; class Track; class RtpPacketizationConfig; }
class QAudioSource;
class QAudioSink;
class QIODevice;
struct OpusEncoder;
struct OpusDecoder;

// ─────────────────────────────────────────────────────────────────────────────
//  CallEngine — голосовой звонок поверх WebRTC (libdatachannel) + Opus/Qt audio.
//  Совместим с браузерным пиром (веб-клиент): SDP offer/answer + trickle ICE.
// ─────────────────────────────────────────────────────────────────────────────
class CallEngine : public QObject {
    Q_OBJECT
public:
    explicit CallEngine(QObject* parent = nullptr);
    ~CallEngine() override;

    void setIceServers(const QList<IceServerCfg>& servers);   // STUN/TURN с эфемерными кредами
    void startAsCaller();                              // создаёт offer
    void startAsCallee(const QString& remoteOffer);    // принимает offer, создаёт answer
    void setRemoteAnswer(const QString& sdp);
    void addRemoteCandidate(const QString& cand, const QString& mid);
    void hangup();
    void setMuted(bool muted);
    void setDeaf(bool deaf);   // не слышать собеседника (динамик в 0)

signals:
    void localOffer(const QString& sdp);
    void localAnswer(const QString& sdp);
    void localCandidate(const QString& candidate, const QString& mid);
    void connected();
    void ended();
    void failed(const QString& reason);

private:
    void createPeerConnection();
    void addLocalAudioTrack();                                   // для звонящего (offer)
    void configureTrack(std::shared_ptr<rtc::Track> track);      // навесить Opus-пакетайзер/приём
    void startAudioIo();
    void stopAudioIo();
    void onEncodedFrameReady();
    void onIncomingRtp(const QByteArray& rtp);

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> track_;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig_;

    QList<IceServerCfg> iceServers_;
    bool caller_ = false;
    bool muted_ = false;
    bool deaf_  = false;
    bool audioStarted_ = false;
    bool hasRemoteDesc_ = false;
    QList<QPair<QString, QString>> pendingCands_;   // кандидаты до remote-description
    void flushCandidates();

    // Audio (48 kHz mono, 20 ms кадры)
    QAudioSource* mic_ = nullptr;
    QIODevice*    micIo_ = nullptr;
    QAudioSink*   spk_ = nullptr;
    QIODevice*    spkIo_ = nullptr;
    QByteArray    capBuf_;
    OpusEncoder*  enc_ = nullptr;
    OpusDecoder*  dec_ = nullptr;
};
