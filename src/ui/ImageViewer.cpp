#include "ui/ImageViewer.h"

#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

ImageViewer::ImageViewer(QWidget* parent, const QStringList& paths, int index,
                         const Loader& loader, const Requester& requester)
    : QWidget(parent), paths_(paths), index_(index), loader_(loader), requester_(requester) {
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
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
    update();
}

void ImageViewer::show(QWidget* window, const QPixmap& pm) {
    if (!window || pm.isNull()) return;
    auto* v = new ImageViewer(window, QStringList(), -1, Loader(), Requester());
    v->pm_ = pm;
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

bool ImageViewer::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(obj, e);
}

void ImageViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 225));
    if (!pm_.isNull()) {
        const QSize area = size() * 0.92;
        QPixmap scaled = pm_.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
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
}

void ImageViewer::mousePressEvent(QMouseEvent* e) {
    // Клик по левой/правой трети — листать, по центру — закрыть.
    if (paths_.size() > 1 && e->position().y() < height() * 0.9) {
        if (e->position().x() < width() / 3.0) { prev(); return; }
        if (e->position().x() > width() * 2 / 3.0) { next(); return; }
    }
    close();
}

void ImageViewer::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape: close(); break;
        case Qt::Key_Left: prev(); break;
        case Qt::Key_Right: next(); break;
        default: QWidget::keyPressEvent(e);
    }
}
