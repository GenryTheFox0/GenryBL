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

public:
    explicit StoryHighlighter(QObject* parent = nullptr);
    QQuickTextDocument* document() const { return m_doc; }
    void setDocument(QQuickTextDocument* doc);
    QVariantList issues() const { return m_issues; }
    void setIssues(const QVariantList& issues);

signals:
    void documentChanged();
    void issuesChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    QPointer<QQuickTextDocument> m_doc;
    QVariantList m_issues;
    QHash<int, int> m_lineLevel;    // line -> worst lint level
};
