// viewer-grab.cpp — живой рендер медиавьюера (MDV-01): зум к курсору, пан,
// двойной клик. Тестовая картинка генерируется (градиент + сетка + метки),
// скриншоты складываются рядом с бинарем: viewer-fit.png / viewer-zoom.png /
// viewer-pan.png. Работает и offscreen: снимки делает QWidget::grab().
//
// Запуск: ./build-linux/bin/viewer-grab  → выход 0, три PNG на месте.

#include "ui/ImageViewer.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

static QPixmap makeTestImage() {
    QPixmap pm(1200, 800);
    QPainter p(&pm);
    QLinearGradient g(0, 0, 1200, 800);
    g.setColorAt(0.0, QColor(139, 92, 246));
    g.setColorAt(0.5, QColor(58, 45, 92));
    g.setColorAt(1.0, QColor(19, 18, 24));
    p.fillRect(pm.rect(), g);
    p.setPen(QColor(255, 255, 255, 40));
    for (int x = 0; x <= 1200; x += 100) p.drawLine(x, 0, x, 800);
    for (int y = 0; y <= 800; y += 100) p.drawLine(0, y, 1200, y);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(28);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("Xipher viewer-grab"));
    f.setPixelSize(14);
    f.setBold(false);
    p.setFont(f);
    p.drawText(QRect(0, 700, 1200, 60), Qt::AlignHCenter,
               QStringLiteral("100px сетка — видно кратность зума"));
    return pm;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherDesignTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ViewerGrab"));

    QWidget host;
    host.resize(900, 640);
    host.show();

    const QPixmap img = makeTestImage();
    ImageViewer::show(&host, img);
    ImageViewer* viewer = host.findChild<ImageViewer*>();
    if (!viewer) { fprintf(stderr, "viewer not found\n"); return 1; }
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    const QString outDir = QCoreApplication::applicationDirPath();

    // 1) «Вписать» — исходное состояние.
    viewer->grab().save(outDir + QStringLiteral("/viewer-fit.png"));

    // 2) Зум к точке (200,200) × несколько шагов — точка остаётся под курсором.
    for (int i = 0; i < 5; ++i) {
        QWheelEvent w(QPointF(200, 200), QPointF(200, 200), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewer, &w);
    }
    QCoreApplication::processEvents();
    viewer->grab().save(outDir + QStringLiteral("/viewer-zoom.png"));

    // 3) Пан влево-вниз средней кнопкой.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(450, 320), QPointF(450, 320),
                      Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(250, 220), QPointF(250, 220),
                     Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &move);
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(250, 220), QPointF(250, 220),
                    Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &rel);
    QCoreApplication::processEvents();
    viewer->grab().save(outDir + QStringLiteral("/viewer-pan.png"));

    printf("fit=%.0f%% zoom=%.0f%% pan=(%d,%d) mode=%s\n",
           100.0 * 900.0 * 0.92 / 1200.0 > 100.0 * 640.0 * 0.92 / 800.0
               ? 100.0 * 640.0 * 0.92 / 800.0 : 100.0 * 900.0 * 0.92 / 1200.0,
           viewer->zoomPercent(), viewer->panOffset().x(), viewer->panOffset().y(),
           viewer->isFitMode() ? "fit" : "manual");
    printf("PNG: %s/viewer-{fit,zoom,pan}.png\n", outDir.toUtf8().constData());
    return 0;
}
