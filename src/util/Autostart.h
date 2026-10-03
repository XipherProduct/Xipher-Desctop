#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

// ─────────────────────────────────────────────────────────────────────────────
//  Autostart — «Запускать с системой» (WIN-07): Linux — ~/.config/autostart/
//  xipher.desktop; Windows — ветка Run реестра. macOS — заглушка (нет галки).
// ─────────────────────────────────────────────────────────────────────────────
namespace Autostart {

inline QString desktopFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
         + QStringLiteral("/autostart/xipher.desktop");
}

inline bool isOn() {
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    return !run.value(QStringLiteral("Xipher")).toString().isEmpty();
#else
    return QFile::exists(desktopFilePath());
#endif
}

inline void set(bool on) {
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (on) run.setValue(QStringLiteral("Xipher"),
                         QCoreApplication::applicationFilePath().replace(QLatin1Char('/'), QLatin1Char('\\')));
    else run.remove(QStringLiteral("Xipher"));
#else
    const QString path = desktopFilePath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream ts(&f);
    ts << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=Xipher\n"
       << "Exec=" << QCoreApplication::applicationFilePath().replace(' ', "\\ ") << "\n"
       << "Icon=xipher\n"
       << "Terminal=false\n"
       << "X-GNOME-Autostart-enabled=true\n";
#endif
}

} // namespace Autostart
