#!/usr/bin/env python3
"""Convert a Glade-format ezgl UI file to a native Qt Designer .ui form.

The conversion target is not "what Glade meant" but "what QtGladeLoader
actually builds": equivalence between the two forms is measured by loading
both and comparing the resulting widget trees, so any property the loader
ignores must be ignored here too. src/qt/qtgladeloader.cpp is the spec.

Usage:  glade2qtui.py <input main_glade.ui> <output main.ui>
"""

import sys
import xml.etree.ElementTree as ET
from xml.dom import minidom

# GtkDrawingArea and GtkSwitch have no stock Qt equivalent; they are emitted as
# placeholder QWidgets carrying this marker, which ezgl::UiLoader resolves.
EZGL_CLASS_PROPERTY = "ezglWidgetClass"
EZGL_POPUP_PROPERTY = "ezglPopupFor"

# Non-widget model types. QtGladeLoader returns nullptr for these (they have no
# Qt widget analogue -- Qt builds models in code), so they vanish here as well.
SKIPPED_CLASSES = {"GtkListStore", "GtkEntryCompletion", "GtkCellRendererText"}


def prop_text(obj, name):
    """Value of <property name=...> on this object, or None."""
    for p in obj.findall("property"):
        if p.get("name") == name:
            return p.text or ""
    return None


def prop_bool(obj, name, default=False):
    v = prop_text(obj, name)
    if v is None or v.strip() == "":
        return default
    return v.strip() in ("True", "true", "1")


def pack_int(child, name, default):
    packing = child.find("packing")
    if packing is None:
        return default
    v = prop_text(packing, name)
    if v is None:
        return default
    try:
        return int(v)
    except ValueError:
        return default


def first_object(child):
    return child.find("object")


def add_prop(widget, name, kind, value, stdset=True):
    p = ET.SubElement(widget, "property", {"name": name})
    if not stdset:
        p.set("stdset", "0")
    ET.SubElement(p, kind).text = value
    return p


def add_size_policy(widget, hpolicy, vpolicy):
    p = ET.SubElement(widget, "property", {"name": "sizePolicy"})
    sp = ET.SubElement(p, "sizepolicy", {"hsizetype": hpolicy, "vsizetype": vpolicy})
    ET.SubElement(sp, "horstretch").text = "0"
    ET.SubElement(sp, "verstretch").text = "0"


def apply_common(widget, obj):
    """Mirror QtGladeLoader::applyCommonProperties.

    'visible' is deliberately not emitted: Qt shows child widgets with their
    parent, and a form that sets visible=true on a child before the window
    exists makes it a top-level window. The Glade loader's setVisible(true) on
    an unparented-then-parented widget is a no-op in the same way.
    """
    if not prop_bool(obj, "sensitive", True):
        add_prop(widget, "enabled", "bool", "false")


def convert_object(obj, parent_el):
    """Convert one <object> and append the resulting <widget> to parent_el."""
    cls = obj.get("class")
    obj_id = obj.get("id", "")

    if cls in SKIPPED_CLASSES:
        return None

    handler = HANDLERS.get(cls)
    if handler is None:
        raise SystemExit(f"error: unsupported class {cls} (id={obj_id})")
    return handler(obj, parent_el, obj_id)


# --- individual class handlers -------------------------------------------

