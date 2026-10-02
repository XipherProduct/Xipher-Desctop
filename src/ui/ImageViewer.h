#pragma once
#include <QWidget>
#include <QPixmap>
#include <QStringList>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  ImageViewer — просмотр фото на весь экран (оверлей внутри окна): затемнённый
//  фон, картинка по центру, закрытие по клику/Esc. Режим галереи: список путей
//  всех медиа чата, навигация ←/→, счётчик «i / N» (как в Telegram).
//
//  Зум и панорамирование (MDV-01, как в Telegram 6.7.8):
//   - колесо — зум к курсору 10%..1000%;
//   - средняя кнопка (и левая в увеличенном состоянии) — перетаскивание;
//   - двойной клик — переключение «вписать ↔ 100%»;
//   - смена кадра сбрасывает зум обратно в «вписать».
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

    // Тестовые швы (design-verify): текущий зум в процентах и смещение пана.
    qreal zoomPercent() const { return zoom_ * 100.0; }
    bool isFitMode() const { return fitMode_; }
    QPoint panOffset() const { return QPoint(qRound(panX_), qRound(panY_)); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    explicit ImageViewer(QWidget* parent, const QStringList& paths, int index,
                         const Loader& loader, const Requester& requester);
    void loadCurrent();
    void next();
    void prev();
    void resetZoom();                       // «вписать» — исходное состояние
    void applyZoom(qreal newZoom, const QPointF& anchor);   // зум к точке курсора
    void clampPan();                        // картинка не улетает за край
    qreal fitFactor() const;                // масштаб «вписать» для текущего кадра

    QStringList paths_;
    int index_ = 0;
    Loader loader_;
    Requester requester_;
    int tries_ = 0;
    QPixmap pm_;

    // Зум/пан: zoom_ — множитель относительно натуральных пикселей,
    // fitMode_ — «вписать» (пан выключен), иначе ручной масштаб.
    qreal zoom_ = 1.0;
    bool  fitMode_ = true;
    qreal panX_ = 0.0, panY_ = 0.0;
    bool  panning_ = false;          // сейчас тащим картинкой
    QPoint panPressPos_;
    qreal panPressX_ = 0.0, panPressY_ = 0.0;
    QPointF pressPos_;               // где началось нажатие (клик vs драг)
};
