#pragma once
#include <QDialog>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QList>

class QToolButton;
class QLineEdit;
class QComboBox;

// ─────────────────────────────────────────────────────────────────────────────
//  ImageEditorDialog — правка фото перед отправкой (IMG-01/02, Telegram-style):
//  кисть/маркер/ластик/стрела/прямоугольник/эллипс/текст, толщина, цвет,
//  отклонить/вернуть, экспорт PNG. Слои правок хранятся векторно и
//  растеризуются поверх исходника — оригинал не портится.
// ─────────────────────────────────────────────────────────────────────────────
class ImageEditorDialog : public QDialog {
    Q_OBJECT
public:
    // bytes — исходные байты картинки; родитель — окно приложения.
    ImageEditorDialog(const QByteArray& bytes, QWidget* parent = nullptr);

    // Итог: исходник + слои правок (PNG). Пусто = без изменений/отмена.
    QByteArray resultPng() const { return result_; }

    // Тестовые швы (design-verify).
    int strokeCount() const { return strokes_.size(); }
    void addStrokeForTest(int tool, const QPoint& a, const QPoint& b, const QString& text = QString());
    QImage renderPreview();

signals:
    // Готово к отправке (кнопка «Отправить») — результат в resultPng().
    void doneEditing();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    struct Stroke {
        int tool = 0;        // 0 кисть, 1 маркер, 2 ластик, 3 стрелка, 4 прям., 5 эллипс, 6 текст
        QPoint a, b;
        QColor color;
        int width = 3;
        QString text;
    };
    void renderStrokes(QPainter* p) const;
    void rebuildCache();
    void setTool(int t);
    void pushUndo();

    QImage src_;
    QPixmap cache_;
    QList<Stroke> strokes_;
    QList<Stroke> undo_;
    int tool_ = 0;
    QColor color_ = QColor(0xF2, 0x3F, 0x71);
    int width_ = 4;
    bool drawing_ = false;
    Stroke cur_;
    QString typingText_;
    QByteArray result_;

    QWidget* canvas_ = nullptr;
    QToolButton* toolBtns_[7] = {};
    QComboBox* widthBox_ = nullptr;
    QLineEdit* textInput_ = nullptr;
};
