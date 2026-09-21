#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
//  DownloadBar — нижняя панель активной загрузки (как в Telegram): имя файла,
//  процент, скорость, отмена. На уровне чата — переживает смену чата.
// ─────────────────────────────────────────────────────────────────────────────
class DownloadBar : public QWidget {
    Q_OBJECT
public:
    explicit DownloadBar(QWidget* parent = nullptr);

signals:
    void cancelRequested(const QString& path);

protected:
    void timerEvent(QTimerEvent*) override;

private:
    void refresh();

    QLabel*      text_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
};
