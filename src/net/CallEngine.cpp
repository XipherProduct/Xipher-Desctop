#include "net/CallEngine.h"

#ifdef XIPHER_HAVE_WEBRTC

#include <rtc/rtc.hpp>
#if __has_include(<opus/opus.h>)
#include <opus/opus.h>   // Linux: пакет кладёт заголовки в opus/
#else
#include <opus.h>        // Windows: путь добавлен в include-директории
#endif

#include <QAudioSource>
#include "net/RnNoise.h"
#include "net/Prefs.h"
#include <QTimer>
#include <cmath>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QIODevice>
#include <QDebug>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QHostInfo>

namespace {

// Резолвим hostname → IPv4 САМИ (Qt), libjuice отдаём литеральный IP.
// Внутренний резолвер libjuice на Windows за VPN-туннелем зависает
// ("Waiting for resolver thread") → TURN-allocate не уходит, relay не собирается.
QString resolveIPv4(const QString& host) {
    if (QHostAddress(host).protocol() == QAbstractSocket::IPv4Protocol) return host;
    const QHostInfo info = QHostInfo::fromName(host);
    for (const QHostAddress& a : info.addresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol) return a.toString();
    return host;   // не вышло — отдаём как есть
}

constexpr int kSampleRate = 48000;
constexpr int kChannels   = 1;
constexpr int kFrame      = 960;             // 20 мс при 48 кГц
constexpr int kFrameBytes = kFrame * 2;      // int16 mono
constexpr int kPayloadType = 111;            // Opus

QAudioFormat pcmFormat() {
    QAudioFormat f;
    f.setSampleRate(kSampleRate);
    f.setChannelCount(kChannels);
    f.setSampleFormat(QAudioFormat::Int16);
    return f;
}

// Разбор ICE-URL: "stun:host:port" / "turn:host:port?transport=udp" / "turns:host:port?transport=tcp".
// host для IPv6 может быть в [...]; username/credential НЕ в URL (передаются отдельно),
// т.к. TURN-username вида "<expiry>:<uuid>" содержит двоеточие и ломает userinfo-парсинг.
bool parseIceUrl(const QString& url, QString& scheme, QString& host,
                 uint16_t& port, QString& transport) {
    QString s = url.trimmed();
    const int colon = s.indexOf(':');
    if (colon < 0) return false;
    scheme = s.left(colon).toLower();
    QString rest = s.mid(colon + 1);

    transport = QStringLiteral("udp");
    const int q = rest.indexOf('?');
    if (q >= 0) {
        const QString query = rest.mid(q + 1);
        rest = rest.left(q);
        for (const QString& kv : query.split('&'))
            if (kv.startsWith(QStringLiteral("transport=")))
                transport = kv.mid(10).toLower();
    }

    const bool turns = (scheme == QStringLiteral("turns"));
    if (rest.startsWith('[')) {                       // IPv6 в скобках
        const int close = rest.indexOf(']');
        if (close < 0) return false;
        host = rest.mid(1, close - 1);
        const QString tail = rest.mid(close + 1);     // ":port" или ""
        port = tail.startsWith(':') ? tail.mid(1).toUShort() : 0;
    } else {
        const int pc = rest.lastIndexOf(':');
        if (pc >= 0) { host = rest.left(pc); port = rest.mid(pc + 1).toUShort(); }
        else         { host = rest; port = 0; }
    }
    if (port == 0) port = turns ? 5349 : 3478;
    return !host.isEmpty();
}
}

CallEngine::CallEngine(QObject* parent) : QObject(parent) {
    // Подробный лог libdatachannel/libjuice → наш файл (видно TURN-аллокацию, ICE-чеки).
    static bool loggerInit = false;
    if (!loggerInit) {
        loggerInit = true;
        // Только Warning+ — Verbose писал по диску на каждый STUN-пакет и тормозил
        // ICE-проверки (коннект растягивался на ~13 c). Свои [call]-логи остаются.
        rtc::InitLogger(rtc::LogLevel::Warning, [](rtc::LogLevel lvl, std::string msg) {
            qInfo().noquote() << "[rtc]" << int(lvl) << QString::fromStdString(msg);
        });
    }
    int err = 0;
    enc_ = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (enc_) opus_encoder_ctl(enc_, OPUS_SET_BITRATE(24000));
    dec_ = opus_decoder_create(kSampleRate, kChannels, &err);
}

