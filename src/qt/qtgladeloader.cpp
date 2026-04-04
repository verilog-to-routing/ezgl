#ifdef EZGL_QT

#include "ezgl/qt/qtgladeloader.hpp"
#include "ezgl/qt/switchbutton.hpp"
#include "ezgl/qt/drawingareawidget.hpp"

#include <QFile>
#include <QWidget>
#include <QFrame>
#include <QGridLayout>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QStatusBar>
#include <QHash>
#include <QSizePolicy>
#include <QStyle>
#include <QDebug>

namespace {

QString propertyText(const QDomElement& objEl, const char* propName)
{
  for (QDomElement p = objEl.firstChildElement("property"); !p.isNull(); p = p.nextSiblingElement("property")) {
    if (p.attribute("name") == propName) {
      return p.text();
    }
  }
  return {};
}

Qt::ArrowType parseGtkArrowType(const QDomElement& objEl)
{
  const QString arrowType = propertyText(objEl, "arrow-type").trimmed().toLower();
  if (arrowType == "up") {
    return Qt::UpArrow;
  }
  if (arrowType == "down") {
    return Qt::DownArrow;
  }
  if (arrowType == "left") {
    return Qt::LeftArrow;
  }
  if (arrowType.isEmpty() || arrowType == "right") {
    return Qt::RightArrow;
  }

  qWarning() << "unsupported GtkArrow type" << arrowType << "- default to right arrow";
  return Qt::RightArrow;
}

QStyle::StandardPixmap standardPixmapForArrow(Qt::ArrowType arrowType)
{
  switch (arrowType) {
    case Qt::UpArrow:
      return QStyle::SP_ArrowUp;
    case Qt::DownArrow:
      return QStyle::SP_ArrowDown;
    case Qt::LeftArrow:
      return QStyle::SP_ArrowLeft;
    case Qt::RightArrow:
    default:
      return QStyle::SP_ArrowRight;
  }
}

} // namespace

QMainWindow* QtGladeLoader::loadFile(const QString& uiGladePath)
{
  //qDebug() << "~~~ loadFile=" << uiGladePath;
  QFile f(uiGladePath);
  if (!f.open(QIODevice::ReadOnly)) {
    qCritical() << "cannot open ui glade file" << uiGladePath;
    std::exit(1);
    return nullptr;
  }

  QDomDocument doc;
  if (!doc.setContent(&f)) {
    qCritical() << uiGladePath << "not a proper xml";
    return nullptr;
  }

  QDomElement iface = doc.documentElement();

  // Pass 1: build all GtkPopover objects first so that GtkMenuButton widgets
  // can look them up by ID when the main window is constructed.
  for (QDomElement obj = iface.firstChildElement("object");
       !obj.isNull();
       obj = obj.nextSiblingElement("object"))
  {
    if (getClass(obj) == "GtkPopover") {
      buildObjectElement(obj);
    }
  }

  // Pass 2: build the GtkWindow and return it.
  for (QDomElement obj = iface.firstChildElement("object");
       !obj.isNull();
       obj = obj.nextSiblingElement("object"))
  {
    if (getClass(obj) == "GtkWindow") {
      QWidget* w = buildObjectElement(obj);
      return qobject_cast<QMainWindow*>(w);
    }
  }

  qCritical() << uiGladePath << "contains no GtkWindow";
  return nullptr;
}

