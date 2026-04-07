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
  QWidget* buildObjectElement(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkWindow(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkPopover(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkGrid(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkBox(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkDrawingArea(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkButton(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkMenuButton(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkArrow(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkLabel(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkSpinButton(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkComboBoxText(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkCheckButton(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkSwitch(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkSeparator(const QDomElement& objEl, QWidget* parent = nullptr);
  QWidget* buildGtkEntry(const QDomElement& objEl, QWidget* parent = nullptr);

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
