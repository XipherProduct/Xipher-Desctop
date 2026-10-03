cmake_minimum_required(VERSION 3.21)

# ─────────────────────────────────────────────────────────────────────────────
#  Xipher Desktop — фул-нативный клиент на Qt 6 (C++), без webview.
#  Версия 0.1: вход / регистрация 1:1 с веб-дизайном + мини-экран после входа.
# ─────────────────────────────────────────────────────────────────────────────
project(XipherDesktop VERSION 1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

# Qt 6 — Widgets (UI), Network (REST), WebSockets (realtime), Multimedia (голос).
find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets Multimedia MultimediaWidgets)

# ── Шумоподавление rnnoise (CAL-04): системная librnnoise через pkg-config;
# без неё клиент собирается, фича отключается (RnNoise = прозрачный проход).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(RNNOISE rnnoise)
endif()
if(RNNOISE_FOUND)
    message(STATUS "Xipher: rnnoise ${RNNOISE_VERSION} — шумоподавление вкл")
    add_compile_definitions(XIPHER_RNNOISE=1)
else()
    message(STATUS "Xipher: rnnoise НЕ найден — шумоподавление выключено")
endif()

set(SOURCES
    src/main.cpp
    src/app/MainWindow.cpp
    src/net/ApiClient.cpp
    src/net/ChatCache.cpp
    src/net/DownloadCenter.cpp
    src/net/FileCache.cpp
    src/net/MediaServer.cpp
    src/net/DiskStore.cpp
    src/net/Session.cpp
    src/net/WsClient.cpp
    src/net/VoiceRecorder.cpp
    src/net/CallEngine.cpp
    src/net/CallController.cpp
    src/net/RnNoise.cpp
    src/ui/RichDoc.cpp
    src/ui/GiftArt.cpp
    src/ui/GiftArtData.cpp
    src/ui/RichRender.cpp
    src/ui/RichEditor.cpp
    src/ui/ImageEditor.cpp
    src/ui/ChatWindow.cpp
    src/ui/CallOverlay.cpp
    src/ui/CallSounds.cpp
    src/ui/BackgroundWidget.cpp
    src/ui/LoginPage.cpp
    src/ui/RegisterPage.cpp
    src/ui/HomePage.cpp
    src/ui/ChatPage.cpp
    src/ui/LinkPreviewBar.cpp
    src/ui/Stories.cpp
    src/ui/SuperSearchDialog.cpp
    src/ui/DownloadBar.cpp
    src/ui/TransferRing.cpp
    src/ui/VideoMessageWidget.cpp
    src/ui/NewChatDialog.cpp
    src/ui/VoiceMessageWidget.cpp
    src/ui/RecordingBar.cpp
    src/ui/EmojiPicker.cpp
    src/ui/AvatarUtil.cpp
    src/ui/Checklist.cpp
    src/ui/ModalOverlay.cpp
    src/ui/EmptyChatGreeting.cpp
    src/ui/AnimatedEmojiLabel.cpp
    src/ui/ProfilePanel.cpp
    src/ui/ToggleSwitch.cpp
    src/ui/SettingsDialog.cpp
    src/ui/GroupChannelDialogs.cpp
    src/ui/FolderDialog.cpp
    src/ui/ContactsPanel.cpp
    src/ui/PeerInfoPanel.cpp
    src/ui/ChatPickerDialog.cpp
    src/ui/QuickSwitcher.cpp
    src/ui/ImageViewer.cpp
    src/ui/ComposerEdit.cpp
    src/util/QrCode.cpp
    src/util/MprisAdapter.cpp
    src/util/PulseAttenuator.cpp
    src/util/GlobalHotkeys.cpp
)