CallEngine::~CallEngine() {
    hangup();
    if (enc_) opus_encoder_destroy(enc_);
    if (dec_) opus_decoder_destroy(dec_);
}

void CallEngine::setIceServers(const QList<IceServerCfg>& servers) { iceServers_ = servers; }

void CallEngine::createPeerConnection() {
    rtc::Configuration config;
    int turnCount = 0;
    for (const IceServerCfg& cfg : iceServers_) {
        QString scheme, host, transport; uint16_t port = 0;
        if (!parseIceUrl(cfg.url, scheme, host, port, transport)) continue;
        host = resolveIPv4(host);   // отдаём libjuice IP, минуя его зависающий резолвер
        try {
            if (scheme == QStringLiteral("stun")) {
                config.iceServers.emplace_back(rtc::IceServer(host.toStdString(), port));
            } else if (scheme == QStringLiteral("turn") || scheme == QStringLiteral("turns")) {
                rtc::IceServer::RelayType rt = rtc::IceServer::RelayType::TurnUdp;
                if (scheme == QStringLiteral("turns"))        rt = rtc::IceServer::RelayType::TurnTls;
                else if (transport == QStringLiteral("tcp"))  rt = rtc::IceServer::RelayType::TurnTcp;
                config.iceServers.emplace_back(rtc::IceServer(
                    host.toStdString(), port,
                    cfg.username.toStdString(), cfg.credential.toStdString(), rt));
                ++turnCount;
            }
        } catch (const std::exception& e) {
            qWarning() << "[call] bad ice server" << cfg.url << ":" << e.what();
        }
    }
    // Форсим ЧИСТЫЙ IPv4 (bind any IPv4): по умолчанию libjuice открывает IPv6
    // dual-stack сокет и шлёт на IPv4 TURN через ::ffff:-mapping — на Windows за
    // VPN-туннелем (happ-tun) такие пакеты не доходят до coturn, relay-allocate
    // вечно "pending". Рабочий python-тест использовал AF_INET/0.0.0.0 — повторяем.
    // ОС сама выберет src по маршруту. Сервер IPv4-only (нет AAAA), пиры тоже IPv4.
    config.bindAddress = "0.0.0.0";

    // Только свои сервера (из /api/turn-credentials). Сторонние STUN не используем.
    qInfo() << "[call] ice servers count" << int(config.iceServers.size())
            << "(turn relays:" << turnCount << ")";

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onLocalDescription([this](rtc::Description desc) {
        const QString sdp = QString::fromStdString(std::string(desc));
        qInfo() << "[call] local description type" << QString::fromStdString(desc.typeString()) << "len" << sdp.size();
        if (desc.type() == rtc::Description::Type::Offer) emit localOffer(sdp);
        else if (desc.type() == rtc::Description::Type::Answer) emit localAnswer(sdp);
    });
    pc_->onLocalCandidate([this](rtc::Candidate cand) {
        const QString cs = QString::fromStdString(std::string(cand));
        qInfo().noquote() << "[call] local candidate:" << cs;
        emit localCandidate(cs, QString::fromStdString(cand.mid()));
    });
    pc_->onGatheringStateChange([](rtc::PeerConnection::GatheringState s) {
        qInfo() << "[call] gathering state" << int(s);
    });
    pc_->onIceStateChange([](rtc::PeerConnection::IceState s) {
        // 1 Checking, 2 Connected, 3 Completed, 4 Failed, 5 Disconnected
        qInfo() << "[call] ICE state" << int(s);
    });
    // Отвечающий получает аудио-трек из offer'а здесь (его нельзя добавлять заранее).
    pc_->onTrack([this](std::shared_ptr<rtc::Track> t) {
        qInfo() << "[call] onTrack";
        if (!track_) configureTrack(t);
    });
    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        qInfo() << "[call] pc state" << int(state);
        if (state == rtc::PeerConnection::State::Connected) {
            QMetaObject::invokeMethod(this, [this]() {
                if (disconnectGrace_) disconnectGrace_->stop();
                startAudioIo();
                emit connected();
            });
        } else if (state == rtc::PeerConnection::State::Disconnected) {
            // Кратковременный сбой — не повод рвать разговор (веб держит
            // recovery-grace 180 c): даём ICE 8 c на восстановление.
            QMetaObject::invokeMethod(this, [this]() {
                if (!disconnectGrace_) {
                    disconnectGrace_ = new QTimer(this);
                    disconnectGrace_->setSingleShot(true);
                    connect(disconnectGrace_, &QTimer::timeout, this, [this]() {
                        if (pc_ && pc_->state() == rtc::PeerConnection::State::Disconnected)
                            emit ended();
                    });
                }
                disconnectGrace_->start(8000);
            });
        } else if (state == rtc::PeerConnection::State::Closed) {
            QMetaObject::invokeMethod(this, [this]() { emit ended(); });
        } else if (state == rtc::PeerConnection::State::Failed) {
            QMetaObject::invokeMethod(this, [this]() { emit failed(QStringLiteral("ICE failed")); });
        }
    });
}

