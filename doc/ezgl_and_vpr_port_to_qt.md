# EZGL/Vpr migration to Qt plan

## Goal:
- to have a seamless incremental migration, where it is possible to validate the result (compare with the original GTK approach at each stage)
- to perform the GTK-to-Qt migration for each component individually

```mermaid
---
title: Qt port flow
---
flowchart TD
  split_to_components[Split app to components]
  isolate_components[Isolate each component with unique macro]
  do_we_have_component{Do we have component to port?}
  select_component[Select component to port]
  unhide_selected_component_code[Unhide selected component code by removing it's macro]
  port[Port selected component to Qt]
  map1to1[Map GTK/Cairo --> Qt types 1-to-1 whenever is possible]
  qtimpl[Add proper Qt implementation under #ifdef EZGL_QT]
  compile{Compiled?}
  validate{Visual result ok?}
  fix_and_polish[Fix and Polish]
  cleanup_api[Cleanup API by remove types mapping layer]
  drop_gtk[Drop GTK support or move it to separate project]

  split_to_components --> isolate_components
  isolate_components --> do_we_have_component
  do_we_have_component -->|YES| select_component
  do_we_have_component -->|NO| cleanup_api

  select_component --> unhide_selected_component_code
  
  unhide_selected_component_code --> port 
  
  port --> map1to1
  port --> qtimpl

  map1to1 --> compile
  qtimpl --> compile

  compile-->|YES| validate  
  compile-->|NO| port
  
  validate -->|YES| do_we_have_component
  validate -->|NO| fix_and_polish
  
  fix_and_polish --> validate

  cleanup_api -->|OPTIONALLY| drop_gtk
```

```mermaid
---
title: EZGL components
---
flowchart TD
  %% components
  component_scene[Scene]
  component_app[Application]
  component_window[Window]
  component_draw_surface[Drawing surface]
  component_draw_instrument[Drawing instrument]
  component_ui_widgets[UI Widgets]
  component_connections[Connections]
  component_log[Logger]

  %% gtk
  gtk_app[GtkApplication]
  gtk_window[gtk_main_window]
  cairo_surface_t[cairo_surface_t]
  cairo[Cairo]
  gtk_ui_widgets[GTK UI Widgets]
  g_signals[g_signals/callbacks]
  g_logs(g_info,g_debug)
  
  %% qt
  qapp[QApplication]
  qimage[QImage]
  qwindow[QWindow]
  qwidgets[QWidgets]
  qpainter[QPainter]
  qconnect[QObject::connect/signal/slots]
  qlogs[QInfo,QDebug]

  component_app --> gtk_app --> qapp
  component_window --> gtk_window --> qwindow
  component_draw_surface --> cairo_surface_t --> qimage
  component_draw_instrument --> cairo --> qpainter
  component_scene --> component_draw_surface
  component_scene --> component_draw_instrument
  component_ui_widgets --> gtk_ui_widgets --> qwidgets
  component_connections --> g_signals --> qconnect
  component_log --> g_logs --> qlogs

  subgraph Component [Components]
    component_scene
    component_app
    component_window
    component_ui_widgets
    component_connections
    component_log
  end
  
  subgraph Gtk [GTK]
    gtk_app
    gtk_window
    cairo_surface_t
    cairo
    gtk_ui_widgets
    g_signals
    g_logs
  end

  subgraph Qt [Qt equivalent]
    qimage
    qapp
    qwindow
    qpainter
    qwidgets
    qconnect
    qlogs
  end
```


## Idea:
- try to keep the API as close to the original as possible, because of this, a 1-to-1 mapping is preferable whenever possible. Put all mapping code into a separate file, as this is a temporary (R&D phase) solution.
- If direct type mapping is not possible, add a proper Qt implementation. If implementing a feature is complex and can be postponed (not required for the main flow, e.g. exporting the scene to PDF), wrap it with a macro and hide it.
- to get a buildable and runnable project ASAP, where the migration of each individual component will be easy to test (using the EZGL basic application from the examples).
    
## Steps:

