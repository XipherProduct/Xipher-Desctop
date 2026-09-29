#pragma once
#include <QWidget>

class QVBoxLayout;
class QPropertyAnimation;

// ─────────────────────────────────────────────────────────────────────────────
//  ModalOverlay — затемняющий слой поверх родителя с центральной карточкой
//  (как модалки в Telegram Desktop): не отдельное окно, а оверлей внутри окна.
//  Сам ресайзится под родителя, закрывается по ��лику на фон / Esc, с анимацией.
//  Контент кладётся в card() через cardLayout().
// ─────────────────────────────────────────────────────────────────────────────
class ModalOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ModalOverlay(QWidget* parent, int cardWidth = 420);

    QWidget*     card() const { return card_; }
    QVBoxLayout* cardLayout() const { return cardLayout_; }

    // Высота по содержимому (fit-content веба, свыше — кламп relayout):
    // для экранов переменной высоты вроде профиля. Фиксированные диалоги
    // не вызывают — у них снапшот cardPrefH_ по минимуму карточки.
    void enableAutoHeight() { autoHeight_ = true; }

    void showAnimated();
    void closeAnimated();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void relayout();

    QWidget*     card_;
    QVBoxLayout* cardLayout_;
    int          cardWidth_;
    int          cardPrefH_ = 0;   // желаемая высота карточки (до клампа)
    bool         autoHeight_ = false;  // fit-content: минимум задаёт контент
    qreal        dim_ = 0.0;   // прозрачность затемнения 0..1 (анимируется)
    Q_PROPERTY(qreal dim READ dim WRITE setDim)
public:
    qreal dim() const { return dim_; }
    void  setDim(qreal d) { dim_ = d; update(); }
};
