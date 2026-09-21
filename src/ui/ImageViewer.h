#pragma once
#include <QWidget>
#include <QPixmap>
#include <QStringList>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  ImageViewer — просмотр фото на весь экран (оверлей внутри окна): затемнённый
//  фон, картинка по центру, закрытие по клику/Esc. Режим галереи: список путей
//  всех медиа чата, навигация ←/→, счётчик «i / N» (как в Telegram).
// ─────────────────────────────────────────────────────────────────────────────
class ImageViewer : public QWidget {
    Q_OBJECT
public:
    // Одиночное фото.
    static void show(QWidget* window, const QPixmap& pm);
    // Галерея: paths — медиа чата (серверные /files/... пути), index — старт.
    // loader(path) отдаёт байты (из кэша мгновенно; может вернуть пусто).
    using Loader = std::function<QByteArray(const QString&)>;
    using Requester = std::function<void(const QString&)>;   // подтянуть байты (сеть/кэш)
    static void showGallery(QWidget* window, const QStringList& paths, int index,
                            const Loader& loader, const Requester& requester = Requester());

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    explicit ImageViewer(QWidget* parent, const QStringList& paths, int index,
                         const Loader& loader, const Requester& requester);
    void loadCurrent();
    void next();
    void prev();

    QStringList paths_;
    int index_ = 0;
    Loader loader_;
    Requester requester_;
    int tries_ = 0;
    QPixmap pm_;
};
