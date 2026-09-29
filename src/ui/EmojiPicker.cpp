#include "ui/EmojiPicker.h"
#include "ui/AnimatedEmojiLabel.h"
#include "ui/EmojiNames.h"
#include "net/Prefs.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>

namespace {

// ── Категории (codepoints Noto). Большой набор, как в вебе. ──────────────────
const char* kFaces =
"1f600 1f603 1f604 1f601 1f606 1f605 1f923 1f602 1f642 1f643 1f609 1f60a 1f607 1f970 1f60d 1f929 1f618 1f617 "
"1f61a 1f619 1f60b 1f61b 1f61c 1f92a 1f61d 1f911 1f917 1f92d 1f92b 1f914 1f910 1f928 1f610 1f611 1f636 1f60f "
"1f612 1f644 1f62c 1f925 1f60c 1f614 1f62a 1f924 1f634 1f637 1f912 1f915 1f922 1f92e 1f927 1f975 1f976 1f974 "
"1f635 1f92f 1f920 1f973 1f978 1f60e 1f913 1f9d0 1f615 1f61f 1f641 1f62e 1f62f 1f632 1f633 1f97a 1f626 1f627 "
"1f628 1f630 1f625 1f622 1f62d 1f631 1f616 1f623 1f61e 1f613 1f629 1f62b 1f971 1f624 1f621 1f620 1f92c 1f608 "
"1f47f 1f480 1f4a9 1f921 1f479 1f47a 1f47b 1f47d 1f47e 1f916";

const char* kGestures =
"1f44b 1f91a 1f590 270b 1f596 1f44c 1f90c 1f90f 270c 1f91e 1f91f 1f918 1f919 1f448 1f449 1f446 1f447 261d "
"1f44d 1f44e 270a 1f44a 1f91b 1f91c 1f44f 1f64c 1f450 1f932 1f91d 1f64f 270d 1f4aa 1f9b5 1f9b6 1f442 1f443 "
"1f463 1f440 1f441 1f445 1f444 1f9b7 1f9b4 1f9e0 1f9be 1f9bf 1f9b8 1f9b9 1f9d1";

const char* kHearts =
"2764 1f9e1 1f49b 1f49a 1f499 1f49c 1f5a4 1f90d 1f90e 1f494 1f495 1f49e 1f493 1f497 1f496 1f498 1f49d 1f49f "
"1f4af 1f4a5 1f4a2 1f4a8 1f4a6 1f4a4 1f573 1f4ac 1f4ad 1f5ef 1f44d 2b50 1f31f 2728 26a1 1f525 1f4ab";

const char* kAnimals =
"1f436 1f431 1f42d 1f439 1f430 1f98a 1f43b 1f43c 1f428 1f42f 1f981 1f42e 1f437 1f438 1f435 1f648 1f649 1f64a "
"1f412 1f414 1f427 1f426 1f424 1f986 1f985 1f989 1f987 1f43a 1f417 1f434 1f984 1f41d 1f41b 1f98b 1f40c 1f41e "
"1f41c 1f577 1f422 1f40d 1f432 1f409 1f419 1f41f 1f420 1f421 1f42c 1f433 1f40b 1f988 1f42b 1f42a 1f992 1f418 "
"1f98f 1f99b 1f404 1f402 1f403 1f40e 1f40f 1f411 1f40a 1f405 1f406 1f993 1f98c 1f415 1f429 1f408 1f413 1f983 "
"1f54a 1f407 1f99d 1f33b 1f339 1f33a 1f337 1f490 1f338 1f33c 1f343 1f342 1f340 1f335 1f384 1f332 1f333";

const char* kFood =
"1f34f 1f34e 1f350 1f34a 1f34b 1f34c 1f349 1f347 1f353 1f348 1f352 1f351 1f96d 1f34d 1f965 1f95d 1f345 1f346 "
"1f951 1f966 1f952 1f336 1f33d 1f955 1f9c4 1f9c5 1f954 1f360 1f950 1f956 1f961 1f35e 1f9c0 1f95a 1f373 1f95e "
"1f9c8 1f953 1f969 1f357 1f356 1f354 1f35f 1f355 1f32d 1f96a 1f32e 1f32f 1f959 1f9c6 1f95c 1f368 1f366 1f367 "
"1f370 1f382 1f36b 1f36c 1f36d 1f36e 1f36f 1f37f 1f9c1 1f375 2615 1f37a 1f37b 1f377 1f378 1f379 1f37e 1f942";

const char* kActivity =
"26bd 1f3c0 1f3c8 26be 1f94e 1f3be 1f3d0 1f3c9 1f94f 1f3b1 1f3d3 1f3f8 1f3d2 1f3d1 1f94d 1f3cf 26f3 1f3f9 "
"1f3a3 1f94a 1f94b 1f3bd 1f6f9 1f3bf 26f8 1f3c2 1f3c6 1f947 1f948 1f949 1f3c5 1f3ab 1f3aa 1f3ad 1f3a8 1f3ac "
"1f3a4 1f3a7 1f3bc 1f3b9 1f941 1f3b7 1f3ba 1f3b8 1f3bb 1f3b2 1f3af 1f3b3 1f3ae 1f579 1f3b0 1f9e9 1f0cf 1f386 "
"1f387 1f9e8 1f388 1f389 1f38a 1f380 1f381";

const char* kTravel =
"1f697 1f695 1f699 1f68c 1f3ce 1f693 1f691 1f692 1f690 1f69a 1f69b 1f69c 1f6f4 1f6b2 1f6f5 1f3cd 1f6fa 1f68d "
"1f684 1f685 1f682 2708 1f6eb 1f6ec 1f681 1f680 26f5 1f6a4 1f6e5 2693 1f6a8 1f30d 1f30e 1f30f 1f5fa 1f5fe "
"1f3d4 26f0 1f30b 1f3d5 1f3d6 1f3dc 1f3dd 1f3de 1f3d8 1f3e0 1f3ef 1f3f0 1f3a1 1f3a2 1f3a0 26f2 1f5fc 1f5fd "
"1f320 1f30c 1f307 1f306 1f3d9 1f303 2600 1f31e 1f319 2b50 1f30a";

const char* kObjects =
"231a 1f4f1 1f4bb 2328 1f5a5 1f5a8 1f4f7 1f4f8 1f4f9 1f3a5 1f4fd 1f4de 260e 1f4df 1f4e0 1f4fa 1f4fb 23f0 231b "
"23f3 1f4a1 1f526 1f56f 1f4b0 1f4b5 1f4b8 1f48e 1f527 1f528 1f4a3 1f52b 1f489 1f48a 1f6aa 1f511 1f512 1f513 "
"1f4d5 1f4d7 1f4d8 1f4d9 1f4da 1f4d3 1f4d2 1f4c5 1f4ce 2702 1f4cc 1f4cd 1f9ee 1f9f2 1f9ff 1f48c 1f4e9 2709 "
"1f381 1f388 1f3ee 1f9f8 1f9f5 1f9e6 1f455 1f456 1f9e3 1f9e4 1f452 1f3a9 1f451 1f48d 1f45c 1f45b 1f392 1f97b";

// Лента категорий: иконка + коды (индекс 0 — недавние, данные подставляются).
struct CatDef { const char* icon; const char* title; const char* data; };
const CatDef kCategories[] = {
    {"🕐", "Недавние",     nullptr},
    {"😀", "Смайлы",       kFaces},
    {"👋", "Жесты",        kGestures},
    {"❤️", "Символы",      kHearts},
    {"🐻", "Животные",     kAnimals},
    {"🍔", "Еда",          kFood},
    {"⚽", "Активности",   kActivity},
    {"🚗", "Путешествия",  kTravel},
    {"💡", "Предметы",     kObjects},
};

} // namespace

