#-------------------------------------------------
#
# Project created by QtCreator 2014-05-10T15:06:55
#
#-------------------------------------------------

QT       += core network

QT       -= gui

TARGET = radeon-profile-daemon
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += main.cpp \
    rpdthread.cpp

HEADERS += \
    rpdthread.h
