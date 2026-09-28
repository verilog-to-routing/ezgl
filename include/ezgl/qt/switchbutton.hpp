#ifndef EZGL_SWITCHBUTTON_HPP
#define EZGL_SWITCHBUTTON_HPP

#include <QAbstractButton>
#include <QPropertyAnimation>

/**
 * @brief A toggle switch widget — pill-shaped track with a sliding thumb,
 * mirroring the look of modern desktop toggle switches.
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
    /// Animated thumb position.
    /// 0.0 = OFF (thumb left), 1.0 = ON (thumb right). Animated by m_animation.
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    /// Constructs a checkable toggle switch. Sets a fixed size policy and a
    /// pointing-hand cursor, and wires the toggled(bool) signal to animate
    /// "position" between 0.0 and 1.0 (150 ms, ease-in-out).
    /// @param parent Owning parent widget, or nullptr.
    explicit SwitchButton(QWidget* parent = nullptr);

    /// Default destructor; the QPropertyAnimation is parented to this widget
    /// and destroyed automatically.
    ~SwitchButton() override = default;

    /// @return The widget's preferred size, 58 x 26 logical pixels. The fixed
    /// size policy set in the constructor makes a layout use it as-is, and
    /// paintEvent() derives the track radius, thumb, and travel from it.
    QSize sizeHint() const override { return {58, 26}; }

    /// @return The current animated thumb position in [0.0, 1.0].
    qreal position() const { return m_position; }

    /// Sets the animated thumb position and schedules a repaint. Normally driven
    /// by the toggle animation rather than called directly.
    /// @param pos Position in [0.0, 1.0] (0.0 = OFF, 1.0 = ON).
    void setPosition(qreal pos);

protected:
    /// Paints the rounded track (colour interpolated gray→blue by position) and
    /// the sliding white thumb with a subtle drop shadow. Uses muted grays when
    /// the widget is disabled.
    void paintEvent(QPaintEvent* event) override;

private:
    // Animated thumb position: 0.0 = fully left (OFF), 1.0 = fully right (ON)
    qreal m_position{0.0};
    QPropertyAnimation* m_animation; ///< Animates the "position" property on toggle; owned via QObject parenting.
};

#endif // EZGL_SWITCHBUTTON_HPP
