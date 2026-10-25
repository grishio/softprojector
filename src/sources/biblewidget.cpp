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

#include "../headers/biblewidget.hpp"
#include "ui_biblewidget.h"
#include "../headers/song.hpp"
#include "../headers/transcriptstream.hpp"
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QSignalBlocker>
//#include <time.h>

//double diffclock(clock_t clock1,clock_t clock2)
//{
//        double diffticks=clock1-clock2;
//        double diffms=(diffticks*1000)/CLOCKS_PER_SEC;
//        return diffms;
//}

BibleWidget::BibleWidget(QWidget *parent) :
    QWidget(parent), ui(new Ui::BibleWidget)
{
    ui->setupUi(this);
    on_hide_result_button_clicked();

    chapter_validator = new QIntValidator(1, 1, ui->chapter_ef);
    verse_validator = new QIntValidator(1, 1, ui->verse_ef);

    ui->chapter_ef->setValidator( chapter_validator );
    ui->verse_ef->setValidator( verse_validator );

    highlight = new HighlighterDelegate(ui->search_results_list);
    ui->search_results_list->setItemDelegate(highlight);

    setupAiPanel();
}

BibleWidget::~BibleWidget()
{
    transcript->stop();
    assistant->cancel();
    delete chapter_validator;
    delete verse_validator;
    delete ui;
}

void BibleWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void BibleWidget::setSettings(BibleVersionSettings &sets)
{
    QString initial_bible = mySettings.operatorBible;
    mySettings = sets;
    loadBibles(initial_bible);
}

void BibleWidget::loadBibles(QString initialId)
{
    // if operator bible = "same", then set it to primary bible
    if(mySettings.operatorBible == "same")
        mySettings.operatorBible = mySettings.primaryBible;

    // make sure that program does not drop if no bible is present
    if(mySettings.operatorBible == "none")
        ui->btnLive->setEnabled(false);
    else
        ui->btnLive->setEnabled(true);

    // Check if primary bible is different that what has been loaded already
    // If it is different, then reload the bible list
    if(initialId!=mySettings.operatorBible)
    {
        bible.setBiblesId(mySettings.operatorBible);
        bible.loadOperatorBible();
        assistantBookCache.clear(); // rebuilt on the next AI request
        ui->listBook->clear();
        ui->listBook->addItems(bible.getBooks());
        ui->listBook->setCurrentRow(0);
    }
}

void BibleWidget::on_listBook_currentTextChanged(QString currentText)
{
    noteManualChange();
    int s = ui->listBook->currentRow();
    if( s != -1 )
    {
        int max_chapter = bible.books.at(bible.getCurrentBookRow(currentText)).chapterCount;
        ui->listChapterNum->clear();
        for(int i=0; i<max_chapter; ++i)
            ui->listChapterNum->addItem(QString::number(i+1));
        chapter_validator->setTop(max_chapter);
        if( ui->listChapterNum->currentRow() != 0 )
            ui->listChapterNum->setCurrentRow(0);
    }
    else
    {
        // No bible book selected
        chapter_validator->setTop(1);
        ui->listChapterNum->clear();
    }
}

void BibleWidget::on_listChapterNum_currentTextChanged(QString currentText)
{
    noteManualChange();
    int s = ui->listChapterNum->currentRow();
    if( s != -1 )
    {
        // This optimization is required in order for the bible filter entry field to work fast:
        if( currentBook != getCurrentBook() || currentChapter != currentText.toInt() )
        {
            currentBook = getCurrentBook();
            currentChapter = currentText.toInt();
            currentChapterList = bible.getChapter(bible.books.at(bible.getCurrentBookRow(currentBook)).bookId.toInt(), currentChapter);
        }

        ui->chapter_preview_list->clear();
        ui->chapter_preview_list->addItems(currentChapterList);
        ui->chapter_ef->setText(currentText);
        verse_validator->setTop(ui->chapter_preview_list->count());
        ui->chapter_preview_list->setCurrentRow(0);
    }
    else
    {
        ui->chapter_preview_list->clear();
    }
}

