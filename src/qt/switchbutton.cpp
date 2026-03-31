#ifdef EZGL_QT

#include "ezgl/qt/switchbutton.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QEasingCurve>

SwitchButton::SwitchButton(QWidget* parent)
    : QAbstractButton(parent)
    , m_animation(new QPropertyAnimation(this, "position", this))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_animation->setDuration(150);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        m_animation->stop();
        m_animation->setStartValue(m_position);
        m_animation->setEndValue(checked ? 1.0 : 0.0);
        m_animation->start();
    });
}

QSize SwitchButton::sizeHint() const
{
    return {58, 26};
}

void SwitchButton::setPosition(qreal pos)
{
    m_position = pos;
    update();
}

void SwitchButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect();
    const qreal h = r.height();
    const qreal radius = h / 2.0;

    // Interpolate track colour: gray (#c0bfbc) → GTK Adwaita accent blue (#3584e4)
    const QColor offColor(0xc0, 0xbf, 0xbc);
    const QColor onColor (0x35, 0x84, 0xe4);
    const QColor trackColor(
        offColor.red()   + static_cast<int>(m_position * (onColor.red()   - offColor.red())),
        offColor.green() + static_cast<int>(m_position * (onColor.green() - offColor.green())),
        offColor.blue()  + static_cast<int>(m_position * (onColor.blue()  - offColor.blue()))
    );

    // Track
    p.setBrush(trackColor);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, radius, radius);

    // Thumb — white circle with a subtle drop shadow
    const qreal margin = 2.0;
    const qreal thumbDia = h - 2.0 * margin;
    const qreal travel = r.width() - thumbDia - 2.0 * margin;
    const qreal thumbX = r.left() + margin + m_position * travel;
    const QRectF thumbRect(thumbX, r.top() + margin, thumbDia, thumbDia);

    // Shadow
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawEllipse(thumbRect.adjusted(0, 1, 0, 1));

    // Thumb face
    p.setBrush(Qt::white);
    p.drawEllipse(thumbRect);
}

#endif // EZGL_QT