set(HEADERS
    src/app/MainWindow.h
    src/net/ApiClient.h
    src/net/ChatCache.h
    src/net/DownloadCenter.h
    src/net/FileCache.h
    src/net/MediaServer.h
    src/net/DiskStore.h
    src/net/Session.h
    src/net/Models.h
    src/net/WsClient.h
    src/net/VoiceRecorder.h
    src/net/CallEngine.h
    src/net/CallController.h
    src/ui/CallOverlay.h
    src/ui/GiftArt.h
    src/ui/Theme.h
    src/ui/BackgroundWidget.h
    src/ui/LoginPage.h
    src/ui/RegisterPage.h
    src/ui/HomePage.h
    src/ui/ChatPage.h
    src/ui/LinkPreviewBar.h
    src/ui/Stories.h
    src/ui/SuperSearchDialog.h
    src/ui/DownloadBar.h
    src/ui/TransferRing.h
    src/ui/VideoMessageWidget.h
    src/ui/NewChatDialog.h
    src/ui/VoiceMessageWidget.h
    src/ui/RecordingBar.h
    src/ui/EmojiPicker.h
    src/ui/EmojiNames.h
    src/ui/AvatarUtil.h
    src/ui/Icons.h
    src/ui/Checklist.h
    src/ui/ModalOverlay.h
    src/ui/EmptyChatGreeting.h
    src/ui/AnimatedEmojiLabel.h
    src/ui/ProfilePanel.h
)