QString BibleWidget::getCurrentBook()
{
    return ui->listBook->currentItem()->text();
}

int BibleWidget::getCurrentChapter()
{
    return ui->listChapterNum->currentItem()->text().toInt();
}

/*
bool BibleWidget::eventFilter(QObject *object, QEvent *event)
{
    if (object == ui->spinVerse && event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::KeyDown) {
            qDebug() << "DOWN KEY";
            // Special tab handling
            return true;
        } else
            return false;
    }
    return false;
}
*/

void BibleWidget::on_chapter_preview_list_currentRowChanged(int currentRow)
{
    noteManualChange();
    ui->verse_ef->setText(QString::number(currentRow+1));
}

void BibleWidget::on_chapter_preview_list_doubleClicked(QModelIndex index)
{
    // Called when a chapter or verse is double clicked
    sendToProjector(true);
}

void BibleWidget::sendToProjector(bool add_to_history)
{
    bible.currentIdList = bible.previewIdList;
    QItemSelection selectedItems = ui->chapter_preview_list->selectionModel()->selection();
    if(selectedItems.count())
    {
        // Get the caption string to show above the show list (right-most list)
        QString cap = QString("%1 %2").arg(ui->listBook->currentItem()->text()).arg(ui->listChapterNum->currentItem()->text());
        emit goLive(bible.verseList, cap,selectedItems);
        if (add_to_history)
            addToHistory();
    }
}

void BibleWidget::on_lineEditBook_textChanged(QString text)
{
    noteManualChange();
    if (text == "0") {
        ui->lineEditBook->clear();
        return;
    }

    // Called when the bible book filter field is modified.
    QStringList all_books = bible.getBooks();

    // Remove trailing spaces:
    text = text.trimmed();

    int chapter = 0;
    int verse = 0;

    // Check whether the user entered a search string that include book, chapter,
    // and verse. For example: "1King 3 13"
    QStringList search_words = text.split(" ");

    // Allows the user to use more than one space as a seperator:
    search_words.removeAll("");

    if( search_words.count() > 1 )
    {
        bool ok;
        int num1 = search_words.last().toInt(&ok);
        if( ok )
        {
            chapter = num1;
            search_words.removeLast();
            if( search_words.count() > 1 )
            {
                bool ok2;
                int num2 = search_words.last().toInt(&ok2);
                if( ok2 )
                {
                    search_words.removeLast();
                    chapter = num2;
                    verse = num1;
                }
            }
            text = search_words.join(" ");
        }
    }

    // Now search all books to find the matching book:
    if( text.isEmpty() )
    {
        // Show all bible books
        if( ui->listBook->count() != all_books.count() )
        {
            // This is an important optimization
            ui->listBook->clear();
            ui->listBook->addItems(all_books);
        }
    }
    else
    {
        // Show only the bible books that match the filter
        QStringList filtered_books;
        if( text.at(0).isDigit() )
        {
            // First character of filter text is a number. Special search, where the
            // first character must be the first character of the first word of the book;
            // while the rest of the filter must be the beginning of the second book word.
            QString num_str(text.at(0));
            QString name_str = text.remove(0, 1);
            // Allow for space inbetween num_str and name_str e.g. "1 Peter"
            name_str = name_str.trimmed();
            for(int i=0; i<all_books.count(); i++)
            {
                QString book = all_books.at(i);
                QStringList book_words = book.split(" ");

                if( ! book_words.at(0).startsWith(num_str) )
                    continue;
                if( !book_words.at(1).startsWith(name_str, Qt::CaseInsensitive) )
                    continue;

                filtered_books.append(book);
            }
        }
        else
            filtered_books = all_books.filter(text, Qt::CaseInsensitive);

        if( ui->listBook->count() != filtered_books.count() )
        {
            // This is an important optimization
            // FIXME don't just check the count; check values
            ui->listBook->clear();
            ui->listBook->addItems(filtered_books);
        }
    }

    if( ui->listBook->count() > 0 )
        // Select the first row. This will take a longer time only if it will cause
        // a new chapter to be loaded into the preview
        ui->listBook->setCurrentRow(0);

    if( chapter != 0 && chapter <= ui->listChapterNum->count() )
    {
        if( ui->listChapterNum->currentRow() != (chapter-1) )
            ui->listChapterNum->setCurrentRow(chapter-1);
        if( verse != 0 && verse <= ui->chapter_preview_list->count() )
            ui->chapter_preview_list->setCurrentRow(verse-1);
    }
}

