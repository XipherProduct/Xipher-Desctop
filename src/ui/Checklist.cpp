#include "ui/Checklist.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QJsonArray>
#include <QUuid>

// ── ChecklistWidget ───────────────────────────────────────────────────────────

ChecklistWidget::ChecklistWidget(const QJsonObject& payload, bool outgoing, bool canMark, QWidget* parent)
    : QWidget(parent), payload_(payload), outgoing_(outgoing), canMark_(canMark) {
    setAttribute(Qt::WA_StyledBackground, false);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 2, 0, 2);
    root->setSpacing(6);

    const QString titleText = payload_.value(QStringLiteral("title")).toString();
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(titleText.isEmpty() ? QStringLiteral("Чек-лист") : titleText, this);
    title->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;")
        .arg(outgoing_ ? QStringLiteral("#F0ECFA") : QStringLiteral("#F3F1F8")));
    progress_ = new QLabel(this);
    progress_->setStyleSheet(QString("font-size:12px;color:%1;")
        .arg(outgoing_ ? QStringLiteral("rgba(240,236,250,70%)") : QStringLiteral("#ACA6BD")));
    head->addWidget(title);
    head->addStretch();
    head->addWidget(progress_);
    root->addLayout(head);

    itemsBox_ = new QVBoxLayout();
    itemsBox_->setSpacing(2);
    root->addLayout(itemsBox_);

    rebuild();
}

void ChecklistWidget::rebuild() {
    boxes_.clear();
    QLayoutItem* it;
    while ((it = itemsBox_->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    const QString textColor = outgoing_ ? QStringLiteral("#F0ECFA") : QStringLiteral("#F3F1F8");
    for (const QJsonValue& v : payload_.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = v.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        const QString text = item.value(QStringLiteral("text")).toString();
        const bool done = item.value(QStringLiteral("done")).toBool(false);

        auto* cb = new QCheckBox(text, this);
        cb->setChecked(done);
        cb->setEnabled(canMark_);
        cb->setCursor(Qt::PointingHandCursor);
        cb->setStyleSheet(QString(
            "QCheckBox{font-size:14px;color:%1;spacing:8px;}"
            "QCheckBox::indicator{width:18px;height:18px;border-radius:6px;"
            "border:2px solid rgba(255,255,255,35%);background:transparent;}"
            "QCheckBox::indicator:checked{background:#8B5CF6;border-color:#8B5CF6;}")
            .arg(textColor));
        boxes_.insert(id, cb);
        itemsBox_->addWidget(cb);

        connect(cb, &QCheckBox::toggled, this, [this, id](bool checked) {
            if (applying_) return;   // не зацикливать при программном обновлении
            // обновляем payload
            QJsonArray arr = payload_.value(QStringLiteral("items")).toArray();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject o = arr[i].toObject();
                if (o.value(QStringLiteral("id")).toString() == id) {
                    o.insert(QStringLiteral("done"), checked);
                    arr[i] = o;
                }
            }
            payload_.insert(QStringLiteral("items"), arr);
            updateProgress();
            emit itemToggled(id, checked);
        });
    }
    updateProgress();
}

void ChecklistWidget::updateProgress() {
    const QJsonArray arr = payload_.value(QStringLiteral("items")).toArray();
    int done = 0;
    for (const QJsonValue& v : arr)
        if (v.toObject().value(QStringLiteral("done")).toBool(false)) ++done;
    progress_->setText(QStringLiteral("%1/%2").arg(done).arg(arr.size()));
}

void ChecklistWidget::applyUpdate(const QJsonObject& update) {
    applying_ = true;
    QJsonArray arr = payload_.value(QStringLiteral("items")).toArray();

    for (const QJsonValue& uv : update.value(QStringLiteral("updates")).toArray()) {
        const QJsonObject u = uv.toObject();
        const QString id = u.value(QStringLiteral("id")).toString();
        const bool done = u.value(QStringLiteral("done")).toBool(false);
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            if (o.value(QStringLiteral("id")).toString() == id) {
                o.insert(QStringLiteral("done"), done);
                arr[i] = o;
            }
        }
        if (auto* cb = boxes_.value(id)) cb->setChecked(done);
    }

    bool added = false;
    for (const QJsonValue& av : update.value(QStringLiteral("added")).toArray()) {
        arr.append(av);
        added = true;
    }
    payload_.insert(QStringLiteral("items"), arr);
    applying_ = false;
    if (added) rebuild(); else updateProgress();
}

// ── ChecklistDialog ───────────────────────────────────────────────────────────