QWidget* QtGladeLoader::buildObjectElement(const QDomElement& objEl)
{
  const QString cls = getClass(objEl);
  if (cls == "GtkWindow")       return buildGtkWindow(objEl);
  if (cls == "GtkPopover")      return buildGtkPopover(objEl);
  if (cls == "GtkGrid")         return buildGtkGrid(objEl);
  if (cls == "GtkBox")          return buildGtkBox(objEl);
  if (cls == "GtkDrawingArea")  return buildGtkDrawingArea(objEl);
  if (cls == "GtkButton")       return buildGtkButton(objEl);
  if (cls == "GtkMenuButton")   return buildGtkMenuButton(objEl);
  if (cls == "GtkArrow")        return buildGtkArrow(objEl);
  if (cls == "GtkLabel")        return buildGtkLabel(objEl);
  if (cls == "GtkSpinButton")   return buildGtkSpinButton(objEl);
  if (cls == "GtkComboBoxText") return buildGtkComboBoxText(objEl);
  if (cls == "GtkCheckButton")  return buildGtkCheckButton(objEl);
  if (cls == "GtkSwitch")       return buildGtkSwitch(objEl);
  if (cls == "GtkSeparator")    return buildGtkSeparator(objEl);
  if (cls == "GtkEntry")        return buildGtkEntry(objEl);

  // Non-widget model/data types — silently skip, callers handle nullptr.
  if (cls == "GtkListStore" || cls == "GtkEntryCompletion" || cls == "GtkCellRendererText") {
    return nullptr;
  }

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

QWidget* QtGladeLoader::buildGtkPopover(const QDomElement& objEl)
{
  const QString id = getId(objEl);

  // Re-use an already-built popover (happens when pass 2 encounters one built in pass 1).
  if (m_widgets.contains(id)) {
    return m_widgets.value(id);
  }

  // GtkPopover → frameless popup widget that auto-closes on outside click.
  QFrame* frame = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
  frame->setObjectName(id);
  frame->setFrameShape(QFrame::StyledPanel);
  frame->setFrameShadow(QFrame::Raised);
  m_widgets.insert(id, frame);

  QDomElement childEl = objEl.firstChildElement("child");
  if (!childEl.isNull()) {
    QDomElement childObj = firstChildObject(childEl);
    if (!childObj.isNull()) {
      QWidget* content = buildObjectElement(childObj);
      if (content) {
        QVBoxLayout* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->addWidget(content);
      }
    }
  }

  frame->hide(); // always start hidden
  return frame;
}

QWidget* QtGladeLoader::buildGtkGrid(const QDomElement& objEl)
{
  QWidget* container = new QWidget;
  container->setObjectName(getId(objEl));
  m_widgets.insert(container->objectName(), container);

  QGridLayout* layout = new QGridLayout(container);
  layout->setContentsMargins(0,0,0,0);

  applyCommonProperties(container, objEl);

  // First pass: count how many non-placeholder children start at each row.
  // Rows with more than one child are "horizontal rows" — multiple widgets
  // sit side-by-side there, so their expand flags should not drive the
  // grid's row/column stretch (that would distort the horizontal layout).
  QHash<int, int> rowChildCount;
  for (QDomElement childEl = objEl.firstChildElement("child");
       !childEl.isNull();
       childEl = childEl.nextSiblingElement("child"))
  {
    if (firstChildObject(childEl).isNull()) continue; // <placeholder/>
    QDomElement packEl = findPacking(childEl);
    rowChildCount[packingInt(packEl, "top-attach", 0)]++;
  }

  // Second pass: build and add children.
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

    // Propagate GTK expand flags to QGridLayout stretch factors, but only
    // when this widget is the sole occupant of its row (i.e. it is in a
    // vertical slot, not part of a horizontal row of sibling widgets).
    // Applying stretch to a shared row would affect all widgets in that row
    // and distort their relative sizing.
    const bool inHorizontalRow = (rowChildCount.value(row, 0) > 1);
    if (!inHorizontalRow) {
      if (childW->sizePolicy().verticalPolicy() == QSizePolicy::Expanding ||
          childW->sizePolicy().verticalPolicy() == QSizePolicy::MinimumExpanding) {
        for (int r = row; r < row + rowSpan; ++r)
          layout->setRowStretch(r, 1);
      }
      if (childW->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding ||
          childW->sizePolicy().horizontalPolicy() == QSizePolicy::MinimumExpanding) {
        for (int c = col; c < col + colSpan; ++c)
          layout->setColumnStretch(c, 1);
      }
    }
  }

  return container;
}

QWidget* QtGladeLoader::buildGtkBox(const QDomElement& objEl)
{
  QWidget* container = new QWidget;
  const QString id = getId(objEl);
  container->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, container);

  applyCommonProperties(container, objEl);

  const QString orientation = propertyText(objEl, "orientation");
  QBoxLayout* layout;
  if (orientation == "vertical") {
    layout = new QVBoxLayout(container);
  } else {
    layout = new QHBoxLayout(container);
  }
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  for (QDomElement childEl = objEl.firstChildElement("child");
       !childEl.isNull();
       childEl = childEl.nextSiblingElement("child"))
  {
    QDomElement childObj = firstChildObject(childEl);
    if (childObj.isNull()) continue; // <placeholder/>

    QWidget* childW = buildObjectElement(childObj);
    if (!childW) continue;

    QDomElement packEl = findPacking(childEl);
    const bool expand = propertyBool(packEl, "expand", false);
    layout->addWidget(childW, expand ? 1 : 0);
  }

  return container;
}

QWidget* QtGladeLoader::buildGtkDrawingArea(const QDomElement& objEl)
{
  ezgl::DrawingAreaWidget* w = new ezgl::DrawingAreaWidget;
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

  const QDomElement childObj = firstChildObject(objEl.firstChildElement("child"));
  if (!childObj.isNull() && getClass(childObj) == "GtkArrow") {
    const Qt::ArrowType arrowType = parseGtkArrowType(childObj);
    b->setText(QString());
    b->setIcon(b->style()->standardIcon(standardPixmapForArrow(arrowType)));
  }

  return b;
}

