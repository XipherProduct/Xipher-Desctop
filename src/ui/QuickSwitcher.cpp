#include "ui/Theme.h"
#include "ui/QuickSwitcher.h"
#include "ui/AvatarUtil.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QEvent>
#include <QKeyEvent>
#include <QStyle>

QuickSwitcher::QuickSwitcher(const QList<Chat>& chats, QWidget* parent)
    : ModalOverlay(parent, 520), chats_(chats) {
    card()->setFixedHeight(480);
    card()->setStyleSheet(ThemePreset::applyTokens(QStringLiteral(R"QSS(
#modalCard{background:@{s1};border:1px solid @{bDef};border-radius:18px;}
QLabel{color:@{tp};}
#qsHeader{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 @{acD},stop:0.55 @{ac},stop:1 #B06CF0);
  border-top-left-radius:18px;border-top-right-radius:18px;}
#qsTitle{font-size:17px;font-weight:800;color:#fff;}
#qsKbd{background:@{bStr};border-radius:8px;color:#fff;font-size:12px;
  font-weight:600;padding:3px 8px;}
QLineEdit{background:@{s1};border:1px solid rgba(255,255,255,10%);border-radius:12px;
  min-height:40px;padding:0 14px;color:@{tp};font-size:15px;
  selection-background-color:@{ac};}
QLineEdit:focus{border:1px solid @{ac};}
#qsRow{background:transparent;border-radius:10px;}
#qsRow:hover{background:@{s2};}
#qsRowSelected{background:@{ac14};border-radius:10px;border-left:3px solid @{ac};}
#qsNm{color:@{tp};font-size:14px;font-weight:600;}
#qsKind{color:@{tt};font-size:12px;}
#qsEmpty{color:@{tt};font-size:13px;}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:@{bStr};border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
)QSS")));

    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* head = new QWidget();
    head->setObjectName(QStringLiteral("qsHeader"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setFixedHeight(52);
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(20, 0, 16, 0);
    auto* t = new QLabel(QStringLiteral("Быстрый переход")); t->setObjectName(QStringLiteral("qsTitle"));
    auto* kbd = new QLabel(QStringLiteral("Ctrl+K")); kbd->setObjectName(QStringLiteral("qsKbd"));
    hh->addWidget(t);
    hh->addStretch();
    hh->addWidget(kbd);
    lay->addWidget(head);

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(14, 12, 14, 14);
    v->setSpacing(10);
    input_ = new QLineEdit();
    input_->setPlaceholderText(QStringLiteral("Введите имя чата, канала или человека…"));
    input_->installEventFilter(this);   // ↑/↓/Enter в поле
    v->addWidget(input_);
    connect(input_, &QLineEdit::textChanged, this,
            [this](const QString& s) { rebuild(s.trimmed()); });

    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listW = new QWidget();
    listBox_ = new QVBoxLayout(listW);
    listBox_->setContentsMargins(0, 0, 6, 0);
    listBox_->setSpacing(4);
    listBox_->addStretch();
    sa->setWidget(listW);
    v->addWidget(sa, 1);
    lay->addWidget(body, 1);

    rebuild(QString());
}

void QuickSwitcher::setChats(const QList<Chat>& chats) {
    chats_ = chats;
    rebuild(input_ ? input_->text().trimmed() : QString());
}

void QuickSwitcher::resetForOpen() {
    if (input_) {
        input_->clear();
        input_->setFocus();
    }
    rebuild(QString());
}

// Fuse-подобный скоринг под запрос (DSC-01). Больше — релевантнее:
//   +120 префикс отображаемого имени;  +80 префикс слова;  +50 подпоследовательность;
//   username весит вдвое меньше названия; штраф за «разреженность» совпадения.
int QuickSwitcher::fuzzyScore(const Chat& c, const QString& query) {
    const QString name = (c.isSaved ? QStringLiteral("Избранное") : c.displayName).toLower();
    const QString uname = c.name.toLower();
    const QString q = query.toLower();
    if (q.isEmpty()) return 0;

    if (name.startsWith(q)) return 120;
    if (!uname.isEmpty() && uname.startsWith(q)) return 100;

    // Префикс любого слова названия («ал» находит «Мама Алисы» слабее, но находит).
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int best = -1;
    for (const QString& w : words)
        if (w.startsWith(q)) { best = 80; break; }
    if (best > 0) return best;

    // Подпоследовательность: буквы запроса в порядке, но с пропусками.
    auto subseq = [](const QString& hay, const QString& needle) {
        int gi = 0, gaps = 0;
        for (int i = 0; i < hay.size() && gi < needle.size(); ++i) {
            if (hay[i] == needle[gi]) { ++gi; }
            else if (gi > 0) { ++gaps; }
        }
        return gi == needle.size() ? gaps : -1;
    };
    const int g1 = subseq(name, q);
    if (g1 >= 0) return qMax(10, 50 - g1);
    const int g2 = subseq(uname, q);
    if (g2 >= 0) return qMax(5, 25 - g2);
    return -1;
}

void QuickSwitcher::rebuild(const QString& filter) {
    rows_.clear();
    while (listBox_->count() > 1) {
        QLayoutItem* it = listBox_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    results_.clear();
    if (filter.isEmpty()) {
        results_ = chats_;   // «последние сверху» — порядок сервера (~свежесть)
    } else {
        QList<QPair<int, Chat>> scored;
        for (const Chat& c : chats_) {
            const int s = fuzzyScore(c, filter);
            if (s >= 0) scored.append({s, c});
        }
        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& p : scored) results_.append(p.second);
    }
    sel_ = results_.isEmpty() ? -1 : 0;

    if (results_.isEmpty()) {
        auto* empty = new QLabel(
            filter.isEmpty() ? QStringLiteral("Чатов пока нет")
                             : QStringLiteral("Ничего не найдено по «%1»").arg(filter));
        empty->setObjectName(QStringLiteral("qsEmpty"));
        empty->setAlignment(Qt::AlignHCenter);
        empty->setContentsMargins(0, 24, 0, 24);
        listBox_->insertWidget(0, empty);
        return;
    }

    int row = 0;
    for (int i = 0; i < results_.size(); ++i) {
        const Chat& c = results_[i];
        auto* w = new QFrame();
        w->setObjectName(i == sel_ ? QStringLiteral("qsRowSelected")
                                   : QStringLiteral("qsRow"));
        w->setProperty("pickId", c.id);
        w->installEventFilter(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(8, 5, 10, 5);
        h->setSpacing(10);
        auto* av = new QLabel();
        av->setFixedSize(36, 36);
        const QString title = c.isSaved ? QStringLiteral("Избранное") : c.displayName;
        Avatar::setRound(av, c.isSaved ? QString() : c.avatarUrl,
                         c.isSaved ? QStringLiteral("★") : title, 36);
        h->addWidget(av);
        auto* col = new QVBoxLayout();
        col->setSpacing(1);
        auto* nm = new QLabel(title);
        nm->setObjectName(QStringLiteral("qsNm"));
        col->addWidget(nm);
        const QString kindTxt = c.kind == ChatKind::Group ? QStringLiteral("Группа")
                              : c.kind == ChatKind::Channel ? QStringLiteral("Канал")
                              : (c.isBot ? QStringLiteral("Бот") : QString());
        QString sub = kindTxt;
        if (!c.name.isEmpty()) {
            if (!sub.isEmpty()) sub += QStringLiteral(" · ");
            sub += QStringLiteral("@") + c.name;
        }
        if (!sub.isEmpty()) {
            auto* k = new QLabel(sub);
            k->setObjectName(QStringLiteral("qsKind"));
            col->addWidget(k);
        }
        h->addLayout(col, 1);
        rows_.append(w);
        listBox_->insertWidget(row++, w);
    }
}

void QuickSwitcher::moveSelection(int delta) {
    if (results_.isEmpty()) return;
    const int ns = (sel_ + delta + results_.size()) % results_.size();
    if (ns == sel_) return;
    // Стили строк: выделение переезжает.
    if (sel_ >= 0 && sel_ < rows_.size())
        rows_[sel_]->setObjectName(QStringLiteral("qsRow"));
    sel_ = ns;
    if (sel_ >= 0 && sel_ < rows_.size()) {
        rows_[sel_]->setObjectName(QStringLiteral("qsRowSelected"));
        // policy/polish обновляют QSS по objectName
        rows_[sel_]->style()->unpolish(rows_[sel_]);
        rows_[sel_]->style()->polish(rows_[sel_]);
    }
}

void QuickSwitcher::activateSelected() {
    if (sel_ < 0 || sel_ >= results_.size()) return;
    const Chat c = results_[sel_];
    closeAnimated();
    emit picked(c);
}

QStringList QuickSwitcher::resultIds() const {
    QStringList ids;
    for (const Chat& c : results_) ids << c.id;
    return ids;
}

QString QuickSwitcher::selectedId() const {
    return (sel_ >= 0 && sel_ < results_.size()) ? results_[sel_].id : QString();
}

bool QuickSwitcher::eventFilter(QObject* obj, QEvent* e) {
    // Клавиатура в поле ввода: ↑/↓ листают, Enter открывает.
    if (obj == input_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Down)  { moveSelection(+1); return true; }
        if (ke->key() == Qt::Key_Up)    { moveSelection(-1); return true; }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            activateSelected();
            return true;
        }
    }
    // Клик по строке — открыть.
    if (e->type() == QEvent::MouseButtonRelease) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            const QString id = w->property("pickId").toString();
            if (!id.isEmpty()) {
                for (int i = 0; i < results_.size(); ++i)
                    if (results_[i].id == id) { sel_ = i; break; }
                activateSelected();
                return true;
            }
        }
    }
    // Ховер по строке снимает клавиатурное выделение на эту строку.
    if (e->type() == QEvent::Enter && rows_.contains(qobject_cast<QFrame*>(obj))) {
        const int idx = rows_.indexOf(qobject_cast<QFrame*>(obj));
        if (idx >= 0 && idx != sel_) moveSelection(idx - sel_);
    }
    return ModalOverlay::eventFilter(obj, e);
}
