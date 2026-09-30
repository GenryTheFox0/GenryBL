// GenryBL V1 - story syntax colours, driven by the same command table as the compiler.
#pragma once
#include <QHash>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class StoryHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QVariantList issues READ issues WRITE setIssues NOTIFY issuesChanged)
    // the find bar (Ctrl+F): every match gets a gold tint
    Q_PROPERTY(QString findText READ findText WRITE setFindText NOTIFY findChanged)
    Q_PROPERTY(bool findCase READ findCase WRITE setFindCase NOTIFY findChanged)

public:
    explicit StoryHighlighter(QObject* parent = nullptr);
    QQuickTextDocument* document() const { return m_doc; }
    void setDocument(QQuickTextDocument* doc);
    QVariantList issues() const { return m_issues; }
    void setIssues(const QVariantList& issues);
    QString findText() const { return m_find; }
    void setFindText(const QString& t);
    bool findCase() const { return m_findCase; }
    void setFindCase(bool on);
    // «Заменить все»: every match at once, as ONE step of Ctrl+Z; how many were replaced
    Q_INVOKABLE int replaceAll(const QString& find, const QString& with, bool caseSensitive);
    Q_INVOKABLE void replaceRange(int from, int to, const QString& with);   // one match: one step of Ctrl+Z

signals:
    void documentChanged();
    void issuesChanged();
    void findChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    QPointer<QQuickTextDocument> m_doc;
    QVariantList m_issues;
    QHash<int, int> m_lineLevel;    // line -> worst lint level
    QHash<int, QVector<QVector<int>>> m_ranges;   // line -> {col, len, level}: the issue's own words
    QString m_find;
    bool m_findCase = false;
};