QWidget* QtGladeLoader::buildGtkMenuButton(const QDomElement& objEl)
{
  QPushButton* btn = new QPushButton;
  const QString id = getId(objEl);
  btn->setObjectName(id);
  m_widgets.insert(id, btn);

  applyCommonProperties(btn, objEl);

  const bool hexpand = propertyBool(objEl, "hexpand", false);
  if (hexpand) {
    btn->setSizePolicy(QSizePolicy::Expanding, btn->sizePolicy().verticalPolicy());
  }

  // The label is a GtkLabel child of the GtkMenuButton.
  for (QDomElement childEl = objEl.firstChildElement("child");
       !childEl.isNull();
       childEl = childEl.nextSiblingElement("child"))
  {
    QDomElement childObj = firstChildObject(childEl);
    if (!childObj.isNull() && getClass(childObj) == "GtkLabel") {
      const QString text = propertyText(childObj, "label");
      if (!text.isEmpty()) {
        btn->setText(text);
        break;
      }
    }
  }

  // Connect button to show/hide its associated GtkPopover.
  const QString popoverName = propertyText(objEl, "popover");
  if (!popoverName.isEmpty()) {
    QWidget* popover = m_widgets.value(popoverName);
    if (popover) {
      QObject::connect(btn, &QPushButton::clicked, [btn, popover]() {
        if (popover->isVisible()) {
          popover->hide();
        } else {
          const QPoint globalPos = btn->mapToGlobal(QPoint(0, btn->height()));
          popover->move(globalPos);
          popover->show();
          popover->raise();
          popover->activateWindow();
        }
      });
    } else {
      qWarning() << "GtkMenuButton" << id << "refers to popover" << popoverName << "which was not found";
    }
  }

  return btn;
}

QWidget* QtGladeLoader::buildGtkArrow(const QDomElement& objEl)
{
  QToolButton* t = new QToolButton;
  t->setObjectName(getId(objEl));
  m_widgets.insert(t->objectName(), t);

  applyCommonProperties(t, objEl);

  t->setArrowType(parseGtkArrowType(objEl));

  return t;
}

QWidget* QtGladeLoader::buildGtkLabel(const QDomElement& objEl)
{
  QLabel* label = new QLabel;
  const QString id = getId(objEl);
  label->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, label);

  applyCommonProperties(label, objEl);

  label->setText(propertyText(objEl, "label"));

  return label;
}

QWidget* QtGladeLoader::buildGtkSpinButton(const QDomElement& objEl)
{
  QSpinBox* spin = new QSpinBox;
  const QString id = getId(objEl);
  spin->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, spin);

  applyCommonProperties(spin, objEl);

  return spin;
}

QWidget* QtGladeLoader::buildGtkComboBoxText(const QDomElement& objEl)
{
  QComboBox* combo = new QComboBox;
  const QString id = getId(objEl);
  combo->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, combo);

  applyCommonProperties(combo, objEl);

  QDomElement itemsEl = objEl.firstChildElement("items");
  for (QDomElement item = itemsEl.firstChildElement("item");
       !item.isNull();
       item = item.nextSiblingElement("item"))
  {
    combo->addItem(item.text());
  }

  return combo;
}

QWidget* QtGladeLoader::buildGtkCheckButton(const QDomElement& objEl)
{
  QCheckBox* cb = new QCheckBox;
  const QString id = getId(objEl);
  cb->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, cb);

  applyCommonProperties(cb, objEl);

  cb->setText(propertyText(objEl, "label"));
  cb->setChecked(propertyBool(objEl, "active", false));

  return cb;
}

QWidget* QtGladeLoader::buildGtkSwitch(const QDomElement& objEl)
{
  SwitchButton* sw = new SwitchButton;
  const QString id = getId(objEl);
  sw->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, sw);

  applyCommonProperties(sw, objEl);

  sw->setChecked(propertyBool(objEl, "active", false));

  return sw;
}

QWidget* QtGladeLoader::buildGtkSeparator(const QDomElement& objEl)
{
  QFrame* sep = new QFrame;
  const QString id = getId(objEl);
  sep->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, sep);

  const QString orientation = propertyText(objEl, "orientation");
  if (orientation == "vertical") {
    sep->setFrameShape(QFrame::VLine);
  } else {
    sep->setFrameShape(QFrame::HLine);
  }
  sep->setFrameShadow(QFrame::Sunken);

  return sep;
}

QWidget* QtGladeLoader::buildGtkEntry(const QDomElement& objEl)
{
  QLineEdit* entry = new QLineEdit;
  const QString id = getId(objEl);
  entry->setObjectName(id);
  if (!id.isEmpty())
    m_widgets.insert(id, entry);

  applyCommonProperties(entry, objEl);

  const QString placeholder = propertyText(objEl, "placeholder-text");
  if (!placeholder.isEmpty())
    entry->setPlaceholderText(placeholder);

  return entry;
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
