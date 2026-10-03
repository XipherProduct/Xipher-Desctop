#include "ui/ImageViewer.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <QDrag>
#include <QMimeData>

ImageViewer::ImageViewer(QWidget* parent, const QStringList& paths, int index,
                         const Loader& loader, const Requester& requester)
    : QWidget(parent), paths_(paths), index_(index), loader_(loader), requester_(requester) {
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);   // картину можно тащить
    if (parent) {
        setGeometry(parent->rect());
        parent->installEventFilter(this);
    }
    loadCurrent();
}

void ImageViewer::loadCurrent() {
    pm_ = QPixmap();
    if (index_ < 0 || index_ >= paths_.size()) return;
    const QString path = paths_[index_];
    QByteArray bytes;
    if (loader_) bytes = loader_(path);
    if (bytes.isEmpty() && path.startsWith(QLatin1Char('/'))) {
        QFile f(path);   // локальный файл (только что отправленное)
        if (f.open(QIODevice::ReadOnly)) bytes = f.readAll();
    }
    if (!bytes.isEmpty()) {
        pm_.loadFromData(bytes);
        tries_ = 0;
    } else if (requester_ && ++tries_ <= 30) {
        // Медиа ещё не в кэше: просим подтянуть и перерисуем, когда придёт.
        requester_(path);
        QMetaObject::invokeMethod(this, &ImageViewer::loadCurrent, Qt::QueuedConnection);
        QTimer::singleShot(600, this, &ImageViewer::loadCurrent);
        return;
    }
    resetZoom();   // новый кадр всегда открывается «вписанным»
    update();
}

void ImageViewer::show(QWidget* window, const QPixmap& pm) {
    if (!window || pm.isNull()) return;
    auto* v = new ImageViewer(window, QStringList(), -1, Loader(), Requester());
    v->pm_ = pm;
    v->resetZoom();
    v->QWidget::show();
    v->raise();
    v->setFocus();
}

void ImageViewer::showGallery(QWidget* window, const QStringList& paths, int index,
                              const Loader& loader, const Requester& requester) {
    if (!window || paths.isEmpty()) return;
    auto* v = new ImageViewer(window, paths, qBound(0, index, paths.size() - 1), loader, requester);
    v->QWidget::show();
    v->raise();
    v->setFocus();
}

void ImageViewer::next() {
    if (paths_.isEmpty()) { close(); return; }
    index_ = (index_ + 1) % paths_.size();
    loadCurrent();
}

void ImageViewer::prev() {
    if (paths_.isEmpty()) { close(); return; }
    index_ = (index_ - 1 + paths_.size()) % paths_.size();
    loadCurrent();
}

// ── Зум/пан (MDV-01) ───────────────────────────────────────────────────────

qreal ImageViewer::fitFactor() const {
    if (pm_.isNull()) return 1.0;
    const qreal w = qMax<qreal>(width() * 0.92, 1.0);
    const qreal h = qMax<qreal>(height() * 0.92, 1.0);
    return qMin(w / pm_.width(), h / pm_.height());
}

void ImageViewer::resetZoom() {
    fitMode_ = true;
    zoom_ = fitFactor();
    panX_ = panY_ = 0.0;
    update();
}

void ImageViewer::applyZoom(qreal newZoom, const QPointF& anchor) {
    newZoom = qBound(0.1, newZoom, 10.0);   // 10%..1000%
    if (pm_.isNull()) { zoom_ = newZoom; return; }
    // Точка картинки под курсором остаётся под курсором.
    const QRectF img = QRectF(QPointF((width() - pm_.width() * zoom_) / 2.0 + panX_,
                                      (height() - pm_.height() * zoom_) / 2.0 + panY_),
                              pm_.size() * zoom_);
    const QPointF under = QPointF((anchor.x() - img.left()) / zoom_,
                                  (anchor.y() - img.top()) / zoom_);
    zoom_ = newZoom;
    fitMode_ = false;
    panX_ = anchor.x() - under.x() * zoom_ - (width() - pm_.width() * zoom_) / 2.0;
    panY_ = anchor.y() - under.y() * zoom_ - (height() - pm_.height() * zoom_) / 2.0;
    clampPan();
    update();
}

void ImageViewer::clampPan() {
    if (pm_.isNull()) return;
    const qreal iw = pm_.width() * zoom_, ih = pm_.height() * zoom_;
    const qreal cx = (width() - iw) / 2.0, cy = (height() - ih) / 2.0;
    if (iw <= width())  panX_ = 0.0;    // вписана по ширине — не двигаем
    else panX_ = qBound(cx - (iw - width()), panX_, cx);   // край всегда в кадре
    if (ih <= height()) panY_ = 0.0;
    else panY_ = qBound(cy - (ih - height()), panY_, cy);
}

void ImageViewer::wheelEvent(QWheelEvent* e) {
    if (pm_.isNull()) { QWidget::wheelEvent(e); return; }
    e->accept();
    const qreal step = e->angleDelta().y() > 0 ? 1.25 : (1.0 / 1.25);
    applyZoom(zoom_ * step, e->position());
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || pm_.isNull()) { QWidget::mouseDoubleClickEvent(e); return; }
    e->accept();
    if (fitMode_) applyZoom(1.0, e->position());   // «вписать» → 100%
    else resetZoom();                              // увеличено → «вписать»
}

