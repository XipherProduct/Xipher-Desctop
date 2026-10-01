#!/usr/bin/env bash
# build-appimage.sh — AppImage Xipher Desktop для Linux x86_64.
#
# Ручная сборка AppDir (без linuxdeploy-plugin-qt: у него шаткая поддержка
# Qt6): бинарнь + полное ldd-замыкание библиотек + нужные Qt-плагины
# (xcb-платформа, imageformats, multimedia/ffmpeg-бэкенд, tls) + qt.conf.
# WebRTC (libdatachannel) и Opus-кодек — статически в бинарне; динамические
# libopus/ssl и прочее тянутся в AppDir.
#
# Требования: собранный build-linux/bin/Xipher, mksquashfs (пакет squashfs-tools),
# appimagetool (скачивается автоматически при отсутствии).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build-linux/bin/Xipher"
APPDIR="$ROOT/build-linux/AppDir"
OUT_VERSION="$(grep -oP 'setApplicationVersion\(QStringLiteral\("\K[0-9.]+' "$ROOT/src/main.cpp" || echo dev)"
OUT="$ROOT/dist/Xipher-$OUT_VERSION-x86_64.AppImage"

[ -x "$BIN" ] || { echo "нет $BIN — сначала ninja Xipher" >&2; exit 1; }

echo "── AppImage $OUT_VERSION ──"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/plugins"
mkdir -p "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$BIN" "$APPDIR/usr/bin/Xipher"
strip "$APPDIR/usr/bin/Xipher"

# ── 1. Библиотечное замыкание (ldd рекурсивно, кроме базовых libc/X11) ──────
# Базовые системные (glibc, libGL, X11, dbus, pulse-клиент) оставляем
# хостовым: они есть везде, а втаскивание glibc в AppImage ломает запуск.
SKIP_RE='linux-vdso|ld-linux|libc\.so|libm\.so|libgcc|libpthread|libdl|libstdc\+\+|libGL\.so|libGLX|libOpenGL|libGLdispatch|libEGL|libX11|libxcb|libXau|libXdmcp|libdbus|libsystemd|libpulse|librt|libresolv|libutil|libcap|libexpat|libffi|libpcre2|libmount|libselinux|liblzma|libzstd|libbrotli|libcom_err|libk5crypto|libkrb5|libkeyutils|libgssapi|libwrap|libsasl2|libldap|liblber|libssh|libcrypto\.so|libssl\.so'

copy_closure() {
    local bin="$1"
    ldd "$bin" 2>/dev/null | awk '/=>/ {print $3}' | grep -v '^$' | sort -u | while read -r so; do
        local base; base="$(basename "$so")"
        printf '%s' "$base" | grep -Eq "$SKIP_RE" && continue
        [ -e "$APPDIR/usr/lib/$base" ] && continue
        cp -L "$so" "$APPDIR/usr/lib/$base"
    done
}
copy_closure "$APPDIR/usr/bin/Xipher"

# ── 2. Qt-плагины: только то, что реально грузится ──────────────────────────
QT_PLUGINS="/usr/lib/qt6/plugins"
QT_PLUGIN_DIRS=""
for sub in platforms imageformats iconengines multimedia tls networkinformation; do
    [ -d "$QT_PLUGINS/$sub" ] || continue
    mkdir -p "$APPDIR/usr/plugins/$sub"
    case "$sub" in
        platforms)        # xcb — рабочий стол; offscreen — headless/CI-проверки.
                          for pl in libqxcb libqoffscreen; do
                              [ -f "$QT_PLUGINS/$sub/$pl.so" ] && \
                                  cp "$QT_PLUGINS/$sub/$pl.so" "$APPDIR/usr/plugins/$sub/"
                          done ;;
        multimedia)       cp "$QT_PLUGINS/$sub/"*.so "$APPDIR/usr/plugins/$sub/" ;;   # ffmpeg-бэкенд + эврендеры
        *)                cp "$QT_PLUGINS/$sub/"*.so "$APPDIR/usr/plugins/$sub/" ;;
    esac
    QT_PLUGIN_DIRS="$QT_PLUGIN_DIRS $sub"
done
# Замыкание для плагинов (ffmpeg-бэкенд тянет libav*).
find "$APPDIR/usr/plugins" -name '*.so' | while read -r so; do copy_closure "$so"; done
# libav* не попали в SKIP — но проверим, что ffmpeg-бэкенд на месте:
ls "$APPDIR/usr/plugins/multimedia/" | grep -q ffmpeg || echo "  [!] ffmpeg-медиабэкенд не найден"

# ── 3. qt.conf: пути внутри AppDir ──────────────────────────────────────────
cat > "$APPDIR/usr/bin/qt.conf" <<'EOF'
[Paths]
Plugins = ../plugins
Libraries = ../lib
EOF

# ── 4. Desktop-файл, иконка, AppRun ────────────────────────────────────────
cat > "$APPDIR/usr/share/applications/xipher.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Xipher
GenericName=Messenger
Comment=Xipher Desktop — защищённый мессенджер
Exec=Xipher
Icon=xipher
Terminal=false
Categories=Network;InstantMessaging;
StartupWMClass=Xipher
EOF
cp "$ROOT/resources/icons/xipher.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/xipher.png"
cp "$ROOT/resources/icons/xipher.png" "$APPDIR/xipher.png"
install -Dm644 "$APPDIR/usr/share/applications/xipher.desktop" "$APPDIR/xipher.desktop"

cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
# Wayland-сессии без бандла wayland-плагина уходят на xcb (XWayland) сами.
exec "$HERE/usr/bin/Xipher" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# ── 5. appimagetool → .AppImage ────────────────────────────────────────────
mkdir -p "$ROOT/dist"
TOOL="$ROOT/build-linux/appimagetool-x86_64.AppImage"
if [ ! -x "$TOOL" ]; then
    echo "скачиваю appimagetool…"
    curl -sL --retry 3 -o "$TOOL" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$TOOL"
fi
rm -f "$OUT"
"$TOOL" "$APPDIR" "$OUT"

echo "── Готово: $OUT"
ls -la "$OUT"