void BibleWidget::on_btnLive_clicked()
{
    sendToProjector(true);
}

void BibleWidget::on_verse_ef_textChanged(QString new_string)
{
    int value = new_string.toInt();
    ui->chapter_preview_list->setCurrentRow(value-1);
}

void BibleWidget::on_chapter_ef_textChanged(QString new_string)
{
    int value = new_string.toInt();
    ui->listChapterNum->setCurrentRow(value-1);
}

void BibleWidget::on_search_button_clicked()
{
    QString search_text = ui->search_ef->text();
    search_text = clean(search_text); // remove all none alphanumeric charecters

    // Make sure that there is some text to do a search for, if none, then return
    if(search_text.count()<1)
    {
        ui->search_ef->clear();
        ui->search_ef->setPlaceholderText(tr("Please enter search text"));
        return;
    }

    emit setWaitCursor();
    int type = ui->comboBoxSearchType->currentIndex();
    int range = ui->comboBoxSearchRange->currentIndex();

    QRegularExpression rx, rxh;
    rx.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    search_text.replace(" ","\\W*");
    if(type == 0)
    {
        // Search text phrase
        rx.setPattern(search_text);
        rxh.setPattern(search_text);
    }
    else if(type == 1)
    {
        // Search whole word exsact phrase only
        rx.setPattern("\\b"+search_text+"\\b");
        rxh.setPattern("\\b"+search_text+"\\b");
    }
    else if(type == 2)
    {
        // Search begining of every line
        rx.setPattern("^"+search_text);
        rxh.setPattern(search_text);
    }
    else if(type == 3 || type == 4)
    {
        // Search for any of the search words
        search_text.replace("\\W*","|");
        rx.setPattern("\\b("+search_text+")\\b");
        rxh.setPattern("\\b("+search_text+")\\b");
    }

    highlight->highlighter->setHighlightText(rxh.pattern()); // set highlighting rule

    if(range == 0) // Search entire Bible
        search_results = bible.searchBible((type == 4),rx);
    else if(range == 1) // Search current book only
        search_results = bible.searchBible((type == 4),rx,
                                           bible.books.at(bible.getCurrentBookRow(ui->listBook->currentItem()->text())).bookId.toInt());
    else if (range == 2) // Search current chapter only
        search_results = bible.searchBible((type == 4),rx,
                                           bible.books.at(bible.getCurrentBookRow(ui->listBook->currentItem()->text())).bookId.toInt(),
                                           ui->listChapterNum->currentItem()->text().toInt());

    ui->search_results_list->clear();

    if (!search_results.isEmpty()) // If have results, then show them
    {
        if( ! ui->result_label->isVisible() )
        {
            ui->lineEditBook->clear();
            hidden_splitter_state = ui->results_splitter->saveState();
            ui->result_label->show();
            ui->result_count_label->show();
            ui->search_results_list->show();
            ui->hide_result_button->show();
            ui->search_layout->addItem(ui->results_layout);
            ui->results_splitter->restoreState(shown_splitter_state);
        }
        QStringList verse_list;
        int count = search_results.count();

        ui->result_count_label->setText(tr("Total\nresutls:\n%1").arg(count));

        for(int i(0);i<count;i++)
            verse_list.append(search_results.at(i).verse_text);
        ui->search_results_list->addItems(verse_list);
    }
    else // If no relust, notify the user and hide result list
        ui->result_count_label->setText(tr("No search\nresults."));

    emit setArrowCursor();
}

