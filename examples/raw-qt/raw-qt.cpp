/*
 * Minimal raw-Qt example.
 *
 * Loads main_glade.ui via QtGladeLoader, shows the resulting QMainWindow, and runs
 * the Qt event loop. An event filter on the window logs key presses and mouse
 * button events to stdout.
 */

#include <iostream>
#include <string>

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMouseEvent>
#include <QObject>

#include <ezgl/qt/qtgladeloader.hpp>
#include <ezgl/qt/uiloader.hpp>

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

    // Pass --qt to build the window from the native Qt Designer form; the
    // default is the Glade-format form. Both files describe the same window,
    // so the two paths should be indistinguishable on screen.
    const bool use_qt_form = (argc > 1 && std::string(argv[1]) == "--qt");
    const char* resource = use_qt_form ? ":/main.ui" : ":/main_glade.ui";

    QMainWindow* window = nullptr;
    if (use_qt_form) {
        ezgl::UiLoader loader;
        window = loader.loadFile(resource);
    } else {
        QtGladeLoader loader;
        window = loader.loadFile(resource);
    }

    if (window == nullptr) {
        std::cerr << "Error loading UI from resource " << resource << "\n";
        return 1;
    }

    event_logger logger;
    window->installEventFilter(&logger);
    window->show();

    return app.exec();
}
