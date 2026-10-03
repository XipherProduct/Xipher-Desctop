#include "ui/ComposerEdit.h"
#include "net/Prefs.h"

#include <QKeyEvent>
#include <QMimeData>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QScrollBar>

ComposerEdit::ComposerEdit(QWidget* parent) : QPlainTextEdit(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setTabChangesFocus(true);
    setFixedHeight(minH_);
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this]() { adjustHeight(); });
    connect(this, &QPlainTextEdit::textChanged, this, [this]() { adjustHeight(); });
}

void ComposerEdit::adjustHeight() {
    const qreal docH = document()->documentLayout()->documentSize().height();
    const int pad = 20;   // вертикальные отступы (padding сверху+снизу + рамка)
    int h = int(docH) + pad;
    h = qBound(minH_, h, maxH_);
    if (h != height()) setFixedHeight(h);
    setVerticalScrollBarPolicy(int(docH) + pad > maxH_ ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
}

void ComposerEdit::keyPressEvent(QKeyEvent* e) {
    // KEY-05: опция «Enter=перенос, Ctrl+Enter=отправка» (Prefs, по умолчанию
    // классика Telegram: Enter отправляет, Shift+Enter переносит).
    const bool ctrlEnterMode = Prefs::getBool(QStringLiteral("xipher_ctrl_enter_send"), false);
    const bool isEnter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
    if (ctrlEnterMode) {
        if (isEnter && (e->modifiers() & Qt::ControlModifier)) {
            emit sendRequested();
            return;
        }
        // Enter без Ctrl — перенос (дефолт QPlainTextEdit).
    } else {
        if (isEnter && !(e->modifiers() & Qt::ShiftModifier)
                    && !(e->modifiers() & Qt::ControlModifier)) {
            emit sendRequested();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

void ComposerEdit::insertFromMimeData(const QMimeData* src) {
    if (src && src->hasImage()) {
        const QImage img = qvariant_cast<QImage>(src->imageData());
        if (!img.isNull()) { emit imagePasted(img); return; }
    }
    if (src && src->hasUrls()) {
        for (const QUrl& u : src->urls()) {
            const QString p = u.toLocalFile();
            if (p.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
                || p.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
                || p.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive)
                || p.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
                QImage img(p);
                if (!img.isNull()) { emit imagePasted(img); return; }
            }
        }
    }
    QPlainTextEdit::insertFromMimeData(src);   // обычный текст
}
