#include <QApplication>
#include <QSettings>
#include <QFont>
#include <QPalette>
#include <QIcon>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QSysInfo>
#include <QMutex>

#include "app/MainWindow.h"
#include "ui/Theme.h"
#include "net/MediaServer.h"
#include "net/Session.h"
#include <QLocalServer>
#include <QLocalSocket>
#include "net/ApiClient.h"

// Лог в файл рядом с exe (xipher.log) — GUI-приложение не имеет консоли,
// поэтому диагностика (в т.ч. звонки [call] …) пишется сюда.
static void logToFile(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/xipher.log"));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        const char* lvl = type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "ERR " : "INFO";
        QTextStream(&f) << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
                        << ' ' << lvl << ' ' << msg << '\n';
    }
}

// Ротация: лог пишется всегда и растёт неограниченно; при старте жирный
// файл уезжает в .1 (хранится одно поколение), текущий начинается заново.
static void rotateLog() {
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/xipher.log");
    QFileInfo fi(path);
    if (!fi.exists() || fi.size() < 2 * 1024 * 1024) return;
    const QString prev = path + QStringLiteral(".1");
    if (QFile::exists(prev)) QFile::remove(prev);
    QFile::rename(path, prev);
}

// PRF-01: аппаратный рендер — до создания QApplication (Prefs недоступен
// до org, читаем QSettings напрямую).
static bool hwRenderPref()
{
    QSettings s(QStringLiteral("Xipher"), QStringLiteral("Desktop"));
    return s.value(QStringLiteral("xipher_hw_render"), false).toBool();
}

int main(int argc, char** argv) {
    // PRF-01: софтверный фолббек до создания QApplication (галка в настройках).
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL, !hwRenderPref());
    QApplication app(argc, argv);

    qInstallMessageHandler(logToFile);
    rotateLog();
    qInfo().noquote() << "старт:" << QCoreApplication::applicationVersion()
                      << QSysInfo::productVersion() << "qt" << QT_VERSION_STR
                      << QCoreApplication::applicationDirPath();

    // Имена для QSettings (HKCU\Software\Xipher\Desktop).
    QApplication::setOrganizationName(QStringLiteral("Xipher"));
    QApplication::setApplicationName(QStringLiteral("Desktop"));
    QApplication::setApplicationDisplayName(QStringLiteral("Xipher"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/xipher.png")));

    // Базовый шрифт. Inter если установлен в системе, иначе Segoe UI.
    QFont base(QStringLiteral("Inter"));
    base.setStyleStrategy(QFont::PreferAntialias);
    if (base.exactMatch() == false) base.setFamily(QStringLiteral("Segoe UI"));
    base.setPixelSize(14);
    app.setFont(base);

    // Тёмная палитра ДО создания окна — иначе при старте мелькает белый фон,
    // пока не отрисуется тёмный градиент (классическая «вспышка» Qt на Windows).
    QPalette pal = app.palette();
    pal.setColor(QPalette::Window,     Theme::BgBase);
    pal.setColor(QPalette::Base,       Theme::BgBase);
    pal.setColor(QPalette::WindowText, Theme::TextMain);
    pal.setColor(QPalette::Text,       Theme::TextMain);
    app.setPalette(pal);

    // Глобальный стиль (палитра/QSS из веб-клиента).
    app.setStyleSheet(Theme::styleSheet());

    // Один экземпляр приложения: второй запуск просто завершается.
    QLocalSocket guard;
    guard.connectToServer(QStringLiteral("xipher-desktop-instance"));
    if (guard.waitForConnected(300)) {
        fprintf(stderr, "Xipher Desktop уже запущен\n");
        return 0;
    }
    QLocalServer::removeServer(QStringLiteral("xipher-desktop-instance"));
    QLocalServer* instanceServer = new QLocalServer(&app);
    instanceServer->listen(QStringLiteral("xipher-desktop-instance"));

    // Стриминг медиа: локальный Range-прокси подставляет токен к /files/*.
    MediaServer::instance().setRemoteBase(QStringLiteral("https://messenger.xipher.pro"));
    MediaServer::instance().setAuthProvider([] {
        return Session::instance().token.toUtf8();
    });

    MainWindow w;
    w.show();
    return app.exec();
}
