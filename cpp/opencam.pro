QT += widgets
CONFIG += c++17 warn_on
TARGET = opencam
TEMPLATE = app

# OpenCV 5 (Arch ships headers under /usr/include/opencv5, no .pc file)
INCLUDEPATH += /usr/include/opencv5
LIBS += -lopencv_core -lopencv_imgproc -lopencv_videoio -lopencv_geometry

SOURCES += \
    src/main.cpp \
    src/filters.cpp \
    src/theme.cpp \
    src/cameras.cpp \
    src/worker.cpp \
    src/histogram.cpp \
    src/videodisplay.cpp \
    src/filterpanel.cpp \
    src/mainwindow.cpp

HEADERS += \
    src/version.h \
    src/filters.h \
    src/theme.h \
    src/cameras.h \
    src/worker.h \
    src/histogram.h \
    src/videodisplay.h \
    src/filterpanel.h \
    src/mainwindow.h