1. Define the EZGL_QT macro and hide all GTK/Cairo headers in all source files.
2. For each component from the flow chart, hide its implementation under its own unique macro.
for example HIDE_GTK_EVENTS, HIDE_GTK_UI_WIDGETS, HIDE_CAIRO:
```cmake
    target_compile_definitions(
        ${PROJECT_NAME}
        PUBLIC
        EZGL_QT
        HIDE_GTK_EVENTS
        HIDE_GTK_UI_WIDGETS
        HIDE_CAIRO 
        # ...
    )
```



3. Make the EZGL project buildable.
4. Unhide a single component and put all effort and focus into implementing all the required API for it. For instance, we can start the port by implementing Cairo: remove the HIDE_CAIRO macro and map/implement all required Cairo API.

**Note:** Cairo requires that an Application (QApplication) class and a target Widget (QWidget) are implemented, so there will be some additional work outside the core Cairo-migration scope.

4.1 Map types 1 to 1 whenever possible.
For instance:
```code
#ifdef EZGL_QT
// ...
#include<QImage>
// ...
using cairo_surface_t = QImage;
// ...
#endif
```
Here in the code we continue using the cairo_surface_t name in the Qt application. This allows us to keep the existing API signatures unchanged. It is a temporary solution, but it helps us reach an MVP as quickly as possible. 

It makes sense to put all type mappings into a separate translation unit, let's call it:

```bash
_qtcompat.cpp
_qtcompat.h
```
4.2 If quick type mapping is not possible, add a Qt implementation, separate from the GTK implementation.
For example:

```code
void renderer::draw_text(point2d point, std::string const &text, double bound_x, double bound_y) {
// common impl
#ifdef EZGL_QT
// qt impl
#else 
// gtk impl
#endif 

// common impl
}
```
So here, API remains the same, but implementation is different based on the selected toolkit (GTK or Qt).



## Cairo -> QPainter migration

<div style="display: flex; gap: 20px;">

```mermaid
---
title: GTK
---
flowchart TD
  cairo_t
  text
  geometry
  cairo_surface_t
  widget[GTK Widget]
  
  cairo_t -->|draw API| geometry
  cairo_t -->|draw API| text
  geometry --> cairo_surface_t
  text --> cairo_surface_t

  cairo_surface_t -->|used| widget
  widget --> screen
 ```
```mermaid
---
title: Qt
---
flowchart TD
  qpainter[QPainter]
  text
  geometry
  qimage[QImage]
  
  qpainter -->|draw API| geometry
  qpainter -->|draw API| text
  
  geometry --> qimage
  text --> qimage
 
  qimage -->|used| qwidget[QWidget::paintEvent]
  qwidget --> screen
 ```
 
</div> 


- initial idea is to get a cairo-like QPainter implementation at the initial stage without advanced render optimization (such as batching primitives or using an OpenGL FrameBufferObject as a surface target device for QPainter to get OpenGL HW acceleration), so we basically copy the cairo → QPainter API 1 to 1.
**Note**: This step will not be redundant, since the remaining drawing-technology optimizations can be built on top of that API without major rewrites.

**Future optimization:**
```mermaid
---
title: Optimizations
---
flowchart TD
  qpainter[QPainter API]
  batching[QPainter API + batch]
  opengl[QPainter API + batch + OpenGL]
  
  qpainter --> batching --> opengl
```

  - batching primitives to draw batches with a shared style.
We collect all objects and store them in a container, then sort them by style.
Primitives of the same type and style are drawn in a single draw call (QPainter::drawLines() for lines, for filled rectangles we can try batching using QPainterPath)

- when a QImage is used as the rendering target, it is actually a software renderer, meaning the video card does not accelerate the rendering process. To benefit from hardware acceleration, we need to change the QImage render target to a QOpenGLFramebufferObject.

**Note**: QPainter will use the same API. The restrictions when using OpenGL are:
1. We need to initialize an OpenGL context.
2. All OpenGL calls must be made inside QOpenGLWidget::paintEvent(), so we must be able to store render objects in a container (this part will be handled during batching optimization).

