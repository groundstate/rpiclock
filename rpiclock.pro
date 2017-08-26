HEADERS       = TimeDisplay.h PowerManager.h
SOURCES       = TimeDisplay.cpp \
                Main.cpp \
								PowerManager.cpp
QT           += core gui network xml
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG      += debug
#DEFINES      += QT_NO_DEBUG_OUTPUT
DEFINES      += DEBUG