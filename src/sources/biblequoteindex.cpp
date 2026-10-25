#include "../headers/biblequoteindex.hpp"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
const int MinWordLength = 4;
const int StemLength = 5;
const int MinSharedStems = 4;
const int MinSharedPairs = 2;
// Stems found in this many verses ("господь", "который"...) carry no signal.
const int MaxPostingSize = 1500;
const int MaxQueryChars = 600;
const int RescoreCount = 40;

QSet<QString> pairsOf(const QStringList &stems)
{
    QSet<QString> pairs;
    for (int i = 1; i < stems.size(); ++i)
        pairs.insert(stems.at(i - 1) + '|' + stems.at(i));
    return pairs;
}
}

QStringList BibleQuoteIndex::stems(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}]+"));
    QString normalized = text.toLower();
    normalized.replace(QChar(0x0451), QChar(0x0435)); // ё -> е
    QStringList result;
    for (const QString &word : normalized.split(separators, Qt::SkipEmptyParts))
        if (word.size() >= MinWordLength)
            result << word.left(StemLength);
    return result;
}

void BibleQuoteIndex::clear()
{
    verses.clear();
    verseStems.clear();
    postings.clear();
}

void BibleQuoteIndex::build(const QList<QuoteVerse> &source)
{
    clear();
    verses.reserve(source.size());
    verseStems.reserve(source.size());
    for (const QuoteVerse &verse : source) {
        const int index = verses.size();
        verses.append(verse);
        const QStringList s = stems(verse.text);
        verseStems.append(s);
        for (const QString &stem : QSet<QString>(s.begin(), s.end()))
            postings[stem].append(index);
    }
}

QList<VerseCandidate> BibleQuoteIndex::search(const QString &text, int maxResults) const
{
    QList<VerseCandidate> results;
    const QStringList query = stems(text.right(MaxQueryChars));
    const QSet<QString> unique(query.begin(), query.end());
    if (unique.size() < MinSharedStems || verses.isEmpty())
        return results;

    QHash<int, int> shared;
    for (const QString &stem : unique) {
        const auto it = postings.constFind(stem);
        if (it == postings.constEnd() || it->size() > MaxPostingSize)
            continue;
        for (int index : *it)
            ++shared[index];
    }
    QList<QPair<int, int> > ranked; // (shared stems, verse index)
    for (auto it = shared.constBegin(); it != shared.constEnd(); ++it)
        if (it.value() >= MinSharedStems)
            ranked.append(qMakePair(it.value(), it.key()));
    std::sort(ranked.begin(), ranked.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        return a.first == b.first ? a.second < b.second : a.first > b.first;
    });
    if (ranked.size() > RescoreCount)
        ranked.erase(ranked.begin() + RescoreCount, ranked.end());

    // Word order: count consecutive stem pairs shared with the transcript.
    const QSet<QString> queryPairs = pairsOf(query);
    for (const auto &entry : ranked) {
        const QSet<QString> versePairs = pairsOf(verseStems.at(entry.second));
        int pairs = 0;
        for (const QString &pair : versePairs)
            if (queryPairs.contains(pair))
                ++pairs;
        if (pairs < MinSharedPairs)
            continue;
        const QuoteVerse &v = verses.at(entry.second);
        VerseCandidate candidate;
        candidate.book = v.book;
        candidate.chapter = v.chapter;
        candidate.verse = v.verse;
        candidate.text = v.text;
        candidate.score = entry.first + 2 * pairs;
        results.append(candidate);
    }
    std::sort(results.begin(), results.end(), [](const VerseCandidate &a, const VerseCandidate &b) {
        return a.score > b.score;
    });
    if (results.size() > maxResults)
        results.erase(results.begin() + maxResults, results.end());
    return results;
}
