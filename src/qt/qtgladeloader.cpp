#ifdef EZGL_QT

#include "ezgl/qt/qtgladeloader.hpp"
#include "ezgl/qt/_qtcompat.hpp"

#include <QFile>
#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QToolButton>
#include <QStatusBar>
#include <QSizePolicy>
#include <QDebug>

QMainWindow* QtGladeLoader::loadFile(const QString& uiGladePath)
{
  QFile f(uiGladePath);
  if (!f.open(QIODevice::ReadOnly)) {
    qCritical() << "cannot open" << uiGladePath;
    return nullptr;
  }

  QDomDocument doc;
  if (!doc.setContent(&f)) {
    qCritical() << uiGladePath << "not a proper xml";
    return nullptr;
  }

  QDomElement iface = doc.documentElement();
  QDomElement topObj = iface.firstChildElement("object");

  QWidget* w = buildObjectElement(topObj);
  QMainWindow* window = qobject_cast<QMainWindow*>(w);
  return window;
}

QWidget* QtGladeLoader::buildObjectElement(const QDomElement& objEl)
{
  const QString cls = getClass(objEl);
  if (cls == "GtkWindow")      return buildGtkWindow(objEl);
  if (cls == "GtkGrid")        return buildGtkGrid(objEl);
  if (cls == "GtkDrawingArea") return buildGtkDrawingArea(objEl);
  if (cls == "GtkButton")      return buildGtkButton(objEl);
  if (cls == "GtkArrow")       return buildGtkArrow(objEl);

  qCritical() << "unsupported class" << cls;
  return nullptr;
}

QWidget* QtGladeLoader::buildGtkWindow(const QDomElement& objEl)
{
  QMainWindow* win = new QMainWindow;
  win->setObjectName(getId(objEl));
  m_widgets.insert(win->objectName(), win);

  const QString title = propertyText(objEl, "title");
  if (!title.isEmpty()) {
    win->setWindowTitle(title);
  }

  bool okW = false, okH = false;
  int w = propertyText(objEl, "default-width").toInt(&okW);
  int h = propertyText(objEl, "default-height").toInt(&okH);
  if (okW && okH) win->resize(w, h);

  // child: the root grid
  QDomElement childEl = objEl.firstChildElement("child");
  QDomElement childObj = firstChildObject(childEl);
  QWidget* central = buildObjectElement(childObj);
  if (!central) {
    central = new QWidget;
  }

  win->setCentralWidget(central);

  QStatusBar* status_bar = new QStatusBar();
  status_bar->setObjectName("StatusBar"); // don't change the object name for status bar
  win->setStatusBar(status_bar);

  return win;
}

QWidget* QtGladeLoader::buildGtkGrid(const QDomElement& objEl)
{
  QWidget* container = new QWidget;
  container->setObjectName(getId(objEl));
  m_widgets.insert(container->objectName(), container);

  QGridLayout* layout = new QGridLayout(container);
  layout->setContentsMargins(0,0,0,0);

  applyCommonProperties(container, objEl);

  for (QDomElement childEl = objEl.firstChildElement("child");
      !childEl.isNull();
      childEl = childEl.nextSiblingElement("child"))
  {
    QDomElement childObj = firstChildObject(childEl);
    if (childObj.isNull()) {
      continue; // <placeholder/>
    }

    const QString childClass = getClass(childObj);
    const QString childId    = getId(childObj);

    if (childClass == "GtkStatusbar") {
      QMainWindow* win = qobject_cast<QMainWindow*>(m_widgets.value("MainWindow"));
      if (!win) {
        qCritical() << "no main window detected, skip creating status bar";
        continue;
      }
      QStatusBar* sb = win->statusBar();
      sb->setObjectName(childId);
      m_widgets.insert(childId, sb);
      continue;
    }

    QWidget* childW = buildObjectElement(childObj);
    if (!childW) {
      continue;
    }

    QDomElement packingEl = findPacking(childEl);
    const int col = packingInt(packingEl, "left-attach", 0);
    const int row = packingInt(packingEl, "top-attach", 0);
    const int colSpan = packingInt(packingEl, "width", 1);
    const int rowSpan = packingInt(packingEl, "height", 1);

    layout->addWidget(childW, row, col, rowSpan, colSpan);
  }

  return container;
}