```mermaid
---
title: Qt+OpenGL
---
flowchart TD
  qpainter[QPainter]
  text
  geometry
  qimage[QImage]
  qfbo[QOpenGLFrameBufferObject]
  qglwidget[QOpenGLWidget::paintEvent]
  
  qpainter -->|draw API| geometry
  qpainter -->|draw API| text
  
  geometry --> qfbo
  text --> qfbo
  
  qfbo -->|used| qglwidget  
  qfbo -->|used| qimage
  qimage --> export[Export As A File]
  qglwidget --> screen
 ```



## GTK CAIRO -> QPainter API mapping/porting



# GTK/Cairo to Qt mapping
## enum
| | Current (GTK-Cairo) | R&D (Qt-compat layer) | Final (Qt) | Role |
|-|-|-|-|-|
| | cairo_line_cap_t | using cairo_line_cap_t = Qt::PenCapStyle; | Qt::PenCapStyle
| | CAIRO_LINE_CAP_BUTT | #define CAIRO_LINE_CAP_BUTT	Qt::FlatCap | Qt::FlatCap
| | CAIRO_LINE_CAP_ROUND | #define CAIRO_LINE_CAP_ROUND Qt::RoundCap | Qt::RoundCap
| | CAIRO_LINE_CAP_SQUARE | #define CAIRO_LINE_CAP_SQUARE	Qt::SquareCap | Qt::SquareCap
| | cairo_font_slant_t | using cairo_font_slant_t = QFont::Style; | QFont::Style
| | CAIRO_FONT_SLANT_NORMAL | #define CAIRO_FONT_SLANT_NORMAL QFont::StyleNormal | QFont::StyleNormal
| | CAIRO_FONT_SLANT_ITALIC | #define CAIRO_FONT_SLANT_ITALIC QFont::StyleItalic | QFont::StyleItalic
| | CAIRO_FONT_SLANT_OBLIQUE | #define CAIRO_FONT_SLANT_OBLIQUE QFont::StyleOblique | QFont::StyleOblique

## Drawing Primitives (lines, rectangle, path, arc, circle ...)

