/*
 * Qt port of raw-gtk.cpp.
 *
 * Loads main.ui via QtGladeLoader, shows the resulting QMainWindow, and runs
 * the Qt event loop. An event filter on the window logs key presses and mouse
 * button events to stdout — analogous to the press_key / click_mouse callbacks
 * in the original GTK example.
 */

#include <iostream>

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMouseEvent>
#include <QObject>

#include <ezgl/qt/qtgladeloader.hpp>

namespace {

class event_logger : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        switch (ev->type()) {
            case QEvent::KeyPress: {
                auto* ke = static_cast<QKeyEvent*>(ev);
                std::cout << QKeySequence(ke->key()).toString().toStdString()
                          << " was pressed.\n";
                return false; // propagate
            }
            case QEvent::MouseButtonPress: {
                auto* me = static_cast<QMouseEvent*>(ev);
                std::cout << "User clicked mouse at "
                          << me->position().x() << ", "
                          << me->position().y() << "\n";
                return true; // consume
            }
            case QEvent::MouseButtonRelease: {
                auto* me = static_cast<QMouseEvent*>(ev);
                std::cout << "User released mouse button at "
                          << me->position().x() << ", "
                          << me->position().y() << "\n";
                return true; // consume
            }
            default:
                return QObject::eventFilter(obj, ev);
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QtGladeLoader loader;
    QMainWindow* window = loader.loadFile(":/main.ui");
    if (window == nullptr) {
        std::cerr << "Error loading UI from resource :/main.ui\n";
        return 1;
    }

    event_logger logger;
    window->installEventFilter(&logger);
    window->show();

    return app.exec();
}
