#pragma once
// Everything around the canvas: toolbar, node palette, inspector, code view,
// status bar and dialogs. All immediate-mode (see Widgets.hpp).
#include "Canvas.hpp"
#include "Editor.hpp"
#include <optional>
#include <string>
#include <unordered_map>

struct RunState
{
  bool running = false;
  bool repeat = true;     // REPEAT: keep scanning / ONCE: stop after one scan
  float slider = 0.5f;    // 0..1, mapped exponentially to 0.5 .. 2000 Hz
  bool stepRequested = false;

  float hz() const;
};

struct ViewState
{
  bool palette = true;
  bool dock = true;        // right dock with inspector + code
  bool code = true;        // code section inside the dock
  float dockWidth = 400.f;
  bool help = false;
  bool modbus = false;
};

namespace layout
{
inline constexpr float Toolbar = 50.f;
inline constexpr float StatusBar = 26.f;
inline constexpr float Palette = 188.f;
}  // namespace layout

class Toolbar
{
public:
  void draw(Editor &ed, RunState &run, ViewState &view, Canvas &canvas, sf::FloatRect r);
};

class Palette
{
public:
  void draw(Editor &ed, Canvas &canvas, sf::FloatRect r);
  // Ghost of a node being dragged out of the palette (drawn last).
  void drawDrag(Editor &ed);
  bool dragging() const { return dragging_.has_value(); }

private:
  std::optional<NodeSpec> dragging_;
  sf::Vector2f press_;
  float scroll_ = 0.f;
};

class Inspector
{
public:
  // Returns the height actually used.
  float draw(Editor &ed, Canvas &canvas, sf::FloatRect r);

private:
  float drawGate(Editor &ed, Canvas &canvas, const graph::Node &n, sf::FloatRect r);
  float drawTerminal(Editor &ed, Canvas &canvas, const graph::Node &n, sf::FloatRect r);
  float drawOverview(Editor &ed, Canvas &canvas, sf::FloatRect r);
  std::unordered_map<std::string, bool> hexMode_;
};

class CodeView
{
public:
  void draw(Editor &ed, Canvas &canvas, sf::FloatRect r);

private:
  float scroll_ = 0.f, scrollX_ = 0.f;
  std::string lastSelection_;
};

class StatusBar
{
public:
  void draw(Editor &ed, const RunState &run, sf::FloatRect r);
};

class ModbusDialog
{
public:
  void open(ModbusManager &mb);
  void draw(Editor &ed, ViewState &view, sf::Vector2f window);

private:
  void apply(ModbusManager &mb);
  std::string fields_[7];
  bool mode32_ = false;
};

// `canClose` is false on the frame the overlay was opened.
void drawHelp(Ui &ui, ViewState &view, sf::Vector2f window, bool canClose);