void BibleWidget::on_hide_result_button_clicked()
{
    shown_splitter_state = ui->results_splitter->saveState();
    ui->result_label->hide();
    ui->result_count_label->hide();
    ui->search_results_list->hide();
    ui->hide_result_button->hide();
    ui->search_layout->removeItem(ui->results_layout);
    ui->results_splitter->restoreState(hidden_splitter_state);
}

void BibleWidget::on_search_results_list_currentRowChanged(int currentRow)
{
    if (currentRow >=0)
    {
        QStringList all_books = bible.getBooks();

        if(ui->listBook->count() != all_books.count())
        {
            ui->listBook->clear();
            ui->listBook->addItems(all_books);
        }

        int row = all_books.indexOf(search_results.at(currentRow).book);
        ui->listBook->setCurrentRow(row);

        ui->chapter_ef->setText(search_results.at(currentRow).chapter);
        ui->verse_ef->setText(search_results.at(currentRow).verse);
    }
}

void BibleWidget::on_search_results_list_doubleClicked(QModelIndex index)
{
    on_search_results_list_currentRowChanged(index.row());
    on_btnLive_clicked();
}

void BibleWidget::addToHistory()
{
    BibleHistory b = getCurrentVerse();
    history_items.append(b);
    ui->history_listWidget->addItem(b.captionLong);
    // Scroll to latest history item
    ui->history_listWidget->scrollToBottom();
}

void BibleWidget::addToHistory(BibleHistory &b)
{
    history_items.append(b);
    ui->history_listWidget->addItem(b.captionLong);
}

void BibleWidget::clearHistory()
{
    ui->history_listWidget->clear();
    history_items.clear();
}

void BibleWidget::on_history_listWidget_currentRowChanged(int currentRow)
{
    if (currentRow >= 0)
    {
        BibleHistory bh = history_items.at(currentRow);
        setSelectedHistory(bh);
    }
}

void BibleWidget::setSelectedHistory(BibleHistory &b)
{
    QStringList all_books = bible.getBooks();
    if(ui->listBook->count()!=all_books.count())
    {
        ui->listBook->clear();
        ui->listBook->addItems(all_books);
    }
    QString bk;
    int ch,vr,vrl;
    bible.getVerseRef(b.verseIds,bk,ch,vr);
    vrl = bible.getVerseNumberLast(b.verseIds);

    ui->listBook->setCurrentRow(all_books.indexOf(bk));
    ui->chapter_ef->setText(QString::number(ch));
    QItemSelection sel;
    sel.select(ui->chapter_preview_list->model()->index(vr-1,0,QModelIndex()),
               ui->chapter_preview_list->model()->index(vrl-1,0,QModelIndex()));
    ui->chapter_preview_list->clearSelection();
    ui->verse_ef->setText(QString::number(vr));
    ui->chapter_preview_list->selectionModel()->select(sel,QItemSelectionModel::Select);
}

void BibleWidget::on_history_listWidget_doubleClicked(QModelIndex index)
{
    sendToProjector( false);
}

QByteArray BibleWidget::getHiddenSplitterState()
{
    if(ui->hide_result_button->isHidden())
        hidden_splitter_state = ui->results_splitter->saveState();
    return hidden_splitter_state;
}

QByteArray BibleWidget::getShownSplitterState()
{
    if(!ui->hide_result_button->isHidden())
        shown_splitter_state = ui->results_splitter->saveState();
    return shown_splitter_state;
}

