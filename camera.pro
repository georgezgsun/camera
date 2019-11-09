TEMPLATE = app
CONFIG += console \
            c++11 \
            link_pkgconfig
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp \
    ffrecording.cpp

HEADERS += \
    ffmpeg.h

LIBS += -lavformat \
        -lavcodec \
        -lswscale \
        -lavutil \
        -lavdevice \
        -lswresample \
        -lpthread