| | Current (GTK-Cairo) | R&D (Qt-compat layer) | Final (Qt) | Role |
|-|-|-|-|-|
| | cairo_t | <code>struct cairo_t {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br>&nbsp;&nbsp;QImage* surface;<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br>&nbsp;&nbsp;QPainterPath path;<br>&nbsp;&nbsp;QFont font;<br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;<br>};</code> | <code>struct PainterContext {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br><strike>&nbsp;&nbsp;QImage* surface;</strike>// will be part of DrawableAreaWidget<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br><strike>&nbsp;&nbsp;QPainterPath path;</strike> // -> becomes local to render call scope<br>&nbsp;&nbsp;QFont font;<strike><br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;</strike> // -> becomes local to render call scope<br>};</code> | Drawing object and context
| | cairo_surface_t | QImage | | Surface to draw on |

**QPainter specific (immediate drawing calls)**
|| Current (GTK-Cairo) | R&D (Qt-compat layer) | Final(Qt) |
|-|-|-|-|
| | void cairo_fill(cairo_t* ctx); | void cairo_fill(cairo_t* ctx, Painter&); | void Painter::fill(); |
| | void cairo_stroke(cairo_t* ctx); | void cairo_stroke(cairo_t* ctx, Painter&); | void Painter::stroke(); |
| | void cairo_paint(cairo_t* ctx); | void cairo_paint(cairo_t* ctx, Painter&); | void Painter::paint(); |
| | void cairo_set_source_surface(cairo_t* cairo, cairo_surface_t* surface, double x, double y); | void cairo_set_source_surface(cairo_t* cairo, QImage* surface, double x, double y, Painter&); | void Painter::setSourceSurface(QImage* surface, double x, double y); |

**QTransform specific**
| | Current (GTK-Cairo) | R&D (Qt-compat layer) | Final(Qt) |
|-|-|-|-|
| | void cairo_save(cairo_t* ctx); | void cairo_save(cairo_t* ctx); | void Painter::save()
| | void cairo_restore(cairo_t* ctx); | void cairo_restore(cairo_t* ctx); | void Painter::restore
| | void cairo_scale(cairo_t* ctx, double sx, double sy); | void cairo_scale(cairo_t* ctx, double sx, double sy); | void Painter::scale(double sx, double sy) |

**Text specific**
| | Current (GTK-Cairo) | R&D (Qt-compat layer) | Final(Qt) | Role |
|-|-|-|-|-|
| | cairo_text_extents_t | <code>struct cairo_text_extents_t {<br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | See row below. | Describes how text is positioned <br> and how much space it occupies
| | void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents); | <code>void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents)<br>{<br>&nbsp;&nbsp;QString text = QString::fromUtf8(utf8);<br>&nbsp;&nbsp;QFontMetricsF fm(ctx->font);<br><br>&nbsp;&nbsp;// QRectF is given in logical coords, origin at baseline (like Cairo)<br>&nbsp;&nbsp;QRectF br = fm.boundingRect(text);<br><br>&nbsp;&nbsp;extents->x_bearing = br.x();<br>&nbsp;&nbsp;extents->y_bearing = br.y();<br>&nbsp;&nbsp;extents->width&nbsp;&nbsp;&nbsp;&nbsp;= br.width();<br>&nbsp;&nbsp;extents->height&nbsp;&nbsp;&nbsp;= br.height();<br><br>&nbsp;&nbsp;// Advance: how much the current point moves along the baseline<br>&nbsp;&nbsp;extents->x_advance = fm.horizontalAdvance(text);<br>&nbsp;&nbsp;extents->y_advance = 0.0; // Qt horizontal layout, so y-advance is 0<br>}</code> | <code>class TextExtents {<br>&nbsp;&nbsp;public:<br>&nbsp;&nbsp;TextExtents(const QFont& font, const char* utf8) {<br>&nbsp;&nbsp;&nbsp;&nbsp;QString text = QString::fromUtf8(utf8);<br>&nbsp;&nbsp;&nbsp;&nbsp;QFontMetricsF fm(font);<br>&nbsp;&nbsp;&nbsp;&nbsp;QRectF br = fm.boundingRect(text);<br>&nbsp;&nbsp;&nbsp;&nbsp;x_bearing = br.x();<br>&nbsp;&nbsp;&nbsp;&nbsp;y_bearing = br.y();<br>&nbsp;&nbsp;&nbsp;&nbsp;width&nbsp;&nbsp;&nbsp;&nbsp;= br.width();<br>&nbsp;&nbsp;&nbsp;&nbsp;height&nbsp;&nbsp;&nbsp;= br.height();<br>&nbsp;&nbsp;&nbsp;&nbsp;x_advance = fm.horizontalAdvance(text);<br>&nbsp;&nbsp;&nbsp;&nbsp;y_advance = 0.0;<br>&nbsp;&nbsp;}<br><br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | 
| | cairo_font_extents_t | <code>struct cairo_font_extents_t {<br>&nbsp;&nbsp;double ascent;<br>&nbsp;&nbsp;double descent;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double max_x_advance;<br>&nbsp;&nbsp;double max_y_advance;<br>};</code> | <code>class FontMetrics : public QFontMetricsF {<br>&nbsp;&nbsp;public:<br>&nbsp;&nbsp;int maxHorizontalAdvance();<br>&nbsp;&nbsp;int maxVerticalAdvance();<br>};</code> |  Font size properties 

**Rest of Cairo**
| | Current (GTK-Cairo) | R&D (Qt-compat layer) | Final(Qt) | Role |
|-|-|-|-|-|
| | int cairo_image_surface_get_width(cairo_surface_t* surface); | <code>int cairo_image_surface_get_width(cairo_surface_t* surface){<br>&nbsp;&nbsp;return surface->width();<br>}</code> | int QImage::width()</code>;
| | int cairo_image_surface_get_height(cairo_surface_t* surface); | | int QImage::height();
| | void cairo_new_path(cairo_t* ctx); | | void Painter::newPath();
| | void cairo_close_path(cairo_t* ctx); | | void Painter::closePath();
| | void cairo_move_to(cairo_t* ctx, double x, double y); | <code>void cairo_move_to(cairo_t* ctx, double x, double y){<br>&nbsp;&nbsp;// Add 0.5 for extra half-pixel accuracy<br>&nbsp;&nbsp;ctx->path.moveTo(x+0.5,y+0.5);<br>}</code> | void Painter::moveTo(double x, double y)
| | void cairo_line_to(cairo_t* ctx, double x, double y); | | Painter::lineTo(double x, double y)
| | void cairo_arc(cairo_t* cr, double xc, double yc, double radius, double angle1, double angle2); | | void Painter::arc(double xc, double yc, double radius, double angle1, double angle2)
| | void cairo_arc_negative(cairo_t* ctx, double xc, double yc, double radius, double angle1, double angle2); | | void Painter::arcNegative(double xc, double yc, double radius, double angle1, double angle2)
| | void cairo_select_font_face(cairo_t* ctx, const char* family, cairo_font_slant_t slant, cairo_font_weight_t weight); | | void Painter::setFontFace(const QString& family, QFont::Style style, QFont::Weight weight);
| | void cairo_set_dash(cairo_t* ctx, const double* pattern, int count, double offset); | | void Painter::setDash(const std::vector<double>& pattern, double offset);
| | void cairo_set_font_size(cairo_t* ctx, int size); | | void Painter::setFontSize(int size);
| | void cairo_set_line_width(cairo_t* ctx, int width); | | void Painter::setLineWidth(int width)
| | void cairo_set_line_cap(cairo_t* ctx, cairo_line_cap_t cap); | | void Painter::setLineCap(Qt::PenCapStyle cap);
| | void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b); | | void Painter::setColor(double r, double g, double b) 
| | void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a); | | void Painter::setColor(double r, double g, double b, double a)
| | void cairo_surface_destroy(cairo_surface_t* surface); | | OBSOLETE (QImage will not be raw pointer)
| | void cairo_destroy(cairo_t* cairo); | | OBSOLETE (Painter will not be raw pointer)

