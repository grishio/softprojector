QT += widgets network sql testlib printsupport
CONFIG += testcase console c++17
CONFIG -= app_bundle debug_and_release
TEMPLATE = app
TARGET = openai-translation-tests

SOURCES += tst_openaitranslation.cpp \
    ../sources/openaisettings.cpp \
    ../sources/songtranslation.cpp \
    ../sources/openaiclient.cpp \
    ../sources/openaisettingswidget.cpp \
    ../sources/songtranslationdialog.cpp \
    ../sources/song.cpp \
    ../sources/bible.cpp \
    ../sources/announcement.cpp \
    ../sources/settings.cpp \
    ../sources/spfunctions.cpp \
    ../sources/editwidget.cpp \
    ../sources/addsongbookdialog.cpp \
    ../sources/highlight.cpp \
    ../sources/printpreviewdialog.cpp
HEADERS += ../headers/openaiclient.hpp \
    ../headers/song.hpp \
    ../headers/announcement.hpp \
    ../headers/openaisettingswidget.hpp \
    ../headers/songtranslationdialog.hpp \
    ../headers/editwidget.hpp \
    ../headers/addsongbookdialog.hpp \
    ../headers/highlight.hpp \
    ../headers/printpreviewdialog.hpp
FORMS += ../ui/editwidget.ui \
    ../ui/addsongbookdialog.ui \
    ../ui/printpreviewdialog.ui