EmojiPicker::EmojiPicker(QWidget* parent) : QFrame(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setObjectName(QStringLiteral("emojiPicker"));
    setFixedSize(420, 400);
    setStyleSheet(QStringLiteral(R"QSS(
#emojiPicker { background:#1A1822; border:1px solid rgba(255,255,255,0.12); border-radius:14px; }
QLineEdit { background:#131218; border:1px solid rgba(255,255,255,0.10); border-radius:10px;
  min-height:30px; padding:0 10px; color:#F3F1F8; font-size:13px; }
QLineEdit:focus { border:1px solid #8B5CF6; }
QPushButton.tab, QPushButton.cat {
    border:none; background:transparent; font-size:17px; padding:4px; border-radius:8px;
    font-family:"Segoe UI Emoji","Noto Color Emoji",sans-serif;
}
QPushButton.tab:hover, QPushButton.cat:hover { background:rgba(255,255,255,0.08); }
QPushButton.cat:checked { background:rgba(139,92,246,0.22); }
QPushButton.del { border:none; background:transparent; color:#ACA6BD; font-size:15px;
  border-radius:8px; min-width:32px; min-height:32px; }
QPushButton.del:hover { background:rgba(255,255,255,0.08); color:#F3F1F8; }
QScrollArea { background:transparent; border:none; }
QScrollBar:vertical { background:transparent; width:8px; margin:2px; }
QScrollBar::handle:vertical { background:rgba(255,255,255,0.14); border-radius:4px; min-height:30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
)QSS"));

    buildCategories();
    loadRecents();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Строка заголовка: поиск + ⌫ (как tg-emoji-tabs веба) ─────────────────
    auto* head = new QHBoxLayout();
    head->setSpacing(4);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Поиск…"));
    search_->setClearButtonEnabled(true);
    head->addWidget(search_, 1);
    auto* del = new QPushButton(QStringLiteral("⌫"), this);
    del->setProperty("class", "del");
    del->setCursor(Qt::PointingHandCursor);
    del->setToolTip(QStringLiteral("Удалить символ"));
    head->addWidget(del);
    root->addLayout(head);

    // ── Лента категорий ───────────────────────────────────────────────────────
    auto* catRow = new QHBoxLayout();
    catRow->setSpacing(2);
    for (int i = 0; i < categories_.size(); ++i) {
        auto* t = new QPushButton(QString::fromUtf8(kCategories[i].icon), this);
        t->setProperty("class", "cat");
        t->setCheckable(true);
        t->setToolTip(QString::fromUtf8(kCategories[i].title));
        t->setCursor(Qt::PointingHandCursor);
        connect(t, &QPushButton::clicked, this, [this, i]() {
            search_->clear();
            showCategory(i);
        });
        catButtons_.append(t);
        catRow->addWidget(t);
    }
    catRow->addStretch();
    root->addLayout(catRow);

    // ── Сетка ─────────────────────────────────────────────────────────────────
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid_ = new QWidget();
    grid_->setStyleSheet(QStringLiteral("background:transparent;"));
    scroll_->setWidget(grid_);
    root->addWidget(scroll_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (t.trimmed().isEmpty()) { showCategory(current_ <= 0 ? 1 : current_); return; }
        showSearchResults(t.trimmed());
    });
    connect(del, &QPushButton::clicked, this, &EmojiPicker::backspacePressed);

    showCategory(1);
}

void EmojiPicker::buildCategories() {
    categories_.clear();
    categories_.append(QStringList());   // 0 — недавние (заполняется в loadRecents)
    for (const CatDef& c : kCategories) {
        if (!c.data) continue;
        categories_.append(QString::fromUtf8(c.data).split(' ', Qt::SkipEmptyParts));
    }
}

void EmojiPicker::loadRecents() {
    // Недавние хранятся JSON-массивом (Prefs, ключ 1:1 с localStorage веба).
    recents_.clear();
    const QJsonArray arr = QJsonDocument::fromJson(
        Prefs::getStr(QStringLiteral("xipher_recent_emoji")).toUtf8()).array();
    for (const QJsonValue& v : arr) recents_.append(v.toString());
    categories_[0] = recents_;
}

void EmojiPicker::addRecent(const QString& emoji) {
    recents_.removeAll(emoji);
    recents_.prepend(emoji);
    if (recents_.size() > 32) recents_.resize(32);   // MAX_RECENT веба
    QJsonArray arr;
    for (const QString& e : recents_) arr.append(e);
    Prefs::setStr(QStringLiteral("xipher_recent_emoji"),
                  QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    categories_[0] = recents_;
}

QWidget* EmojiPicker::makeCell(const QString& emoji, QWidget* parent) {
    auto* cell = new AnimatedEmojiLabel(emoji, 32, /*autoplay*/ false, parent);
    cell->setOnClick([this, emoji](const QString&) {
        addRecent(emoji);
        emit emojiPicked(emoji);
    });
    return cell;
}

void EmojiPicker::showCategory(int index) {
    current_ = index;
    for (int i = 0; i < catButtons_.size(); ++i)
        catButtons_[i]->setChecked(i == index);

    if (grid_->layout()) {
        QLayoutItem* it;
        while ((it = grid_->layout()->takeAt(0)) != nullptr) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        delete grid_->layout();
    }
    auto* g = new QGridLayout(grid_);
    g->setContentsMargins(2, 2, 2, 2);
    g->setSpacing(2);

    const QStringList list = categories_.value(index);
    const int cols = 9;
    for (int i = 0; i < list.size(); ++i)
        g->addWidget(makeCell(list[i], grid_), i / cols, i % cols);
    scroll_->verticalScrollBar()->setValue(0);
}

void EmojiPicker::showSearchResults(const QString& query) {
    current_ = -1;
    for (auto* b : catButtons_) b->setChecked(false);

    if (grid_->layout()) {
        QLayoutItem* it;
        while ((it = grid_->layout()->takeAt(0)) != nullptr) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        delete grid_->layout();
    }
    auto* g = new QGridLayout(grid_);
    g->setContentsMargins(2, 2, 2, 2);
    g->setSpacing(2);

    // Все категории, кроме недавних; без дублей (эмодзи встречаются дважды).
    QStringList hits;
    QSet<QString> seen;
    for (int c = 1; c < categories_.size(); ++c) {
        for (const QString& e : categories_[c]) {
            if (seen.contains(e)) continue;
            if (EmojiNames::matches(e, query)) { seen.insert(e); hits.append(e); }
        }
    }
    const int cols = 9;
    for (int i = 0; i < hits.size(); ++i)
        g->addWidget(makeCell(hits[i], grid_), i / cols, i % cols);
    scroll_->verticalScrollBar()->setValue(0);
}
