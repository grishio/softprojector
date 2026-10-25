QT += core network sql testlib
QT -= gui
CONFIG += testcase console c++17
CONFIG -= app_bundle debug_and_release
TEMPLATE = app
TARGET = ai-bible-assistant-tests

SOURCES += tst_aibibleassistant.cpp \
    ../sources/aibiblesettings.cpp \
    ../sources/biblereferenceassistant.cpp \
    ../sources/transcriptstream.cpp \
    ../sources/biblequoteindex.cpp
HEADERS += ../headers/aibiblesettings.hpp \
    ../headers/biblereferenceassistant.hpp \
    ../headers/transcriptstream.hpp \
    ../headers/biblequoteindex.hpp
