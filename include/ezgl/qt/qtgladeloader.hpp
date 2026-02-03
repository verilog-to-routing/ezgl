#pragma once

#ifdef EZGL_QT

#include <QMainWindow>
#include <QDomDocument>
#include <QDomElement>
#include <QHash>
#include <QString>

class QWidget;
class QGridLayout;

class QtGladeLoader {
public:
  QMainWindow* loadFile(const QString& uiGladePath);

private:
  QWidget* buildObjectElement(const QDomElement& objEl);
  QWidget* buildGtkWindow(const QDomElement& objEl);
  QWidget* buildGtkGrid(const QDomElement& objEl);
  QWidget* buildGtkDrawingArea(const QDomElement& objEl);
  QWidget* buildGtkButton(const QDomElement& objEl);
  QWidget* buildGtkArrow(const QDomElement& objEl);

  void applyCommonProperties(QWidget* w, const QDomElement& objEl);
  static QString getId(const QDomElement& objEl);
  static QString getClass(const QDomElement& objEl);

  static QDomElement firstChildObject(const QDomElement& childEl);
  static QDomElement findPacking(const QDomElement& childEl);
  static int packingInt(const QDomElement& packingEl, const char* name, int def);

  // helpers
  static QString propertyText(const QDomElement& objEl, const char* propName);
  static bool propertyBool(const QDomElement& objEl, const char* propName, bool def);

  QHash<QString, QWidget*> m_widgets;
};

#endif // EZGL_QT