QWidget* QtGladeLoader::buildGtkDrawingArea(const QDomElement& objEl)
{
  DrawingAreaWidget* w = new DrawingAreaWidget;
  w->setObjectName(getId(objEl));
  m_widgets.insert(w->objectName(), w);

  applyCommonProperties(w, objEl);

  // hexpand/vexpand -> size policy
  const bool hexpand = propertyBool(objEl, "hexpand", false);
  const bool vexpand = propertyBool(objEl, "vexpand", false);
  w->setSizePolicy(hexpand ? QSizePolicy::Expanding : QSizePolicy::Preferred,
      vexpand ? QSizePolicy::Expanding : QSizePolicy::Preferred);

  // can-focus -> focus policy
  const bool canFocus = propertyBool(objEl, "can-focus", true);
  w->setFocusPolicy(canFocus ? Qt::StrongFocus : Qt::NoFocus);

  w->setMouseTracking(true); // required for proper mouse move handling event

  return w;
}

QWidget* QtGladeLoader::buildGtkButton(const QDomElement& objEl)
{
  QPushButton* b = new QPushButton;
  b->setObjectName(getId(objEl));
  m_widgets.insert(b->objectName(), b);

  applyCommonProperties(b, objEl);

  const QString label = propertyText(objEl, "label");
  if (!label.isEmpty()) {
    b->setText(label);
  }

  return b;
}

QWidget* QtGladeLoader::buildGtkArrow(const QDomElement& objEl)
{
  QToolButton* t = new QToolButton;
  t->setObjectName(getId(objEl));
  m_widgets.insert(t->objectName(), t);

  applyCommonProperties(t, objEl);

  const QString arrowType = propertyText(objEl, "arrow-type");
  if (arrowType == "up")    t->setArrowType(Qt::UpArrow);
  if (arrowType == "down")  t->setArrowType(Qt::DownArrow);
  if (arrowType == "left")  t->setArrowType(Qt::LeftArrow);
  if (arrowType == "right") t->setArrowType(Qt::RightArrow);

  return t;
}

void QtGladeLoader::applyCommonProperties(QWidget* w, const QDomElement& objEl)
{
  if (!w) {
    return;
  }
  const bool visible = propertyBool(objEl, "visible", true);
  w->setVisible(visible);

  const bool canFocus = propertyBool(objEl, "can-focus", true);
  w->setFocusPolicy(canFocus ? Qt::StrongFocus : Qt::NoFocus);
}

QString QtGladeLoader::getId(const QDomElement& objEl)   { return objEl.attribute("id"); }
QString QtGladeLoader::getClass(const QDomElement& objEl){ return objEl.attribute("class"); }

QDomElement QtGladeLoader::firstChildObject(const QDomElement& childEl)
{
  return childEl.firstChildElement("object");
}

QDomElement QtGladeLoader::findPacking(const QDomElement& childEl)
{
  return childEl.firstChildElement("packing");
}

int QtGladeLoader::packingInt(const QDomElement& packingEl, const char* name, int def)
{
  if (packingEl.isNull()) {
    return def;
  }
  for (QDomElement p = packingEl.firstChildElement("property"); !p.isNull(); p = p.nextSiblingElement("property")) {
    if (p.attribute("name") == name) {
      bool ok = false;
      int v = p.text().toInt(&ok);
      return ok ? v : def;
    }
  }
  return def;
}

QString QtGladeLoader::propertyText(const QDomElement& objEl, const char* propName)
{
  for (QDomElement p = objEl.firstChildElement("property"); !p.isNull(); p = p.nextSiblingElement("property")) {
    if (p.attribute("name") == propName) {
      return p.text();
    }
  }
  return {};
}

bool QtGladeLoader::propertyBool(const QDomElement& objEl, const char* propName, bool def)
{
  const QString v = propertyText(objEl, propName).trimmed();
  if (v.isEmpty()) {
    return def;
  }
  return (v == "True" || v == "true" || v == "1");
}

#endif // EZGL_QT