void CallEngine::addLocalAudioTrack() {
    // mid="0" — как у браузера. CallController::wrapCandidate шлёт sdpMid="0",
    // поэтому mid дорожки ДОЛЖЕН быть "0", иначе собеседник не сопоставит наши
    // ICE-кандидаты с remote-описанием (mid "audio" ≠ "0") и связь не поднимется.
    rtc::Description::Audio media("0", rtc::Description::Direction::SendRecv);
    media.addOpusCodec(kPayloadType);
    media.setBitrate(24);
    configureTrack(pc_->addTrack(media));
}

void CallEngine::configureTrack(std::shared_ptr<rtc::Track> track) {
    track_ = track;
    rtpConfig_ = std::make_shared<rtc::RtpPacketizationConfig>(
        12345678u, "xipher", kPayloadType, 48000u);
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig_);
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig_);
    packetizer->addToChain(srReporter);
    auto nack = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack);
    track_->setMediaHandler(packetizer);

    // Приём RTP: парсим заголовок и декодируем Opus.
    track_->onMessage([this](rtc::message_variant data) {
        if (!std::holds_alternative<rtc::binary>(data)) return;
        const auto& bin = std::get<rtc::binary>(data);
        QByteArray pkt(reinterpret_cast<const char*>(bin.data()), int(bin.size()));
        QMetaObject::invokeMethod(this, [this, pkt]() { onIncomingRtp(pkt); });
    }, nullptr);
}

void CallEngine::startAsCaller() {
    caller_ = true;
    createPeerConnection();
    addLocalAudioTrack();
    pc_->setLocalDescription();   // → onLocalDescription(offer)
}

void CallEngine::startAsCallee(const QString& remoteOffer) {
    caller_ = false;
    createPeerConnection();
    // Трек НЕ добавляем заранее — он придёт из offer через onTrack.
    try {
        pc_->setRemoteDescription(rtc::Description(remoteOffer.toStdString(), "offer"));
        hasRemoteDesc_ = true;
        flushCandidates();
        pc_->setLocalDescription();   // → onLocalDescription(answer)
    } catch (const std::exception& e) {
        qWarning() << "[call] setRemoteDescription(offer) FAILED:" << e.what();
        emit failed(QString::fromUtf8(e.what()));
    }
}

void CallEngine::setRemoteAnswer(const QString& sdp) {
    if (!pc_) return;
    try {
        pc_->setRemoteDescription(rtc::Description(sdp.toStdString(), "answer"));
        hasRemoteDesc_ = true;
        flushCandidates();
    } catch (const std::exception& e) {
        qWarning() << "[call] setRemoteDescription(answer) FAILED:" << e.what();
        emit failed(QString::fromUtf8(e.what()));
    }
}

void CallEngine::addRemoteCandidate(const QString& cand, const QString& mid) {
    if (!pc_) return;
    if (!hasRemoteDesc_) { pendingCands_.append({cand, mid}); return; }   // буфер до remote-desc
    qInfo().noquote() << "[call] remote candidate (mid" << mid << "):" << cand;
    try { pc_->addRemoteCandidate(rtc::Candidate(cand.toStdString(), mid.toStdString())); }
    catch (const std::exception& e) { qWarning() << "[call] addRemoteCandidate failed:" << e.what(); }
}

