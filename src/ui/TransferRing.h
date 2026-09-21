#pragma once
#include <QWidget>

// ─────────────────────────────────────────────────────────────────────────────
//  TransferRing — круговой индикатор загрузки вокруг иконки, 1:1 с
//  file-transfer-ring веб-клиента (как в Telegram):
//    IDLE     — стрелка «скачать», трек пуст;
//    LOADING  — крестик «отменить», дуга прогресса;
//    DONE     — иконка файла/действия, трек скрыт.
//  Клик по кольцу запускает/отменяет загрузку или открывает файл.
// ─────────────────────────────────────────────────────────────────────────────
class TransferRing : public QWidget {
    Q_OBJECT
public:
    enum class State { Idle, Loading, Done };

    explicit TransferRing(int size, QWidget* parent = nullptr);

    void setState(State state);
    State state() const { return state_; }
    void setProgress(int percent);        // 0..100
    int  progress() const { return progress_; }
    void setDoneIconFileStyle(bool fileStyle) { doneFileStyle_ = fileStyle; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void drawDownloadIcon(QPainter& p, const QPointF& c);
    void drawCancelIcon(QPainter& p, const QPointF& c);
    void drawFileIcon(QPainter& p, const QPointF& c);

    int  size_;
    int  progress_ = 0;
    bool doneFileStyle_ = true;
    State state_ = State::Idle;
};