**Note**: moving from R&D phase to final doesn't require significant code change:
Example:

## Original code
```code
  // ...

#ifdef EZGL_USE_X11
  if(!transparency_flag && x11_display != nullptr) {
    XDrawLine(x11_display, x11_drawable, x11_context, start.x, start.y, end.x, end.y);
    return;
  }
#endif

  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, end.x, end.y);
  cairo_stroke(m_cairo);
```

## R&D code (contains cairo structure mapping)

```code
void renderer::draw_line(point2d start, point2d end)
{
  // ...

#ifdef EZGL_USE_X11
  if(!transparency_flag && x11_display != nullptr) {
    XDrawLine(x11_display, x11_drawable, x11_context, start.x, start.y, end.x, end.y);
    return;
  }
#endif

  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, end.x, end.y);

#ifdef EZGL_QT
  {
    Painter painter(m_cairo->surface);
    cairo_stroke(m_cairo, painter);
  }
#else
  cairo_stroke(m_cairo);
#endif
}
```
## Final code
```code
void renderer::draw_line(point2d start, point2d end)
{
  //...
  m_painter.moveTo(start);
  m_painter.lineTo(end);
  m_painter.stroke();
 }
```
## GTK Events to Qt events
### g_signals_connect -> QObject::connect
| GTK | Qt | 
| - | - |
|   g_signal_connect(main_canvas, "button_press_event", G_CALLBACK(press_mouse), application); | QObject::connect(main_canvas, &Canvas::mouseButtonPress, application, &Application::onMouseButtonPress) |
| g_signal_connect(zoom_fit_button, "clicked", G_CALLBACK(press_zoom_fit), application); | QObject::connect(zoom_fit_button, &QPushButton::clicked, application, &Application::press_zoom_fit);

