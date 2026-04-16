#ifndef EZGL_QTUTILS_HPP
#define EZGL_QTUTILS_HPP

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QWidget>

class QGridLayout;
class QBoxLayout;

namespace ezgl {
class application;
}

class Application final : public QApplication {
public:
[[deprecated("unite with ezgl::application")]]
  Application(int& argc, char** argv);
  virtual ~Application();

  void setApp(ezgl::application* app);

protected:
  // bool eventFilter(QObject* obj, QEvent* event) override final;
  bool notify(QObject* receiver, QEvent* event) override final;

private:
  ezgl::application* m_app{nullptr};
};

namespace ezgl {

inline QLineEdit* to_lineedit(QObject* w)  { return qobject_cast<QLineEdit*>(w); }

QWidget* grid_new();

QGridLayout* get_grid_layout(QWidget* grid_container);

QWidget* grid_get_child_at(QWidget* grid_container, int col, int row);

void grid_attach(QWidget* grid_container, QWidget* child, int col, int row, int w, int h);

void center_window(QWidget* w);

void widget_set_margin_start(QWidget*, int);
void widget_set_margin_end(QWidget*, int);
void widget_set_margin_top(QWidget*, int);
void widget_set_margin_bottom(QWidget*, int);

QList<QWidget*> widget_get_direct_children(QWidget* container);
void widget_set_halign(QWidget* w, Qt::AlignmentFlag flag);

void box_pack_start(QBoxLayout* box,
    QWidget* widget,
    bool expand,
    bool fill,
    int padding);

} // namespace ezgl

#endif // EZGL_QTUTILS_HPP
