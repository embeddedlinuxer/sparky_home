TARGET = Sparky
TEMPLATE = app
VERSION = 0.1.0

QT += gui widgets charts concurrent

SOURCES += src/main.cpp \
    src/mainwindow.cpp \
    3rdparty/qextserialport/qextserialport.cpp	\
    3rdparty/libmodbus/src/modbus.c \
    3rdparty/libmodbus/src/modbus-data.c \
    3rdparty/libmodbus/src/modbus-rtu.c \
    3rdparty/libmodbus/src/modbus-tcp.c \
    3rdparty/libmodbus/src/modbus-ascii.c \
#    src/ipaddressctrl.cpp \
#    src/iplineedit.cpp \
#    src/serialsetting.cpp \
#    src/qcgaugewidget.cpp

HEADERS += src/mainwindow.h \
    src/BatchProcessor.h \
    3rdparty/qextserialport/qextserialport.h \
    3rdparty/qextserialport/qextserialenumerator.h \
    3rdparty/libmodbus/src/modbus.h \
#    src/imodbus.h \
#    src/ipaddressctrl.h \
#    src/iplineedit.h \
#    src/serialsetting.h \
#    src/qcgaugewidget.h

INCLUDEPATH += 3rdparty/libmodbus \
               3rdparty/libmodbus/src \
               3rdparty/qextserialport \
               src
unix {
    SOURCES += 3rdparty/qextserialport/posix_qextserialport.cpp	\
           3rdparty/qextserialport/qextserialenumerator_unix.cpp
    DEFINES += _TTY_POSIX_
}

win32 {
    SOURCES += 3rdparty/qextserialport/win_qextserialport.cpp \
           3rdparty/qextserialport/qextserialenumerator_win.cpp
    DEFINES += _TTY_WIN_  WINVER=0x0501
    LIBS += -lsetupapi -lws2_32
}

FORMS += forms/mainwindow.ui \
    forms/about.ui	\
#    forms/serialsettingswidget.ui \
#    forms/ipaddressctrl.ui \
#    forms/serialsetting.ui

RESOURCES += data/sparky.qrc

RC_FILE += sparky.rc

include(deployment.pri)

DISTFILES +=
