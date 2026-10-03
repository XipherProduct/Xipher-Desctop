#pragma once
#include <QByteArray>

// ─────────────────────────────────────────────────────────────────────────────
//  RnNoise — шумоподавление микрофона (CAL-04, Krisp-аналог на RNN).
//  Кадр int16/48кГц/моно прогоняется через rnnoise (модель 10 мс = 480
//  сэмплов). Сборка без librnnoise превращает класс в прозрачный проход
//  (фича за флагом сборки XIPHER_RNNOISE, откат — выкинуть пакет).
// ─────────────────────────────────────────────────────────────────────────────
class RnNoise {
public:
    RnNoise();
    ~RnNoise();
    RnNoise(const RnNoise&) = delete;
    RnNoise& operator=(const RnNoise&) = delete;

    bool ok() const { return st_ != nullptr; }
    // Обработать кадр (кратен 480 сэмплам; типовой звонковый кадр 960).
    // Возвращает среднюю вероятность голоса (VAD) 0..1 этого кадра.
    float process(QByteArray& frameInt16);

private:
    void* st_ = nullptr;   // RNNState* (скрыт, чтобы не тащить заголовок)
    float in_[480];
    float out_[480];
};