void CallEngine::flushCandidates() {
    if (!pendingCands_.isEmpty()) qInfo() << "[call] flush buffered candidates:" << pendingCands_.size();
    for (const auto& c : pendingCands_) {
        try { pc_->addRemoteCandidate(rtc::Candidate(c.first.toStdString(), c.second.toStdString())); }
        catch (const std::exception& e) { qWarning() << "[call] flush candidate failed:" << e.what(); }
    }
    pendingCands_.clear();
}

void CallEngine::setMuted(bool muted) { muted_ = muted; }

// Демо-тон: арпеджио C5-E5-G5, нота 0.5 c, амплитудная огибающая без щелчков.
void CallEngine::setTestTone(bool on) {
    testTone_ = on;
    if (!on) {
        if (toneTimer_) toneTimer_->stop();
        return;
    }
    if (!toneTimer_) {
        toneTimer_ = new QTimer(this);
        toneTimer_->setInterval(20);
        connect(toneTimer_, &QTimer::timeout, this, &CallEngine::sendToneFrame);
    }
    if (audioStarted_) toneTimer_->start();
}

void CallEngine::sendToneFrame() {
    if (!enc_ || !track_) return;
    static const double kNotes[] = {523.25, 659.25, 783.99, 659.25};   // C5 E5 G5 E5
    const double freq = kNotes[(toneFrames_ / 25) % 4];                // нота = 25 кадров
    const quint64 inNote = toneFrames_ % 25;
    // Плавные 5 мс на краях ноты — иначе щелчки на стыках.
    double env = 1.0;
    if (inNote < 12) env = double(inNote) / 12.0;
    else if (inNote > 12 && inNote < 25 - 12) env = 1.0;
    else if (inNote >= 25 - 12) env = double(25 - inNote) / 12.0;
    env *= 0.16;   // негромко

    opus_int16 pcm[kFrame];
    for (int i = 0; i < kFrame; ++i) {
        tonePhase_ += 2.0 * M_PI * freq / 48000.0;
        if (tonePhase_ > 2.0 * M_PI) tonePhase_ -= 2.0 * M_PI;
        pcm[i] = opus_int16(32767.0 * env * std::sin(tonePhase_));
    }
    ++toneFrames_;
    if (muted_) return;   // мьют глушит и демо-тон
    encodeAndSend(pcm);
}

void CallEngine::setDeaf(bool deaf) {
    deaf_ = deaf;
    if (spk_) spk_->setVolume(deaf ? 0.0f : 1.0f);
}

// ── Шумоподавление (CAL-04): A/B на живом звонке, состояние в Prefs ─────────

void CallEngine::setNoiseSuppression(bool on) {
    if (on && !denoiser_) {
        denoiser_ = new RnNoise();
        if (!denoiser_->ok()) {           // librnnoise не собралась — тихо выкл
            delete denoiser_;
            denoiser_ = nullptr;
        }
    } else if (!on && denoiser_) {
        delete denoiser_;
        denoiser_ = nullptr;
    }
    Prefs::setBool(QStringLiteral("xipher_call_noise_suppression"), on);
}

// ── Аудио ввод/вывод ──────────────────────────────────────────────────────────

void CallEngine::startAudioIo() {
    if (audioStarted_) return;
    audioStarted_ = true;

    // CAL-04: шумоподавление из настроек (по умолчанию вкл при наличии rnnoise).
    if (Prefs::getBool(QStringLiteral("xipher_call_noise_suppression"), true))
        setNoiseSuppression(true);

    const QAudioFormat fmt = pcmFormat();
    if (testTone_) {
        // Микрофон не нужен вовсе: кадры генерирует таймер (нет акустической
        // петли микрофон→динамики даже при открытых колонках).
        if (toneTimer_ && !toneTimer_->isActive()) toneTimer_->start();
    } else if (!QMediaDevices::defaultAudioInput().isNull()) {
        mic_ = new QAudioSource(QMediaDevices::defaultAudioInput(), fmt, this);
        micIo_ = mic_->start();
        if (micIo_) connect(micIo_, &QIODevice::readyRead, this, &CallEngine::onEncodedFrameReady);
    }
    if (!QMediaDevices::defaultAudioOutput().isNull()) {
        spk_ = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
        spkIo_ = spk_->start();
    }
}

