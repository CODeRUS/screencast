TEMPLATE = app
TARGET = screencast

target.path = /usr/sbin
INSTALLS += target

QMAKE_RPATHDIR += /usr/share/$${TARGET}/lib

QT += \
    platformsupport-private \
    network
CONFIG += \
    wayland-scanner \
    link_pkgconfig
PKGCONFIG += \
    wayland-client \
    mlite5 \
    libsystemd-daemon
WAYLANDCLIENTSOURCES += protocol/lipstick-recorder.xml

SOURCES += \
    src/main.cpp \
    src/cast.cpp

HEADERS += \
    src/cast.h

systemd.files = \
    systemd/screencast.service \
    systemd/screencast.socket
systemd.path = /lib/systemd/system/
INSTALLS += systemd

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII

EXTRA_CFLAGS=-W -Wall -Wextra -Wpedantic -Werror=return-type
QMAKE_CXXFLAGS += $$EXTRA_CFLAGS
QMAKE_CFLAGS += $$EXTRA_CFLAGS

isEmpty(PROJECT_PACKAGE_VERSION) {
    VERSION = 0.1.0
} else {
    VERSION = $$PROJECT_PACKAGE_VERSION
}
DEFINES += PROJECT_PACKAGE_VERSION=\\\"$$VERSION\\\"

INCLUDEPATH += /usr/include
INCLUDEPATH += /usr/include/mlite5
