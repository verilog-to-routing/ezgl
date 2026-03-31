#ifndef EZGL_SWITCHBUTTON_HPP
#define EZGL_SWITCHBUTTON_HPP

#ifdef EZGL_QT

#include <QAbstractButton>
#include <QPropertyAnimation>

/**
 * @brief A toggle switch widget that visually and behaviourally matches GtkSwitch.
 *
 * Renders a rounded pill-shaped track whose colour interpolates from gray (OFF)
 * to blue (ON) together with a sliding white thumb.  Smooth animation is driven
 * by a QPropertyAnimation on the "position" property (0.0 = OFF, 1.0 = ON).
 *
 * The widget is checkable: use isChecked() / setChecked() / toggled(bool) exactly
 * as you would with any other QAbstractButton.
 */
class SwitchButton : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    explicit SwitchButton(QWidget* parent = nullptr);
    ~SwitchButton() override = default;

    QSize sizeHint() const override;

    qreal position() const { return m_position; }
    void setPosition(qreal pos);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Animated thumb position: 0.0 = fully left (OFF), 1.0 = fully right (ON)
    qreal m_position{0.0};
    QPropertyAnimation* m_animation;
};

#endif // EZGL_QT
#endif // EZGL_SWITCHBUTTON_HPP