### QObject::connect to callback
```code
void customZoomFitPressCallbackFn(Application* app, const QString& msg) {
  /* custom logic here */
  app->print_msg("run **custom** zoomFitPressCallbackFn");
  app->press_zoom_fit();
}
QObject::connect(zoom_fit_button, &QPushButton::clicked, application, [&application](){
  customZoomFitPressCallbackFn(application, "zoom_fit_button clicked");
});
```
### override virtual interface in QWidget
```code

class DrawingAreaWidget final : public QWidget {
// ...
protected:
  void paintEvent(QPaintEvent* event) override final;
  void mousePressEvent(QMouseEvent* event) override final;
  void mouseMoveEvent(QMouseEvent* event) override final;
  void keyPressEvent(QKeyEvent* event) override final;
```

### custom signals
```code
class DrawingAreaWidget final : public QWidget {
// ...
signals:
  void sizeChanged(const QSize& size);
  
protected:
  void resizeEvent(QResizeEvent* event) override final {
    emit sizeChanged(event->size());
  }
};

// ..
QObject::connect(drawing_area_widget, &DrawingAreaWidget::sizeChanged, this, [](const QSize& size) {
  qInfo() << "DrawingAreaWidget got new size" << size;
});
// ..
  
```

## GTK Widgets to Qt Widgets
| GTK | Qt | Comment |
| - | - | - |
| GObject | QObject | |
| GtkGrid | QGridLayout | |
| GtkWidget | QWidget | |
| GtkWidget(label) | QLabel | |
| GtkButton | QPushButton | |
| GtkComboBoxText | QComboBox | |
| GtkDialog | QDialog | |
| GtkApplication | QApplication | |
| GdkWindow | QWindow | |

**Note:**
If Qt equivalent miss some functionality, we subclass it and implement missing functionality.

## ETA
### milestone 1: EZGL to Qt
| Component | ETA (days) | Comment |
| - | - | - |
| Scene | 5 | cairo and cairo_surface |
| Application class | 1 |  |
| Window class | 1 |  |
| Drawing Surface class | 1 |  |
| UI Widgets | 5 |  |
| Connections | 2 | Callbacks, g_signal_connect |
| Logs | 1 | g_log | 
| Stress Benchmark | 1 | Write example benchmark application to stresstest GTK vs Qt with all primitives types
| Move out GTK | 2 | Removing commented sections of code responsible to GTK
| Polish | 3 | Final code cleanup |
| Code comments | 3 |
| Total: | 25 

### milestone 2: Vpr to Qt
| Component | ETA (days) | Comment |
| - | - | - |
| Move VPR to Qt | 4? |
| Code comments | 1 | 
| Total:| 5

### milestone 3: Render Optimization
| Component | ETA (days) | Comment |
| - | - | - |
| Batch optimization | 1 | This task could be performed out of Qt migration scope
| OpenGL draw optimization | 3 | This task could be performed out of Qt migration scope
| Total:| 4
  
## Cairo Migration Roadmap

```mermaid
---
title: Cairo Qt migration Progress
---
flowchart TD
  ezgl_frame_buildable_without_gtk[buildable without GTK/Cairo] -->
  basic_qapp[Basic QApplication] -->
  basic_window[Basic Window] -->
  basic_render_area_widget[Basic RenderAreaWidget] -->
  cairo_draw_primitives[Cairo draw geometric primitives] -->
  cairo_draw_text[Cairo draw text] -->
  drawing_area[Drawing area: Resize/Translate/Zoom] -->
  draw_example_app[EZGL Qt Example application] -->
  stress_test_benchmark[Stress test benchmark GTK vs Qt on different primitive types and styles]

  subgraph status [Done]
    ezgl_frame_buildable_without_gtk
    basic_qapp
    basic_window
    basic_render_area_widget
    cairo_draw_primitives
    cairo_draw_text
  end
 ```
![qpainter.png](https://raw.githubusercontent.com/bucketio/img7/main/2025/12/11/1765470810652-7d652e40-0b0b-48ae-907d-d591c2368dd6.png 'qpainter.png')

![ezgl_gtk.png](https://raw.githubusercontent.com/bucketio/img0/main/2025/12/11/1765470944447-8858b3bd-0d2c-47cf-981c-fad6ac68a782.png 'ezgl_gtk.png')

 

