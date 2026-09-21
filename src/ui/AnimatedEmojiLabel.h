#pragma once
#include <QLabel>
#include <functional>

class QMovie;

// ─────────────────────────────────────────────────────────────────────────────
//  AnimatedEmojiLabel — эмодзи с анимацией (Google Noto Animated Emoji, Apache-2.0).
//  Показывает статичный символ-плейсхолдер, по требованию подгружает GIF с CDN
//  (дисковый кэш) и проигрывает: autoplay=true — сразу (крупные эмодзи в сообщении),
//  иначе по наведению (сетка пикера). Клик → onClick(символ).
// ─────────────────────────────────────────────────────────────────────────────
class AnimatedEmojiLabel : public QLabel {
public:
    AnimatedEmojiLabel(const QString& codepoints, int size, bool autoplay, QWidget* parent = nullptr);

    void setOnClick(std::function<void(const QString&)> cb) { onClick_ = std::move(cb); }

    // "😀" → "1f600", "❤️" → "2764_fe0f" (разделитель Noto — подчёркивание).
    static QString emojiToCodepoints(const QString& emoji);

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    void play();
    void stop();
    void startMovie(const QString& path);

    QString cp_;
    QString ch_;
    int     size_;
    bool    autoplay_;
    QMovie* movie_ = nullptr;
    std::function<void(const QString&)> onClick_;
};
