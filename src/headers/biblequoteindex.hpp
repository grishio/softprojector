#ifndef BIBLEQUOTEINDEX_HPP
#define BIBLEQUOTEINDEX_HPP

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

struct QuoteVerse
{
    QString book;
    int chapter = 0;
    int verse = 0;
    QString text;
};

struct VerseCandidate
{
    QString book;
    int chapter = 0;
    int verse = 0;
    QString text;
    int score = 0;
    QString caption() const { return QString("%1 %2:%3").arg(book).arg(chapter).arg(verse); }
};

// Local fuzzy search that finds verses the speaker may be reading aloud
// without citing them. Words are lowercased, 'ё' folded to 'е', and cut to a
// 5-letter stem so inflections and small speech-recognition errors still
// match. A verse must share several stems and several consecutive stem pairs
// (word order) with the transcript to become a candidate; the decision model
// then confirms one candidate or rejects them all.
class BibleQuoteIndex
{
public:
    void build(const QList<QuoteVerse> &verses);
    void clear();
    bool isEmpty() const { return verses.isEmpty(); }
    QList<VerseCandidate> search(const QString &text, int maxResults = 5) const;
    static QStringList stems(const QString &text);

private:
    QVector<QuoteVerse> verses;
    QVector<QStringList> verseStems;
    QHash<QString, QVector<int> > postings;
};

#endif