def conv_grid(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QWidget", "name": obj_id})
    apply_common(widget, obj)
    layout = ET.SubElement(widget, "layout",
                           {"class": "QGridLayout", "name": f"{obj_id}Layout"})
    for margin in ("leftMargin", "topMargin", "rightMargin", "bottomMargin"):
        add_prop(layout, margin, "number", "0")
    add_prop(layout, "spacing", "number", "0")

    # Pass 1: how many children start in each row. The loader suppresses
    # stretch for rows holding more than one child so a horizontal row is not
    # distorted (qtgladeloader.cpp).
    row_count = {}
    for child in obj.findall("child"):
        co = first_object(child)
        if co is None or co.get("class") in SKIPPED_CLASSES:
            continue
        row_count[pack_int(child, "top-attach", 0)] = \
            row_count.get(pack_int(child, "top-attach", 0), 0) + 1

    row_stretch, col_stretch = {}, {}
    for child in obj.findall("child"):
        co = first_object(child)
        if co is None:
            continue

        # A GtkStatusbar inside a grid is hoisted to the window's status bar by
        # the loader rather than placed in the layout.
        if co.get("class") == "GtkStatusbar":
            continue

        col = pack_int(child, "left-attach", 0)
        row = pack_int(child, "top-attach", 0)
        colspan = pack_int(child, "width", 1)
        rowspan = pack_int(child, "height", 1)

        item = ET.SubElement(layout, "item", {
            "row": str(row), "column": str(col),
            "rowspan": str(rowspan), "colspan": str(colspan)})
        if convert_object(co, item) is None:
            layout.remove(item)
            continue

        # Only a canvas ends up with an Expanding policy, and only when the
        # Glade file asked for it; everything else keeps Qt's default.
        if row_count.get(row, 0) <= 1 and co.get("class") == "GtkDrawingArea":
            if prop_bool(co, "vexpand", False):
                for r in range(row, row + rowspan):
                    row_stretch[r] = 1
            if prop_bool(co, "hexpand", False):
                for c in range(col, col + colspan):
                    col_stretch[c] = 1

    if row_stretch:
        n = max(row_stretch) + 1
        layout.set("rowstretch", ",".join(str(row_stretch.get(r, 0)) for r in range(n)))
    if col_stretch:
        n = max(col_stretch) + 1
        layout.set("columnstretch", ",".join(str(col_stretch.get(c, 0)) for c in range(n)))
    return widget


def conv_box(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QWidget", "name": obj_id})
    apply_common(widget, obj)
    vertical = (prop_text(obj, "orientation") or "").strip() == "vertical"
    layout = ET.SubElement(widget, "layout", {
        "class": "QVBoxLayout" if vertical else "QHBoxLayout",
        "name": f"{obj_id}Layout"})
    for margin in ("leftMargin", "topMargin", "rightMargin", "bottomMargin"):
        add_prop(layout, margin, "number", "0")
    add_prop(layout, "spacing", "number", "0")

    entries = []   # (position, stretch, item element)
    for child in obj.findall("child"):
        co = first_object(child)
        if co is None:
            continue
        item = ET.SubElement(layout, "item")
        if convert_object(co, item) is None:
            layout.remove(item)
            continue
        entries.append((pack_int(child, "position", -1),
                        1 if prop_bool(child.find("packing"), "expand", False) else 0,
                        item))

    # GTK applies <position> as gtk_box_reorder_child *after* appending: the
    # just-added child moves to absolute slot N and shifts whoever was there.
    # Mirror qtgladeloader.cpp's takeAt/insertItem so a form whose positions
    # disagree with document order still lays out the way the loader renders it.
    result = []
    for pos, stretch, item in entries:
        result.append((stretch, item))
        current = len(result) - 1
        if 0 <= pos < current:
            result.insert(pos, result.pop(current))

    for _, item in result:
        layout.remove(item)
    for _, item in result:
        layout.append(item)

    if any(stretch for stretch, _ in result):
        layout.set("stretch", ",".join(str(stretch) for stretch, _ in result))
    return widget


def conv_drawing_area(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QWidget", "name": obj_id})
    add_prop(widget, EZGL_CLASS_PROPERTY, "string", "DrawingAreaWidget", stdset=False)
    hexpand = "Expanding" if prop_bool(obj, "hexpand", False) else "Preferred"
    vexpand = "Expanding" if prop_bool(obj, "vexpand", False) else "Preferred"
    add_size_policy(widget, hexpand, vexpand)
    apply_common(widget, obj)
    return widget


def conv_switch(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QWidget", "name": obj_id})
    add_prop(widget, EZGL_CLASS_PROPERTY, "string", "SwitchButton", stdset=False)
    if prop_bool(obj, "active", False):
        add_prop(widget, "checked", "bool", "true")
    apply_common(widget, obj)
    return widget


ARROW_TO_QT = {"up": "UpArrow", "down": "DownArrow",
               "left": "LeftArrow", "right": "RightArrow"}


def conv_button(obj, parent_el, obj_id):
    # A GtkButton wrapping a GtkArrow renders as an arrow icon on the button;
    # the loader drops the child, so the arrow's id disappears here too.
    arrow = None
    for child in obj.findall("child"):
        co = first_object(child)
        if co is not None and co.get("class") == "GtkArrow":
            arrow = co
            break

    widget = ET.SubElement(parent_el, "widget", {"class": "QPushButton", "name": obj_id})
    apply_common(widget, obj)
    if arrow is None:
        label = prop_text(obj, "label")
        if label:
            add_prop(widget, "text", "string", label)
    else:
        direction = (prop_text(arrow, "arrow-type") or "right").strip()
        add_prop(widget, "text", "string", "")
        add_prop(widget, "ezglArrowType", "string",
                 ARROW_TO_QT.get(direction, "RightArrow"), stdset=False)
    return widget


def conv_menu_button(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QPushButton", "name": obj_id})
    apply_common(widget, obj)
    for child in obj.findall("child"):
        co = first_object(child)
        if co is not None and co.get("class") == "GtkLabel":
            text = prop_text(co, "label")
            if text:
                add_prop(widget, "text", "string", text)
            break
    if prop_bool(obj, "hexpand", False):
        add_size_policy(widget, "Expanding", "Fixed")
    return widget


def conv_arrow(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QToolButton", "name": obj_id})
    apply_common(widget, obj)
    direction = (prop_text(obj, "arrow-type") or "right").strip()
    add_prop(widget, "arrowType", "enum", f"Qt::{ARROW_TO_QT.get(direction, 'RightArrow')}")
    return widget


def conv_label(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QLabel", "name": obj_id})
    apply_common(widget, obj)
    add_prop(widget, "text", "string", prop_text(obj, "label") or "")
    return widget


def conv_spin(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QSpinBox", "name": obj_id})
    apply_common(widget, obj)
    return widget


def conv_combo(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QComboBox", "name": obj_id})
    apply_common(widget, obj)
    items = obj.find("items")
    if items is not None:
        for it in items.findall("item"):
            item_el = ET.SubElement(widget, "item")
            add_prop(item_el, "text", "string", it.text or "")
    return widget


def conv_check(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QCheckBox", "name": obj_id})
    apply_common(widget, obj)
    add_prop(widget, "text", "string", prop_text(obj, "label") or "")
    if prop_bool(obj, "active", False):
        add_prop(widget, "checked", "bool", "true")
    return widget


def conv_separator(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QFrame", "name": obj_id})
    vertical = (prop_text(obj, "orientation") or "").strip() == "vertical"
    add_prop(widget, "frameShape", "enum",
             "QFrame::VLine" if vertical else "QFrame::HLine")
    add_prop(widget, "frameShadow", "enum", "QFrame::Sunken")
    return widget


def conv_entry(obj, parent_el, obj_id):
    widget = ET.SubElement(parent_el, "widget", {"class": "QLineEdit", "name": obj_id})
    apply_common(widget, obj)
    placeholder = prop_text(obj, "placeholder-text")
    if placeholder:
        add_prop(widget, "placeholderText", "string", placeholder)
    return widget


HANDLERS = {
    "GtkGrid": conv_grid,
    "GtkBox": conv_box,
    "GtkDrawingArea": conv_drawing_area,
    "GtkButton": conv_button,
    "GtkMenuButton": conv_menu_button,
    "GtkArrow": conv_arrow,
    "GtkLabel": conv_label,
    "GtkSpinButton": conv_spin,
    "GtkComboBoxText": conv_combo,
    "GtkCheckButton": conv_check,
    "GtkSwitch": conv_switch,
    "GtkSeparator": conv_separator,
    "GtkEntry": conv_entry,
}


def find_status_bar_id(window_obj):
    """The loader names the status bar after the GtkStatusbar in the form, or
    'StatusBar' when the form declares none."""
    for sb in window_obj.iter("object"):
        if sb.get("class") == "GtkStatusbar":
            return sb.get("id", "StatusBar")
    return "StatusBar"


def convert(in_path, out_path):
    tree = ET.parse(in_path)
    root = tree.getroot()

    window = None
    popovers = []
    for obj in root.findall("object"):
        if obj.get("class") == "GtkWindow":
            window = obj
        elif obj.get("class") == "GtkPopover":
            popovers.append(obj)
    if window is None:
        raise SystemExit(f"error: {in_path} contains no GtkWindow")

    ui = ET.Element("ui", {"version": "4.0"})
    ET.SubElement(ui, "class").text = window.get("id", "MainWindow")
    win_el = ET.SubElement(ui, "widget",
                           {"class": "QMainWindow", "name": window.get("id", "MainWindow")})

    geom = ET.SubElement(win_el, "property", {"name": "geometry"})
    rect = ET.SubElement(geom, "rect")
    ET.SubElement(rect, "x").text = "0"
    ET.SubElement(rect, "y").text = "0"
    ET.SubElement(rect, "width").text = prop_text(window, "default-width") or "800"
    ET.SubElement(rect, "height").text = prop_text(window, "default-height") or "600"

    title = prop_text(window, "title")
    if title:
        p = ET.SubElement(win_el, "property", {"name": "windowTitle"})
        ET.SubElement(p, "string", {"notr": "true"}).text = title

    central_el = None
    child = window.find("child")
    if child is not None:
        co = first_object(child)
        if co is not None:
            central_el = convert_object(co, win_el)

    # The loader gives every window a status bar whether or not the form asks.
    ET.SubElement(win_el, "widget",
                  {"class": "QStatusBar", "name": find_status_bar_id(window)})

    # Popovers are top-level objects in Glade. Emit each as a hidden QFrame
    # child of the window tagged with the button that opens it; ezgl::UiLoader
    # applies Qt::Popup and wires the button after the tree is built.
    button_for = {}
    for obj in window.iter("object"):
        if obj.get("class") == "GtkMenuButton":
            target = prop_text(obj, "popover")
            if target:
                button_for[target] = obj.get("id", "")

    # Popovers must be declared *inside* the central widget, not as further
    # children of the QMainWindow: a .ui file only accepts specific roles under
    # a main window (central widget, menu bar, status bar, tool bars, docks),
    # and QUiLoader silently drops anything else. They sit outside the central
    # widget's layout, so they take up no space, and ezgl::UiLoader reparents
    # each one to the window as a Qt::Popup once the tree is built.
    popover_parent = central_el if central_el is not None else win_el
    for pop in popovers:
        pop_id = pop.get("id", "")
        frame = ET.SubElement(popover_parent, "widget", {"class": "QFrame", "name": pop_id})
        add_prop(frame, "visible", "bool", "false")
        add_prop(frame, EZGL_POPUP_PROPERTY, "string",
                 button_for.get(pop_id, ""), stdset=False)
        add_prop(frame, "frameShape", "enum", "QFrame::StyledPanel")
        add_prop(frame, "frameShadow", "enum", "QFrame::Raised")
        layout = ET.SubElement(frame, "layout",
                               {"class": "QVBoxLayout", "name": f"{pop_id}Layout"})
        for margin in ("leftMargin", "topMargin", "rightMargin", "bottomMargin"):
            add_prop(layout, margin, "number", "4")
        pop_child = pop.find("child")
        if pop_child is not None:
            co = first_object(pop_child)
            if co is not None:
                item = ET.SubElement(layout, "item")
                convert_object(co, item)

    ET.SubElement(ui, "resources")
    ET.SubElement(ui, "connections")

    xml = minidom.parseString(ET.tostring(ui, "utf-8")).toprettyxml(indent=" ")
    xml = "\n".join(line for line in xml.split("\n") if line.strip())
    with open(out_path, "w") as f:
        f.write(xml + "\n")
    print(f"{in_path} -> {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    convert(sys.argv[1], sys.argv[2])
