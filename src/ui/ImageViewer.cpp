#include "ui/ImageViewer.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QPushButton>
#include <QWheelEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QUrl>
#include <QSaveFile>
#include <QCloseEvent>
#include <QMessageBox>
#include <QTemporaryDir>

ImageViewer::ImageViewer(QWidget* parent, const QStringList& paths, int index,
                         const Loader& loader, const Requester& requester)
    : QWidget(parent), paths_(paths), index_(index), loader_(loader), requester_(requester) {
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);   // картину можно тащить
    // WIN-05: кнопка «⧉ в окно» — вьюер выносится в отдельное окно
    // (поверх всех), клик по фону больше не закрывает.
    winBtn_ = new QPushButton(QStringLiteral("⧉"), this);
    winBtn_->setToolTip(QStringLiteral("В отдельное окно, поверх всех"));
    winBtn_->setCursor(Qt::PointingHandCursor);
    winBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(255,255,255,14%);border:none;border-radius:16px;"
        "color:#fff;font-size:15px;min-width:32px;min-height:32px;}"
        "QPushButton:hover{background:rgba(255,255,255,28%);}"));
    connect(winBtn_, &QPushButton::clicked, this, [this]() {
        setWindowFlag(Qt::Window, true);
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setWindowTitle(QStringLiteral("Xipher — просмотр"));
        resize(parentWidget() ? parentWidget()->size() * 9 / 10 : QSize(900, 700));
        detached_ = true;
        showNormal();
    });
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
    // MDV-02: горизонтальное колесо/тачпад-свайп — листание галереи.
    if (qAbs(e->angleDelta().x()) > qAbs(e->angleDelta().y())) {
        e->accept();
        if (e->angleDelta().x() < 0) next(); else prev();
        return;
    }
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

// Контекстное меню просмотрщика (как в Telegram/Discord): правый клик по
// открытому фото больше не «мёртвый» — те же действия, что и в чате.
void ImageViewer::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    QAction* copyAct = menu.addAction(QString::fromUtf8("Копировать изображение"));
    QAction* saveAct = menu.addAction(QString::fromUtf8("Сохранить как…"));
    QAction* openAct = menu.addAction(QString::fromUtf8("Открыть в системе"));
    menu.addSeparator();
    QAction* closeAct = menu.addAction(QString::fromUtf8("Закрыть просмотр"));
    QAction* chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if (chosen == copyAct) {
        if (!pm_.isNull()) QApplication::clipboard()->setPixmap(pm_);
        return;
    }
    if (chosen == saveAct) {
        // Оригинальные байты (качество не теряем), фолбэк — что на экране.
        // Оригинальные байты (качество не теряем); если кадр ещё не скачан —
        // просим загрузку и сохраняем экранную копию с честным предупреждением.
        const QString path = paths_.value(index_);
        QByteArray bytes;
        if (!path.isEmpty() && loader_) bytes = loader_(path);
        if (bytes.isEmpty() && !path.isEmpty() && requester_) requester_(path);
        QString suggested = QFileInfo(path).fileName();
        if (suggested.isEmpty()) suggested = QStringLiteral("photo.png");
        const QString dest = QFileDialog::getSaveFileName(
            this, QString::fromUtf8("Сохранить"), QDir::homePath() + QLatin1Char('/') + suggested);
        if (dest.isEmpty()) return;
        if (!bytes.isEmpty()) {
            QSaveFile out(dest);
            if (out.open(QIODevice::WriteOnly)) { out.write(bytes); out.commit(); }
        } else if (pm_.save(dest)) {
            QMessageBox::information(this, QString::fromUtf8("Сохранено"),
                QString::fromUtf8("Оригинал ещё не загружен — сохранена копия с экрана."));
        }
        return;
    }
    if (chosen == openAct) {
        const QString tmp = ensureDragFile();
        if (!tmp.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(tmp));
        return;
    }
    if (chosen == closeAct) close();
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
    // Оригинальные байты из кэша (loader_) — качество и формат без потерь;
    // экранная копия — только если оригинала ещё нет.
    QByteArray bytes;
    if (index_ >= 0 && index_ < paths_.size() && loader_)
        bytes = loader_(paths_[index_]);
    QString name = paths_.isEmpty() ? QString() : QFileInfo(paths_[index_]).fileName();
    if (name.isEmpty()) name = QStringLiteral("photo.png");
    if (!name.contains(QLatin1Char('.'))) {
        name += bytes.startsWith("\x89PNG") ? QStringLiteral(".png")
                                            : QStringLiteral(".jpg");
    }
    const QString file = QDir::tempPath()
        + QStringLiteral("/xipher_view_") + QString::number(qHash(name)) + QLatin1Char('_') + name;
    bool ok = false;
    if (!bytes.isEmpty()) {
        QFile f(file);
        ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    } else {
        ok = pm_.save(file);
    }
    if (!ok) return QString();
    if (!tempFiles_.contains(file)) tempFiles_.append(file);
    return file;
}

// Temp-копии живут только пока открыт вьюер — снаружи файл не нужен.
void ImageViewer::cleanupTempFiles() {
    for (const QString& f : std::as_const(tempFiles_)) QFile::remove(f);
    tempFiles_.clear();
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
    if (e->button() == Qt::LeftButton && !detached_) close();
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

void ImageViewer::closeEvent(QCloseEvent* e) {
    cleanupTempFiles();
    QWidget::closeEvent(e);
}

void ImageViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 225));
    if (winBtn_) winBtn_->move(width() - 96, 12);
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