void CallEngine::stopAudioIo() {
    audioStarted_ = false;
    if (denoiser_) { delete denoiser_; denoiser_ = nullptr; }   // CAL-04
    if (mic_) { mic_->stop(); mic_->deleteLater(); mic_ = nullptr; micIo_ = nullptr; }
    if (spk_) { spk_->stop(); spk_->deleteLater(); spk_ = nullptr; spkIo_ = nullptr; }
    capBuf_.clear();
}

// Общий отправщик: PCM-кадр → Opus → RTP.
void CallEngine::encodeAndSend(const void* pcm16) {
    if (!enc_ || !track_) return;
    unsigned char out[4000];
    const int n = opus_encode(enc_, reinterpret_cast<const opus_int16*>(pcm16),
                              kFrame, out, sizeof(out));
    if (n <= 0) return;
    rtpConfig_->timestamp += kFrame;
    try {
        track_->send(reinterpret_cast<const std::byte*>(out), size_t(n));
        ++rtpSent_;
    } catch (...) {}
}

void CallEngine::onEncodedFrameReady() {
    if (!micIo_ || !enc_ || !track_) return;
    capBuf_.append(micIo_->readAll());
    while (capBuf_.size() >= kFrameBytes) {
        QByteArray frame = capBuf_.left(kFrameBytes);
        capBuf_.remove(0, kFrameBytes);
        if (muted_) continue;   // тишину не шлём
        if (denoiser_) denoiser_->process(frame);   // CAL-04: гул → чистый голос
        encodeAndSend(frame.constData());
    }
}

void CallEngine::onIncomingRtp(const QByteArray& rtp) {
    if (!dec_ || !spkIo_ || rtp.size() < 13) return;
    // Минимальный разбор RTP: версия 2, payload после 12 + 4*CSRC байт.
    const auto* d = reinterpret_cast<const unsigned char*>(rtp.constData());
    const int csrc = d[0] & 0x0F;
    int offset = 12 + 4 * csrc;
    if ((d[0] & 0x10) && rtp.size() > offset + 4) {   // extension
        const int extLen = (d[offset + 2] << 8 | d[offset + 3]);
        offset += 4 + extLen * 4;
    }
    if (rtp.size() <= offset) return;

    opus_int16 pcm[kFrame * 2];
    const int samples = opus_decode(dec_,
        reinterpret_cast<const unsigned char*>(d + offset), rtp.size() - offset,
        pcm, kFrame * 2, 0);
    if (samples > 0) {
        spkIo_->write(reinterpret_cast<const char*>(pcm), samples * 2);
        ++rtpReceived_;
    }
}

void CallEngine::hangup() {
    // Снимаем колбэки ДО закрытия — иначе поздний onStateChange/onCandidate из
    // внутреннего потока libdatachannel дёрнет уже освобождённые объекты (краш).
    if (pc_) {
        try {
            pc_->onStateChange(nullptr);
            pc_->onLocalDescription(nullptr);
            pc_->onLocalCandidate(nullptr);
            pc_->onGatheringStateChange(nullptr);
        } catch (...) {}
    }
    if (track_) { try { track_->onMessage(nullptr, nullptr); } catch (...) {} }
    stopAudioIo();
    track_.reset();
    rtpConfig_.reset();
    hasRemoteDesc_ = false;
    pendingCands_.clear();
    if (pc_) { try { pc_->close(); } catch (...) {} pc_.reset(); }
}

#else   // ── XIPHER_HAVE_WEBRTC не задан: сборка без libdatachannel (без звонков) ──

CallEngine::CallEngine(QObject* parent) : QObject(parent) {}
CallEngine::~CallEngine() {}
void CallEngine::setIceServers(const QList<IceServerCfg>&) {}
void CallEngine::startAsCaller() { emit failed(QStringLiteral("Звонки недоступны в этой сборке")); }
void CallEngine::startAsCallee(const QString&) { emit failed(QStringLiteral("Звонки недоступны в этой сборке")); }
void CallEngine::setRemoteAnswer(const QString&) {}
void CallEngine::addRemoteCandidate(const QString&, const QString&) {}
void CallEngine::hangup() {}
void CallEngine::setMuted(bool) {}
void CallEngine::setDeaf(bool) {}
void CallEngine::setTestTone(bool) {}
void CallEngine::sendToneFrame() {}
void CallEngine::encodeAndSend(const void*) {}

#endif