ChecklistEditor::ChecklistEditor(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QStringLiteral(R"QSS(
QLabel { color:#F3F1F8; }
#title { font-size:18px;font-weight:800; }
QLineEdit { background:#1A1822;border:1px solid rgba(255,255,255,10%);border-radius:10px;
            min-height:38px;padding:0 12px;color:#F3F1F8;font-size:14px; }
QLineEdit:focus { border:1px solid #8B5CF6; }
QCheckBox { color:#ACA6BD;font-size:13px;spacing:8px; }
QCheckBox::indicator{width:16px;height:16px;border-radius:5px;border:2px solid rgba(255,255,255,30%);}
QCheckBox::indicator:checked{background:#8B5CF6;border-color:#8B5CF6;}
QPushButton.primary{border:none;border-radius:10px;padding:9px 18px;font-weight:700;color:#fff;
  background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);}
QPushButton.primary:hover{background:#9B72F8;}
QPushButton.ghost{border:1px solid rgba(255,255,255,14%);border-radius:10px;padding:9px 14px;
  color:#ACA6BD;background:transparent;}
QPushButton.add{border:1px dashed rgba(255,255,255,20%);border-radius:10px;padding:8px;color:#ACA6BD;background:transparent;}
QPushButton.add:hover{color:#F3F1F8;border-color:#8B5CF6;}
)QSS"));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    auto* head = new QLabel(QStringLiteral("Новый чек-лист"), this);
    head->setObjectName(QStringLiteral("title"));
    lay->addWidget(head);

    title_ = new QLineEdit(this);
    title_->setPlaceholderText(QStringLiteral("Название чек-листа"));
    lay->addWidget(title_);

    // Пункты — в прокручиваемой области (чтобы при 20-30+ не вылезало за окно).
    itemsScroll_ = new QScrollArea(this);
    itemsScroll_->setWidgetResizable(true);
    itemsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    itemsScroll_->setMaximumHeight(280);
    itemsScroll_->setStyleSheet(QStringLiteral(
        "QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{background:transparent;width:8px;margin:2px;}"
        "QScrollBar::handle:vertical{background:rgba(255,255,255,14%);border-radius:4px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"));
    auto* itemsHost = new QWidget();
    itemsHost->setStyleSheet(QStringLiteral("background:transparent;"));
    items_ = new QVBoxLayout(itemsHost);
    items_->setContentsMargins(0, 0, 0, 0);
    items_->setSpacing(6);
    items_->addStretch();   // строки прижаты вверх
    itemsScroll_->setWidget(itemsHost);
    lay->addWidget(itemsScroll_);
    addItemRow();
    addItemRow();

    auto* addBtn = new QPushButton(QStringLiteral("+ Добавить пункт"), this);
    addBtn->setProperty("class", "add");
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, [this]() { addItemRow(); });
    lay->addWidget(addBtn);

    othersMark_ = new QCheckBox(QStringLiteral("Другие могут отмечать"), this);
    othersMark_->setChecked(true);
    othersAdd_ = new QCheckBox(QStringLiteral("Другие могут добавлять"), this);
    othersAdd_->setChecked(true);
    lay->addWidget(othersMark_);
    lay->addWidget(othersAdd_);
    lay->addStretch();

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Отмена"), this);
    cancel->setProperty("class", "ghost");
    cancel->setCursor(Qt::PointingHandCursor);
    auto* send = new QPushButton(QStringLiteral("Отправить"), this);
    send->setProperty("class", "primary");
    send->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(cancel);
    btnRow->addWidget(send);
    lay->addLayout(btnRow);

    connect(cancel, &QPushButton::clicked, this, &ChecklistEditor::cancelled);
    connect(send, &QPushButton::clicked, this, [this]() {
        QJsonArray arr;
        for (int i = 0; i < items_->count(); ++i) {
            auto* le = qobject_cast<QLineEdit*>(items_->itemAt(i)->widget());
            if (!le) continue;
            const QString t = le->text().trimmed();
            if (t.isEmpty()) continue;
            QJsonObject o;
            o.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
            o.insert(QStringLiteral("text"), t);
            o.insert(QStringLiteral("done"), false);
            arr.append(o);
        }
        if (arr.isEmpty()) return;
        QJsonObject payload{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("title"), title_->text().trimmed()},
            {QStringLiteral("othersCanMark"), othersMark_->isChecked()},
            {QStringLiteral("othersCanAdd"), othersAdd_->isChecked()},
            {QStringLiteral("items"), arr}
        };
        emit submitted(payload);
    });
}

void ChecklistEditor::addItemRow() {
    auto* le = new QLineEdit();
    le->setProperty("class", "checklist-editor-input");
    le->setPlaceholderText(QStringLiteral("Текст пункта"));
    items_->insertWidget(items_->count() - 1, le);   // перед стретчем
    le->setFocus();
    if (itemsScroll_) {
        QTimer::singleShot(0, this, [this]() {
            itemsScroll_->verticalScrollBar()->setValue(itemsScroll_->verticalScrollBar()->maximum());
        });
    }
}