void BibleWidget::setHiddenSplitterState(QByteArray& state)
{
    hidden_splitter_state = state;
    ui->results_splitter->restoreState(hidden_splitter_state);
}

void BibleWidget::setShownSplitterState(QByteArray& state)
{
    shown_splitter_state = state;
}

BibleHistory BibleWidget::getCurrentVerse()
{
    BibleHistory b;
    QString selected_ids;

    QString book = ui->listBook->currentItem()->text();
    QString chapter = ui->chapter_ef->text();

    int first_selected(-1),last_selected(-1);
    for(int i(0);i<ui->chapter_preview_list->count();++i)
    {
        if(ui->chapter_preview_list->item(i)->isSelected())
        {
            if(first_selected == -1)
                first_selected = i;
            last_selected = i;
            selected_ids += bible.previewIdList.at(i) + ",";
        }
    }
    selected_ids.chop(1);

    QString verse_text = ui->chapter_preview_list->item(first_selected)->text().trimmed();
    b.verseIds = selected_ids;

    if(first_selected==last_selected)
    {
        b.caption = book + " " + chapter + ":" + QString::number(first_selected+1);
        b.captionLong = book + " " + chapter + ":" + verse_text;
    }
    else
    {   // Create multi verse caption for display
        int f(first_selected+1), l(last_selected+1),j(0);
        QString v=verse_text,p=".";
        while(v.at(j)!=p.at(0))
            ++j;
        v = v.remove(0,j);

        b.caption = book + " " + chapter + ":" + QString::number(f) + "-" + QString::number(l);
        b.captionLong = book + " " + chapter + ":" + QString::number(f) + "-" + QString::number(l) + v + "...";
    }

    return b;
}

bool BibleWidget::isVerseSelected()
{
    if(ui->chapter_preview_list->selectedItems().count() >= 1)
        return true;
    else
        return false;
}

void BibleWidget::setBibleBookActive()
{
    ui->lineEditBook->setFocus();
    ui->lineEditBook->selectAll();
}

void BibleWidget::setBibleSearchActive()
{
    ui->search_ef->setFocus();
    ui->search_ef->selectAll();
}

// ---------------------------------------------------------------------------
// AI Bible assistant
// ---------------------------------------------------------------------------