void ImageViewer::mousePressEvent(QMouseEvent* e) {
    pressPos_ = e->position();
    // Средняя кнопка — пан всегда; левая — только когда картинка увеличена.
    if ((e->button() == Qt::MiddleButton) || (e->button() == Qt::LeftButton && !fitMode_)) {
        panning_ = true;
        panPressPos_ = e->globalPosition().toPoint();
        panPressX_ = panX_;
        panPressY_ = panY_;
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void ImageViewer::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->globalPosition().toPoint() - panPressPos_;
        panX_ = panPressX_ + d.x();
        panY_ = panPressY_ + d.y();
        clampPan();
        update();
        e->accept();
        return;
    }
    // MDV-03: drag-out — тащим картинку из вьюера в проводник/мессенджер.
    // Работает в «вписать»-режиме (в увеличенном ЛКМ занята паном): тянем
    // за пределы окна — отдаём temp-файл через QMimeData(file://).
    if (e->buttons() & Qt::LeftButton && fitMode_ && !pm_.isNull() && !paths_.isEmpty()) {
        const QPointF d = e->position() - pressPos_;
        if (qAbs(d.x()) > 12 || qAbs(d.y()) > 12) {
            const QPoint g = e->globalPosition().toPoint();
            if (!rect().contains(mapFromGlobal(g))) {
                const QString file = ensureDragFile();
                if (!file.isEmpty()) {
                    auto* drag = new QDrag(this);
                    auto* md = new QMimeData;
                    md->setUrls({QUrl::fromLocalFile(file)});
                    drag->setMimeData(md);
                    drag->setPixmap(pm_.scaled(120, 120, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
                    drag->exec(Qt::CopyAction);
                    return;
                }
            }
        }
    }
    QWidget::mouseMoveEvent(e);
}

// MDV-03: кадр → temp-файл (имя из пути/дат), для драг-аута и теста.
QString ImageViewer::ensureDragFile() {
    if (pm_.isNull()) return QString();
    QString name = paths_.isEmpty() ? QString() : QFileInfo(paths_[index_]).fileName();
    if (name.isEmpty()) name = QStringLiteral("photo.png");
    const QString file = QDir::tempPath() + QStringLiteral("/xipher_drag_") + name;
    if (!pm_.save(file)) return QString();
    return file;
}

void ImageViewer::mouseReleaseEvent(QMouseEvent* e) {
    if (panning_ && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        panning_ = false;
        setCursor(Qt::OpenHandCursor);
        e->accept();
        return;
    }
    // Клик по левой/правой трети — листать, по центру — закрыть.
    // После пана левой (сдвиг > 6px) это уже не клик — не листаем и не закрываем.
    const QPointF d = e->position() - pressPos_;
    if (e->button() == Qt::LeftButton && (qAbs(d.x()) > 6 || qAbs(d.y()) > 6)) { e->accept(); return; }
    if (e->button() == Qt::LeftButton && paths_.size() > 1
            && e->position().y() < height() * 0.9) {
        if (e->position().x() < width() / 3.0) { prev(); return; }
        if (e->position().x() > width() * 2 / 3.0) { next(); return; }
    }
    if (e->button() == Qt::LeftButton) close();
}

bool ImageViewer::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        // Окно изменило размер: «вписать» пересчитывается, ручной зум держим,
        // только не даём картинке вылезти за края.
        if (fitMode_) zoom_ = fitFactor();
        clampPan();
        update();
    }
    return QWidget::eventFilter(obj, e);
}

void ImageViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 225));
    if (!pm_.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate((width() - pm_.width() * zoom_) / 2.0 + panX_,
                    (height() - pm_.height() * zoom_) / 2.0 + panY_);
        p.scale(zoom_, zoom_);
        p.drawPixmap(0, 0, pm_);
        p.resetTransform();
    } else {
        p.setPen(QColor(255, 255, 255, 160));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Загрузка…"));
    }
    if (paths_.size() > 1) {
        p.setPen(QColor(255, 255, 255, 180));
        const QString counter = QStringLiteral("%1 / %2").arg(index_ + 1).arg(paths_.size());
        p.drawText(QRect(0, height() - 44, width(), 24), Qt::AlignHCenter, counter);
        // Стрелки-подсказки по краям.
        p.setFont(font());
        p.setPen(QColor(255, 255, 255, 120));
        p.drawText(QRect(16, 0, 60, height()), Qt::AlignVCenter, QStringLiteral("‹"));
        p.drawText(QRect(width() - 76, 0, 60, height()), Qt::AlignVCenter, QStringLiteral("›"));
    }
    // Индикатор зума (не показываем в исходном «вписать»).
    if (!fitMode_ && !pm_.isNull()) {
        p.setPen(QColor(255, 255, 255, 200));
        p.setFont(font());
        p.drawText(QRect(0, 12, width(), 20), Qt::AlignHCenter,
                   QStringLiteral("%1%").arg(qRound(zoom_ * 100.0)));
    }
}

void ImageViewer::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape: close(); break;
        case Qt::Key_Left: prev(); break;
        case Qt::Key_Right: next(); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            applyZoom(zoom_ * 1.25, QPointF(width() / 2.0, height() / 2.0)); break;
        case Qt::Key_Minus:
            applyZoom(zoom_ / 1.25, QPointF(width() / 2.0, height() / 2.0)); break;
        case Qt::Key_0: resetZoom(); break;
        default: QWidget::keyPressEvent(e);
    }
}