# Иконка EXE (windres). Абсолютный путь к .ico в кавычках — переживает пробелы.
if(WIN32)
    set(XIPHER_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/xipher.ico")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    set(APP_RC ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()


qt_add_executable(Xipher
    WIN32                       # GUI-приложение без консоли
    ${SOURCES}
    ${HEADERS}
    resources/resources.qrc
    ${APP_RC}
)

target_include_directories(Xipher PRIVATE src)

target_link_libraries(Xipher PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

if(WIN32)
    target_link_libraries(Xipher PRIVATE dwmapi)   # тёмный заголовок окна (DwmSetWindowAttribute)
endif()

# ── WebRTC (libdatachannel, собран в third_party) для звонков ──────────────────
# Windows: привязка к .dll.a (общий рабочий каталог с виндовой сборкой).
# Linux: статическая сборка third_party/libdatachannel/build-linux (см.
# BUILDING.md; cmake -B build-linux && cmake --build --target datachannel-static).
# Переменные CALL_* применяются и к приложению, и к офлайн-инструментам ниже.
set(LIBDC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdatachannel")
set(CALL_INCLUDES "")
set(CALL_LIBS "")
set(CALL_DEFS "")
if(WIN32 AND EXISTS "${LIBDC_DIR}/build/libdatachannel.dll.a")
    list(APPEND CALL_INCLUDES "${LIBDC_DIR}/include")
    list(APPEND CALL_LIBS "${LIBDC_DIR}/build/libdatachannel.dll.a")
    list(APPEND CALL_DEFS XIPHER_HAVE_WEBRTC=1 RTC_ENABLE_MEDIA=1)
    find_library(OPUS_LIB opus REQUIRED)
    find_path(OPUS_INC opus/opus.h)
    list(APPEND CALL_INCLUDES "${OPUS_INC}" "${OPUS_INC}/opus")
    list(APPEND CALL_LIBS "${OPUS_LIB}")
    message(STATUS "Xipher: WebRTC (libdatachannel/win) + Opus подключены")
elseif(EXISTS "${LIBDC_DIR}/build-linux/libdatachannel-static.a")
    list(APPEND CALL_INCLUDES "${LIBDC_DIR}/include")
    list(APPEND CALL_DEFS XIPHER_HAVE_WEBRTC=1 RTC_ENABLE_MEDIA=1)
    list(APPEND CALL_LIBS
        "${LIBDC_DIR}/build-linux/libdatachannel-static.a"
        "${LIBDC_DIR}/build-linux/deps/usrsctp/usrsctplib/libusrsctp.a"
        "${LIBDC_DIR}/build-linux/deps/libjuice/libjuice-static.a"
        "${LIBDC_DIR}/build-linux/deps/libsrtp/libsrtp2.a")
    find_library(OPUS_LIB opus REQUIRED)
    find_path(OPUS_INC opus/opus.h)
    list(APPEND CALL_INCLUDES ${OPUS_INC})
    list(APPEND CALL_LIBS ${OPUS_LIB} ssl crypto z pthread dl)
    message(STATUS "Xipher: WebRTC (libdatachannel/linux) + Opus подключены")
else()
    message(WARNING "Xipher: libdatachannel не собран — звонки будут отключены")
endif()
target_include_directories(Xipher PRIVATE ${CALL_INCLUDES})
target_link_libraries(Xipher PRIVATE ${CALL_LIBS})
target_compile_definitions(Xipher PRIVATE ${CALL_DEFS})

set_target_properties(Xipher PROPERTIES
    OUTPUT_NAME "Xipher"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ── Offscreen-верификация дизайна (tests/design-verify.cpp) ───────────────────
# Тот же набор источников, что у приложения, но со своим main(): рендерит
# реальный ChatPage без сети и сверяет пиксели/геометрию с CSS веб-клиента.
if(NOT WIN32)
    list(FILTER SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    qt_add_executable(design-verify ${SOURCES} ${HEADERS} tests/design-verify.cpp)
    target_include_directories(design-verify PRIVATE src)
    target_link_libraries(design-verify PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    set_target_properties(design-verify PROPERTIES
        OUTPUT_NAME "design-verify"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Демо-звонок с звуком: два пира в одном процессе, loopback-сигналинг,
    # реальный RTP-эфир (микрофон → динамик), статистика кадров и мьют.
    qt_add_executable(call-demo ${SOURCES} ${HEADERS} tests/call-demo.cpp)
    target_include_directories(call-demo PRIVATE src)
    target_link_libraries(call-demo PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    set_target_properties(call-demo PROPERTIES
        OUTPUT_NAME "call-demo"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Живая проверка звонковой цепочки: TURN → SDP offer → WS → рингтон.
    qt_add_executable(call-live ${SOURCES} ${HEADERS} tests/call-live.cpp)
    target_include_directories(call-live PRIVATE src)
    target_link_libraries(call-live PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    set_target_properties(call-live PROPERTIES
        OUTPUT_NAME "call-live"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Живой рендер профиля на реальных данных (токен из QSettings приложения):
    # те же классы, что в окне, но головой — скриншоты без GUI-автоматизации.
    qt_add_executable(profile-live ${SOURCES} ${HEADERS} tests/profile-live.cpp)
    target_include_directories(profile-live PRIVATE src)
    target_link_libraries(profile-live PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::MultimediaWidgets)
    set_target_properties(profile-live PROPERTIES
        OUTPUT_NAME "profile-live"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Нагрузочный прогон (PRF-02): чат 10к сообщений — открытие/скролл/RSS.
    qt_add_executable(bench-live ${SOURCES} ${HEADERS} tests/bench-live.cpp)
    target_include_directories(bench-live PRIVATE src)
    target_link_libraries(bench-live PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    set_target_properties(bench-live PROPERTIES
        OUTPUT_NAME "bench-live"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Визуальное доказательство каталога подарков (GiftArt): лист 4×4.
    qt_add_executable(gift-grab tests/gift-grab.cpp src/ui/GiftArt.cpp src/ui/GiftArtData.cpp)
    target_include_directories(gift-grab PRIVATE src)
    target_link_libraries(gift-grab PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    set_target_properties(gift-grab PROPERTIES
        OUTPUT_NAME "gift-grab"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Репродуктор бага прокрутки истории: порядок/дыры/якорь при листании вверх.
    qt_add_executable(scroll-repro ${SOURCES} ${HEADERS} tests/scroll-repro.cpp)
    target_include_directories(scroll-repro PRIVATE src)
    target_link_libraries(scroll-repro PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    set_target_properties(scroll-repro PROPERTIES
        OUTPUT_NAME "scroll-repro"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Живой рендер медиавьюера (MDV-01): скрины зума/пана на тестовой картинке.
    qt_add_executable(viewer-grab ${SOURCES} ${HEADERS} tests/viewer-grab.cpp)
    target_include_directories(viewer-grab PRIVATE src)
    target_link_libraries(viewer-grab PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::MultimediaWidgets)
    set_target_properties(viewer-grab PROPERTIES
        OUTPUT_NAME "viewer-grab"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # ASAN-сборка верификатора: гонять в цикле при подозрении на утечки/краши.
    qt_add_executable(dv-asan ${SOURCES} ${HEADERS} tests/design-verify.cpp)
    target_include_directories(dv-asan PRIVATE src)
    target_link_libraries(dv-asan PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::MultimediaWidgets)
    target_compile_options(dv-asan PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(dv-asan PRIVATE -fsanitize=address)
    set_target_properties(dv-asan PROPERTIES
        OUTPUT_NAME "dv-asan"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Звонковая сборка у инструментов та же, что у приложения.
    if(RNNOISE_FOUND)
        foreach(t Xipher design-verify profile-live call-live call-demo viewer-grab bench-live dv-asan scroll-repro)
            target_include_directories(${t} PRIVATE ${RNNOISE_INCLUDE_DIRS})
            target_link_libraries(${t} PRIVATE ${RNNOISE_LIBRARIES})
        endforeach()
    endif()

    find_package(ZLIB REQUIRED)
    target_link_libraries(Xipher PRIVATE ZLIB::ZLIB)
    foreach(tz design-verify profile-live call-live call-demo viewer-grab bench-live dv-asan scroll-repro)
        target_link_libraries(${tz} PRIVATE ZLIB::ZLIB)
    endforeach()

    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(Xipher PRIVATE X11::X11)
        foreach(t design-verify profile-live call-live call-demo viewer-grab bench-live dv-asan scroll-repro)
            target_link_libraries(${t} PRIVATE X11::X11)
        endforeach()
    endif()

    find_package(Qt6Svg QUIET)
    if(Qt6Svg_FOUND)
        message(STATUS "Xipher: Qt6Svg — векторные подарки (GiftArt)")
        add_compile_definitions(XIPHER_QT_SVG=1)
        target_link_libraries(Xipher PRIVATE Qt6::Svg)
        foreach(t design-verify profile-live call-live call-demo viewer-grab bench-live dv-asan scroll-repro gift-grab)
            target_link_libraries(${t} PRIVATE Qt6::Svg)
        endforeach()
    else()
        message(STATUS "Xipher: Qt6Svg НЕ найден — GiftArt через image-плагин/кисть")
    endif()

    find_package(Qt6DBus QUIET)
    if(Qt6DBus_FOUND)
        target_link_libraries(Xipher PRIVATE Qt6::DBus)
        foreach(t design-verify profile-live call-live call-demo viewer-grab bench-live dv-asan scroll-repro)
            target_link_libraries(${t} PRIVATE Qt6::DBus)
        endforeach()
    endif()

    foreach(t design-verify profile-live call-live call-demo viewer-grab bench-live)
        target_include_directories(${t} PRIVATE ${CALL_INCLUDES})
        target_link_libraries(${t} PRIVATE ${CALL_LIBS})
        target_compile_definitions(${t} PRIVATE ${CALL_DEFS})
    endforeach()
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Подпись релиза (опционально). По умолчанию ВЫКЛ — для локальной разработки
#  не нужна. На релизе включается -DXIPHER_SIGN=ON и подставляется сертификат.
#  Бесплатные пути к сертификату: SignPath Foundation (open-source) или
#  Microsoft Store (MSIX). См. docs/SIGNING.md.
# ─────────────────────────────────────────────────────────────────────────────
option(XIPHER_SIGN "Подписывать Xipher.exe через signtool после сборки" OFF)
set(XIPHER_SIGN_PFX  "" CACHE FILEPATH "Путь к .pfx сертификату")
set(XIPHER_SIGN_SHA1 "" CACHE STRING   "Отпечаток (thumbprint) сертификата из хранилища")
set(XIPHER_SIGN_PASS "" CACHE STRING   "Пароль к .pfx (если задан)")
set(XIPHER_TIMESTAMP_URL "http://timestamp.sectigo.com" CACHE STRING "RFC3161 timestamp URL")
set(XIPHER_SIGNTOOL "signtool" CACHE FILEPATH "Путь к signtool.exe (из Windows SDK)")

if(WIN32 AND XIPHER_SIGN)
    set(_sign_args sign /fd SHA256 /tr "${XIPHER_TIMESTAMP_URL}" /td SHA256)
    if(XIPHER_SIGN_PFX)
        list(APPEND _sign_args /f "${XIPHER_SIGN_PFX}")
        if(XIPHER_SIGN_PASS)
            list(APPEND _sign_args /p "${XIPHER_SIGN_PASS}")
        endif()
    elseif(XIPHER_SIGN_SHA1)
        list(APPEND _sign_args /sha1 "${XIPHER_SIGN_SHA1}")
    else()
        message(WARNING "XIPHER_SIGN=ON, но не задан ни XIPHER_SIGN_PFX, ни XIPHER_SIGN_SHA1")
    endif()
    add_custom_command(TARGET Xipher POST_BUILD
        COMMAND "${XIPHER_SIGNTOOL}" ${_sign_args} "$<TARGET_FILE:Xipher>"
        COMMENT "Подпись Xipher.exe (signtool)"
        VERBATIM)
endif()