void BibleWidget::setupAiPanel()
{
    transcript = new TranscriptStream(this);
    assistant = new BibleReferenceAssistant(this, this);

    // Compact strip: transcript (two text lines) | AI status (two lines) | Follow.
    aiPanel = new QFrame(this);
    aiPanel->setObjectName("aiTranscriptStrip");
    auto *panelLayout = new QHBoxLayout(aiPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(6);
    transcriptView = new QPlainTextEdit(aiPanel);
    transcriptView->setObjectName("aiTranscriptView");
    transcriptView->setReadOnly(true);
    transcriptView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    transcriptView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    transcriptView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    transcriptView->setFocusPolicy(Qt::NoFocus);
    transcriptView->setToolTip(tr("Live transcript (most recent text)."));
    const int twoLines = transcriptView->fontMetrics().lineSpacing() * 2 +
        2 * transcriptView->frameWidth() + 2 * int(transcriptView->document()->documentMargin()) + 2;
    transcriptView->setFixedHeight(twoLines);
    panelLayout->addWidget(transcriptView, 1);
    aiStatus = new QLabel(aiPanel);
    aiStatus->setTextFormat(Qt::PlainText);
    aiStatus->setWordWrap(true);
    aiStatus->setFixedHeight(twoLines);
    aiStatus->setFixedWidth(280);
    aiStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    panelLayout->addWidget(aiStatus);
    aiFollow = new QCheckBox(tr("Follow"), aiPanel);
    aiFollow->setToolTip(tr("When checked, the AI preselects the book, chapter and verse the speaker names. "
                            "Nothing is shown until you press F5 or Live."));
    aiFollow->setChecked(true);
    panelLayout->addWidget(aiFollow);
    aiPanel->setFixedHeight(twoLines);
    aiPanel->setVisible(false);
    // Fixed height under the book/chapter/verse lists, so the Bible lists keep the space.
    ui->verticalLayout_9->addWidget(aiPanel, 0);

    connect(aiFollow, &QCheckBox::toggled, this, [this](bool follow) {
        if (follow)
            assistant->operatorTookOver(); // start fresh from the next spoken line
        else
            assistant->cancel();
        aiStatus->setText(follow ? tr("Listening...") : tr("Not following the transcript."));
    });
    connect(transcript, &TranscriptStream::lineReceived, this, &BibleWidget::onTranscriptLine);
    connect(transcript, &TranscriptStream::connected, this, [this]() {
        aiStatus->setText(tr("Connected to transcript feed. Listening..."));
    });
    connect(transcript, &TranscriptStream::disconnected, this, [this](const QString &reason) {
        if (transcript->isRunning())
            aiStatus->setText(tr("%1 Reconnecting...").arg(reason));
    });
    connect(assistant, &BibleReferenceAssistant::suggestion, this, &BibleWidget::applyAiSelection);
    connect(assistant, &BibleReferenceAssistant::statusChanged, aiStatus, &QLabel::setText);
    connect(assistant, &BibleReferenceAssistant::failed, aiStatus, &QLabel::setText);
}

void BibleWidget::loadAiAssistantSettings()
{
    QString error;
    AiBibleSettings loaded;
    if (!loaded.load(&error)) {
        loaded = AiBibleSettings();
        loaded.enabled = false;
    }
    aiSettings = loaded;
    transcript->stop();
    assistant->setSettings(aiSettings);
    aiPanel->setVisible(aiSettings.enabled);
    if (!aiSettings.enabled)
        return;
    if (!aiSettings.isUsable()) {
        aiStatus->setText(tr("AI Bible assistant is not fully configured. Open Settings > AI Bible Assistant."));
        return;
    }
    aiStatus->setText(tr("Connecting to transcript feed..."));
    transcript->start(QUrl(aiSettings.transcriptUrl.trimmed()));
}

void BibleWidget::onTranscriptLine(qint64 id, const QString &text)
{
    Q_UNUSED(id);
    // Running text, not one line per message; only the last two visual lines are visible.
    QString all = transcriptView->toPlainText();
    if (!all.isEmpty())
        all += ' ';
    all += text;
    if (all.size() > 1500)
        all = all.right(1000);
    transcriptView->setPlainText(all);
    transcriptView->moveCursor(QTextCursor::End);
    transcriptView->verticalScrollBar()->setValue(transcriptView->verticalScrollBar()->maximum());
    if (aiFollow->isChecked())
        assistant->addLine(text);
}

void BibleWidget::noteManualChange()
{
    if (applyingAiSelection)
        return;
    lastManualChange.restart();
    if (assistant && aiSettings.enabled)
        assistant->operatorTookOver();
}

bool BibleWidget::assistantPaused() const
{
    if (!aiFollow || !aiFollow->isChecked())
        return true;
    return lastManualChange.isValid() && aiSettings.pauseSeconds > 0 &&
        lastManualChange.elapsed() < qint64(aiSettings.pauseSeconds) * 1000;
}

void BibleWidget::rebuildAssistantCache() const
{
    assistantBookCache.clear();
    assistantMaxVerseCache = 0;
    const QHash<int, QList<int> > counts = bible.allVerseCounts();
    for (const BibleBook &b : bible.books)
    {
        BibleBookInfo info;
        info.name = b.book;
        info.chapterCount = b.chapterCount;
        info.verseCounts = counts.value(b.bookId.toInt());
        while (info.verseCounts.size() < info.chapterCount)
            info.verseCounts.append(0);
        for (int v : info.verseCounts)
            assistantMaxVerseCache = qMax(assistantMaxVerseCache, v);
        assistantBookCache.append(info);
    }

    // Index of verse texts for detecting uncited readings. Split verses that
    // share a verse number are merged, as in the chapter preview.
    QHash<int, QString> bookNames;
    for (const BibleBook &b : bible.books)
        bookNames.insert(b.bookId.toInt(), b.book);
    QList<QuoteVerse> quoteVerses;
    for (const BibleVerse &bv : bible.operatorVerses())
    {
        if (!quoteVerses.isEmpty() && quoteVerses.last().verse == bv.verseNumber &&
            quoteVerses.last().chapter == bv.chapter && quoteVerses.last().book == bookNames.value(bv.book))
        {
            quoteVerses.last().text += " " + bv.verseText;
            continue;
        }
        QuoteVerse qv;
        qv.book = bookNames.value(bv.book);
        qv.chapter = bv.chapter;
        qv.verse = bv.verseNumber;
        qv.text = bv.verseText;
        quoteVerses.append(qv);
    }
    quoteIndex.build(quoteVerses);
}

QList<VerseCandidate> BibleWidget::assistantQuoteCandidates(const QString &text) const
{
    assistantBooks(); // builds the index on first use
    return quoteIndex.search(text);
}

QList<BibleBookInfo> BibleWidget::assistantBooks() const
{
    if (assistantBookCache.size() != bible.books.size())
        rebuildAssistantCache();
    return assistantBookCache;
}

int BibleWidget::assistantVerseCount(const QString &book, int chapter) const
{
    for (const BibleBookInfo &b : assistantBooks())
        if (b.name == book)
            return chapter >= 1 && chapter <= b.verseCounts.size() ? b.verseCounts.at(chapter - 1) : 0;
    return 0;
}

int BibleWidget::assistantMaxVerses() const
{
    assistantBooks();
    return assistantMaxVerseCache;
}

BibleSelection BibleWidget::assistantSelection() const
{
    BibleSelection selection;
    if (ui->listBook->currentItem())
        selection.book = ui->listBook->currentItem()->text();
    if (ui->listChapterNum->currentItem())
        selection.chapter = ui->listChapterNum->currentItem()->text().toInt();
    if (ui->chapter_preview_list->currentRow() >= 0)
        selection.verse = ui->chapter_preview_list->currentRow() + 1;
    return selection;
}

void BibleWidget::applyAiSelection(const BibleSelection &selection, double confidence)
{
    const QStringList all_books = bible.getBooks();
    const int bookRow = all_books.indexOf(selection.book);
    if (bookRow < 0)
        return;
    applyingAiSelection = true;
    {
        // Drop any book filter so the suggested book is visible in the list.
        QSignalBlocker blocker(ui->lineEditBook);
        ui->lineEditBook->clear();
    }
    if (ui->listBook->count() != all_books.count())
    {
        ui->listBook->clear();
        ui->listBook->addItems(all_books);
    }
    ui->listBook->setCurrentRow(bookRow);
    const int chapter = qBound(1, selection.chapter, qMax(1, ui->listChapterNum->count()));
    if (ui->listChapterNum->currentRow() != chapter - 1)
        ui->listChapterNum->setCurrentRow(chapter - 1);
    const int verse = qBound(1, selection.verse, qMax(1, ui->chapter_preview_list->count()));
    ui->chapter_preview_list->clearSelection();
    ui->chapter_preview_list->setCurrentRow(verse - 1);
    if (ui->chapter_preview_list->currentItem())
        ui->chapter_preview_list->scrollToItem(ui->chapter_preview_list->currentItem(), QAbstractItemView::PositionAtCenter);
    applyingAiSelection = false;
    Q_UNUSED(confidence); // shown in the status text by the assistant
}

bool BibleWidget::goLiveWithCurrentSelection()
{
    if (!ui->btnLive->isEnabled() || !isVerseSelected())
        return false;
    sendToProjector(true);
    return true;
}
