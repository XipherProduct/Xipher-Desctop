#include "net/RnNoise.h"

#if XIPHER_RNNOISE

#include <rnnoise.h>

RnNoise::RnNoise() {
    st_ = rnnoise_create(nullptr);
}

RnNoise::~RnNoise() {
    if (st_) rnnoise_destroy(static_cast<DenoiseState*>(st_));
}

float RnNoise::process(QByteArray& frame) {
    if (!st_ || frame.isEmpty()) return 0.0f;
    auto* ds = static_cast<DenoiseState*>(st_);
    auto* pcm = reinterpret_cast<short*>(frame.data());
    const int samples = frame.size() / 2;
    float vadSum = 0.0f;
    int vadN = 0;
    for (int off = 0; off + 480 <= samples; off += 480) {
        for (int i = 0; i < 480; ++i)
            in_[i] = static_cast<float>(pcm[off + i]);
        const float vad = rnnoise_process_frame(ds, out_, in_);
        for (int i = 0; i < 480; ++i) {
            // Клип: модель изредка выходит за диапазон int16.
            float v = out_[i];
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            pcm[off + i] = static_cast<short>(v);
        }
        vadSum += vad;
        ++vadN;
    }
    return vadN > 0 ? vadSum / vadN : 0.0f;
}

#else

// Без librnnoise (нет пакета / сборка под Windows): прозрачный проход —
// кадры не меняются, шумоподавление заявляется выключенным (ok()=false).
RnNoise::RnNoise() {}
RnNoise::~RnNoise() {}
float RnNoise::process(QByteArray&) { return 0.0f; }

#endif
