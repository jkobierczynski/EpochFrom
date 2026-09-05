#pragma once

// Wheel-safe replacements for QDoubleSpinBox/QSpinBox/QComboBox.
//
// By default, Qt spin boxes and combo boxes accept mouse-wheel scroll events
// as soon as the mouse hovers over them -- not only when they have keyboard
// focus. That means scrolling a page of options (e.g. inside a QScrollArea)
// silently changes whatever value happens to be under the cursor at the
// time, instead of scrolling the page. These subclasses ignore wheel events
// unless the widget currently has focus, which lets the event propagate up
// to the enclosing scroll area instead. Clicking into a field (giving it
// focus) still allows the wheel to adjust its value, matching how sliders
// and spin boxes behave in most desktop applications.

#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QWheelEvent>

namespace epochfrom::gui {

class NoWheelDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit NoWheelDoubleSpinBox(QWidget *parent = nullptr) : QDoubleSpinBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus()) {
            QDoubleSpinBox::wheelEvent(event);
        } else {
            event->ignore();
        }
    }
};

class NoWheelSpinBox : public QSpinBox {
    Q_OBJECT
public:
    explicit NoWheelSpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus()) {
            QSpinBox::wheelEvent(event);
        } else {
            event->ignore();
        }
    }
};

class NoWheelComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit NoWheelComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus()) {
            QComboBox::wheelEvent(event);
        } else {
            event->ignore();
        }
    }
};

class NoWheelDateEdit : public QDateEdit {
    Q_OBJECT
public:
    explicit NoWheelDateEdit(const QDate &date, QWidget *parent = nullptr) : QDateEdit(date, parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus()) {
            QDateEdit::wheelEvent(event);
        } else {
            event->ignore();
        }
    }
};

} // namespace epochfrom::gui
