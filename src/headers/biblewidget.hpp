/***************************************************************************
//
//    softProjector - an open source media projection software
//    Copyright (C) 2017  Vladislav Kobzar
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation version 3 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
***************************************************************************/

#ifndef BIBLEWIDGET_HPP
#define BIBLEWIDGET_HPP

#include <QWidget>
#include <QButtonGroup>
#include <QtGui>
#include "bible.hpp"
#include "highlight.hpp"
#include "settings.hpp"
#include "aibiblesettings.hpp"
#include "biblereferenceassistant.hpp"

class QCheckBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class TranscriptStream;

namespace Ui {
class BibleWidget;
}

class BibleWidget : public QWidget, public BibleSelectionProvider {
    Q_OBJECT
    Q_DISABLE_COPY(BibleWidget)
public:
    explicit BibleWidget(QWidget *parent = 0);
    virtual ~BibleWidget();
    Bible bible;
    QString getCurrentBook();

    // For optimization:
    QString currentBook;
    int currentChapter;
    QStringList currentChapterList;

    int getCurrentChapter();

    // BibleSelectionProvider (AI Bible assistant)
    QList<BibleBookInfo> assistantBooks() const override;
    int assistantVerseCount(const QString &book, int chapter) const override;
    int assistantMaxVerses() const override;
    QList<VerseCandidate> assistantQuoteCandidates(const QString &text) const override;
    BibleSelection assistantSelection() const override;
    bool assistantPaused() const override;

public slots:
    QByteArray getHiddenSplitterState();
    QByteArray getShownSplitterState();
    void setHiddenSplitterState(QByteArray& state);
    void setShownSplitterState(QByteArray& state);
    void loadBibles(QString initialId);
    void sendToProjector(bool add_to_history);
    void setSettings(BibleVersionSettings& sets);
    BibleHistory getCurrentVerse();
    void addToHistory(BibleHistory &b);
    void clearHistory();
    void setSelectedHistory(BibleHistory &b);
    bool isVerseSelected();
    void setBibleBookActive();
    void setBibleSearchActive();
    // (Re)loads AI Bible assistant settings and (re)connects the transcript feed.
    void loadAiAssistantSettings();
    // Sends the current (possibly AI-preselected) verse live. Returns false if nothing is selected.
    bool goLiveWithCurrentSelection();

protected:
    virtual void changeEvent(QEvent *e);

signals:
    void setWaitCursor();
    void setArrowCursor();
    void goLive(QStringList chapter_list, QString caption, QItemSelection selectItems);

private slots:
    void on_history_listWidget_doubleClicked(QModelIndex index);
    void on_history_listWidget_currentRowChanged(int currentRow);
    void on_search_results_list_doubleClicked(QModelIndex index);
    void on_search_results_list_currentRowChanged(int currentRow);
    void on_hide_result_button_clicked();
    void on_search_button_clicked();
    void on_chapter_ef_textChanged(QString new_string);
    void on_verse_ef_textChanged(QString new_string);
    void on_btnLive_clicked();
    void on_lineEditBook_textChanged(QString );
    void on_chapter_preview_list_doubleClicked(QModelIndex index);
    void on_chapter_preview_list_currentRowChanged(int currentRow);
    void on_listChapterNum_currentTextChanged(QString currentText);
    void on_listBook_currentTextChanged(QString currentText);
    void addToHistory();
    void applyAiSelection(const BibleSelection &selection, double confidence);
    void onTranscriptLine(qint64 id, const QString &text);

private:
    BibleVersionSettings mySettings;
    Ui::BibleWidget *ui;
    HighlighterDelegate *highlight;
    QList<BibleSearch> search_results;
    QList<BibleHistory> history_items;
    QIntValidator *chapter_validator, *verse_validator;
    QByteArray hidden_splitter_state, shown_splitter_state;
    QButtonGroup search_type_buttongroup;

    // AI Bible assistant
    AiBibleSettings aiSettings;
    TranscriptStream *transcript = nullptr;
    BibleReferenceAssistant *assistant = nullptr;
    QFrame *aiPanel = nullptr;
    QPlainTextEdit *transcriptView = nullptr;
    QLabel *aiStatus = nullptr;
    QCheckBox *aiFollow = nullptr;
    bool applyingAiSelection = false;
    // Book/chapter/verse structure of the operator Bible, built once per Bible load.
    mutable QList<BibleBookInfo> assistantBookCache;
    mutable int assistantMaxVerseCache = 0;
    mutable BibleQuoteIndex quoteIndex;
    void rebuildAssistantCache() const;
    QElapsedTimer lastManualChange;
    void setupAiPanel();
    void noteManualChange();
};

#endif // BIBLEWIDGET_HPP
