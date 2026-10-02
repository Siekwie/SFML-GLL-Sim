#include "Panels.hpp"
#include "TimeUtils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

using graph::Node;
using graph::NodeKind;
using K = sf::Keyboard::Key;
namespace edits = gll::edits;

float RunState::hz() const
{
  // Exponential so low speeds (where the scan is visible) get most travel.
  return 0.5f * std::pow(4000.0f, slider);
}

// ---- helpers ------------------------------------------------------------------------

static void panelBg(Ui &ui, sf::FloatRect r)
{
  sf::RectangleShape bg(r.size);
  bg.setPosition(r.position);
  bg.setFillColor(Theme::Panel);
  ui.rt->draw(bg);
}

static void hline(Ui &ui, float x0, float x1, float y)
{
  sf::RectangleShape l({x1 - x0, 1.f});
  l.setPosition({x0, y});
  l.setFillColor(Theme::Border);
  ui.rt->draw(l);
}

static void vline(Ui &ui, float x, float y0, float y1)
{
  sf::RectangleShape l({1.f, y1 - y0});
  l.setPosition({x, y0});
  l.setFillColor(Theme::Border);
  ui.rt->draw(l);
}

static std::string trim(const std::string &s)
{
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::vector<std::string> splitList(const std::string &s)
{
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size())
  {
    size_t comma = s.find(',', start);
    std::string item = trim(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (!item.empty())
      out.push_back(item);
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return out;
}

static std::string joinList(const std::vector<std::string> &v)
{
  std::string s;
  for (size_t i = 0; i < v.size(); ++i)
    s += (i ? ", " : "") + v[i];
  return s;
}

// Rejects invalid or already used names with a status message.
static bool acceptName(Editor &ed, const std::string &name)
{
  if (!gll::isValidIdentifier(name))
  {
    ed.doc.setStatus("\"" + name + "\" is not a valid name (no spaces, commas, quotes or parentheses)");
    return false;
  }
  if (edits::isNameTaken(ed.doc.script(), name))
  {
    ed.doc.setStatus("\"" + name + "\" is already used");
    return false;
  }
  return true;
}

// Keep a node's position and selection when its key changes (rename).
static void moveKey(Editor &ed, const std::string &from, const std::string &to)
{
  auto &pos = ed.doc.positions();
  if (auto it = pos.find(from); it != pos.end())
  {
    pos[to] = it->second;
    pos.erase(it);
  }
  if (ed.selection.erase(from))
    ed.selection.insert(to);
}

static bool validTime(const std::string &s)
{
  size_t i = 0;
  bool digits = false;
  while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
    digits |= std::isdigit(static_cast<unsigned char>(s[i++])) != 0;
  std::string unit;
  for (; i < s.size(); ++i)
    unit += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  return digits && (unit.empty() || unit == "ms" || unit == "s" || unit == "m" || unit == "h");
}

static std::string modbusHint(const Node &n, const std::string &name)
{
  struct Map
  {
    NodeKind kind;
    const char *prefix;
    const char *what;
  };
  static const Map maps[] = {{NodeKind::Input, "INPUT_", "discrete input"},
                             {NodeKind::Output, "OUTPUT_", "coil"},
                             {NodeKind::AInput, "AINPUT_", "input register"},
                             {NodeKind::AOutput, "AOUTPUT_", "holding register"}};
  for (const auto &m : maps)
  {
    if (m.kind != n.kind)
      continue;
    std::string p = m.prefix;
    if (name.rfind(p, 0) == 0 && name.size() > p.size() &&
        std::all_of(name.begin() + p.size(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
      return std::string("Modbus ") + m.what + " " + name.substr(p.size());
    return std::string("Not on Modbus — name it ") + p + "<n> to map it";
  }
  return {};
}

// ---- toolbar ------------------------------------------------------------------------

static void playIcon(Ui &ui, sf::Vector2f c, bool running)
{
  if (running)
  {
    draw::roundRect(*ui.rt, {{c.x - 5.f, c.y - 6.f}, {3.5f, 12.f}}, 1.f, sf::Color::White);
    draw::roundRect(*ui.rt, {{c.x + 1.5f, c.y - 6.f}, {3.5f, 12.f}}, 1.f, sf::Color::White);
    return;
  }
  sf::ConvexShape tri(3);
  tri.setPoint(0, {c.x - 4.f, c.y - 6.5f});
  tri.setPoint(1, {c.x + 6.f, c.y});
  tri.setPoint(2, {c.x - 4.f, c.y + 6.5f});
  tri.setFillColor(sf::Color::White);
  ui.rt->draw(tri);
}

void Toolbar::draw(Editor &ed, RunState &run, ViewState &view, Canvas &canvas, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  panelBg(ui, r);
  hline(ui, r.position.x, r.position.x + r.size.x, r.position.y + r.size.y - 1.f);

  const float h = 32.f, y = r.position.y + (r.size.y - h) / 2.f;
  float x = r.position.x + 14.f;

  // Identity
  draw::text(*ui.rt, ui.bold, "GLL", {x, y + 6.f}, 15, Theme::Accent);
  x += 38.f;
  std::string file = draw::fit(ui.font, ed.doc.fileName(), 13, 170.f);
  draw::text(*ui.rt, ui.font, file, {x, y + 7.f}, 13, Theme::TextDim);
  x += std::max(80.f, draw::textWidth(ui.font, file, 13)) + 18.f;
  vline(ui, x, y + 4.f, y + h - 4.f);
  x += 14.f;

  // Simulation
  bool valid = ed.sim && ed.sim->isValidTopology();
  sf::FloatRect play({x, y}, {92.f, h});
  if (ui.button(play, "", run.running ? Ui::Style::Active : Ui::Style::Primary, valid))
    run.running = !run.running;
  playIcon(ui, {x + 20.f, y + h / 2.f}, run.running);
  draw::text(*ui.rt, ui.bold, run.running ? "Pause" : "Run", {x + 34.f, y + 7.f}, 13, sf::Color::White);
  ui.tooltip(play, "Run / pause the simulation  [Space]");
  x += 98.f;
  sf::FloatRect step({x, y}, {58.f, h});
  if (ui.button(step, "Step", Ui::Style::Normal, valid))
    run.stepRequested = true;
  ui.tooltip(step, "Evaluate the next node  [.]");
  x += 64.f;
  sf::FloatRect rep({x, y}, {74.f, h});
  if (ui.chip(rep, run.repeat ? "Repeat" : "Once", run.repeat, Theme::High))
    run.repeat = !run.repeat;
  ui.tooltip(rep, run.repeat ? "Scans repeat continuously" : "Stops after one complete scan");
  x += 86.f;

  sf::FloatRect sl({x, y + 4.f}, {120.f, h - 8.f});
  ui.slider("speed", sl, run.slider);
  ui.tooltip(sl, "Evaluation speed in nodes per second  [+/-]");
  char hz[32];
  float f = run.hz();
  snprintf(hz, sizeof(hz), f < 10.f ? "%.1f Hz" : "%.0f Hz", f);
  draw::text(*ui.rt, ui.font, hz, {x + 130.f, y + 7.f}, 13, Theme::TextDim);
  x += 196.f;
  vline(ui, x, y + 4.f, y + h - 4.f);
  x += 14.f;

  // Editing
  sf::FloatRect undo({x, y}, {58.f, h}), redo({x + 62.f, y}, {58.f, h});
  if (ui.button(undo, "Undo", Ui::Style::Ghost, ed.doc.canUndo() && ed.doc.editable()) && ed.doc.undo())
    ed.rebuild();
  ui.tooltip(undo, "Undo  [Ctrl+Z]");
  if (ui.button(redo, "Redo", Ui::Style::Ghost, ed.doc.canRedo() && ed.doc.editable()) && ed.doc.redo())
    ed.rebuild();
  ui.tooltip(redo, "Redo  [Ctrl+Y]");
  x += 132.f;
  sf::FloatRect fit({x, y}, {44.f, h}), lay({x + 48.f, y}, {92.f, h});
  if (ui.button(fit, "Fit", Ui::Style::Ghost))
    canvas.fitView();
  ui.tooltip(fit, "Fit the whole circuit in view  [F]");
  if (ui.button(lay, "Auto layout", Ui::Style::Ghost, !ed.graph.nodes.empty()))
    canvas.relayout();
  ui.tooltip(lay, "Arrange all nodes left to right by signal flow  [L]");

  // Right side
  float rx = r.position.x + r.size.x - 14.f;
  sf::FloatRect help({rx - 32.f, y}, {32.f, h});
  if (ui.button(help, "?", Ui::Style::Ghost))
    view.help = !view.help;
  ui.tooltip(help, "Shortcuts  [F1]");
  rx -= 38.f;
  sf::FloatRect dock({rx - 64.f, y}, {64.f, h});
  if (ui.chip(dock, "Panel", view.dock))
    view.dock = !view.dock;
  ui.tooltip(dock, "Inspector and code panel  [Tab]");
  rx -= 70.f;
  sf::FloatRect pal({rx - 70.f, y}, {70.f, h});
  if (ui.chip(pal, "Palette", view.palette))
    view.palette = !view.palette;
  ui.tooltip(pal, "Node palette  [P]");
  rx -= 84.f;

  bool conn = ed.modbus.isConnected();
  std::string mb = conn ? "Modbus connected" : "Modbus";
  float mw = draw::textWidth(ui.font, mb, 13) + 40.f;
  sf::FloatRect mbr({rx - mw, y}, {mw, h});
  if (ui.button(mbr, "", Ui::Style::Ghost))
    view.modbus = true;
  draw::circle(*ui.rt, {mbr.position.x + 14.f, y + h / 2.f}, 4.f, conn ? Theme::High : Theme::TextFaint);
  draw::text(*ui.rt, ui.font, mb, {mbr.position.x + 26.f, y + 7.f}, 13, conn ? Theme::TextDefault : Theme::TextDim);
  ui.tooltip(mbr, conn ? ed.modbus.getIp() + ":" + std::to_string(ed.modbus.getPort()) : "Modbus TCP settings");
}

// ---- palette ------------------------------------------------------------------------

void Palette::draw(Editor &ed, Canvas &canvas, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  panelBg(ui, r);
  vline(ui, r.position.x + r.size.x - 1.f, r.position.y, r.position.y + r.size.y);

  const auto &specs = paletteSpecs();
  float contentH = 40.f + specs.size() * 30.f + 8 * 26.f;
  if (ui.hovering(r) && ui.in.wheel != 0.f)
    scroll_ = std::clamp(scroll_ - ui.in.wheel * 40.f, 0.f, std::max(0.f, contentH - r.size.y));

  sf::View old = ui.rt->getView();
  sf::Vector2f win(ui.rt->getSize());
  sf::View clip(sf::FloatRect({r.position.x, r.position.y + scroll_}, r.size));
  clip.setViewport({{r.position.x / win.x, r.position.y / win.y}, {r.size.x / win.x, r.size.y / win.y}});
  ui.rt->setView(clip);
  sf::Vector2f mouse = ui.in.mouse + sf::Vector2f(0.f, scroll_);
  bool inside = r.contains(ui.in.mouse) && ui.interactive && !ui.menuOpen();

  float x = r.position.x + 12.f, w = r.size.x - 24.f;
  float y = r.position.y + 14.f;
  ui.sectionTitle({x, y}, "NODES");
  draw::text(*ui.rt, ui.font, "drag onto the canvas", {x + 50.f, y - 1.f}, 11, Theme::TextFaint);
  y += 24.f;

  std::string lastGroup;
  for (const auto &spec : specs)
  {
    std::string group = spec.terminal ? "SIGNALS" : gll::categoryName(gll::typeInfo(spec.type).category);
    if (group != lastGroup)
    {
      if (!lastGroup.empty())
        y += 6.f;
      std::string upper;
      for (char c : group)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      draw::text(*ui.rt, ui.bold, upper, {x, y}, 10, Theme::TextFaint);
      y += 20.f;
      lastGroup = group;
    }
    sf::FloatRect item({x, y}, {w, 26.f});
    bool hot = inside && item.contains(mouse);
    draw::roundRect(*ui.rt, item, 6.f, hot ? Theme::ButtonHover : Theme::PanelRaised);
    draw::roundRect(*ui.rt, {{x, y}, {4.f, 26.f}}, 2.f, spec.color());
    draw::text(*ui.rt, ui.bold, spec.keyword(), {x + 14.f, y + 4.f}, 12, Theme::TextDefault);
    std::string sum = spec.summary();
    auto dash = sum.find("— ");
    std::string brief = dash == std::string::npos ? sum : sum.substr(dash + std::string("— ").size());
    draw::text(*ui.rt, ui.font, draw::fit(ui.font, brief, 11, w - 66.f), {x + 56.f, y + 5.f}, 11, Theme::TextFaint);
    if (hot)
    {
      ui.tooltip({ui.in.mouse - sf::Vector2f(4, 4), {8, 8}}, spec.summary());
      if (ui.in.pressed[0] && !ui.mouseTaken)
      {
        ui.mouseTaken = true;
        dragging_ = spec;
        press_ = ui.in.mouse;
      }
    }
    y += 30.f;
  }
  ui.rt->setView(old);

  if (dragging_ && ui.in.released[0])
  {
    sf::Vector2f d = ui.in.mouse - press_;
    bool moved = d.x * d.x + d.y * d.y > 25.f;
    if (!moved)
      canvas.addNode(*dragging_);  // click: drop in the middle of the view
    else if (canvas.viewport().contains(ui.in.mouse) && !r.contains(ui.in.mouse))
      canvas.addNode(*dragging_, ui.in.mouse);
    dragging_.reset();
  }
}

void Palette::drawDrag(Editor &ed)
{
  if (!dragging_)
    return;
  Ui &ui = ed.ui;
  sf::Vector2f d = ui.in.mouse - press_;
  if (d.x * d.x + d.y * d.y < 25.f)
    return;
  sf::FloatRect ghost({ui.in.mouse.x - 60.f, ui.in.mouse.y - 16.f}, {120.f, 32.f});
  draw::roundRect(*ui.rt, ghost, 8.f, Theme::withAlpha(Theme::NodeBody, 220), dragging_->color(), 1.5f);
  draw::text(*ui.rt, ui.bold, dragging_->keyword(), {ghost.position.x + 60.f, ghost.position.y + 7.f}, 13,
             Theme::TextDefault, draw::Align::Center);
}

// ---- inspector ------------------------------------------------------------------------

namespace
{
struct Rows
{
  Ui &ui;
  float x, w, y;
  static constexpr float LabelW = 84.f, H = 28.f;

  sf::FloatRect field(const std::string &label, float rightReserve = 0.f)
  {
    draw::text(*ui.rt, ui.font, label, {x, y + 5.f}, 12, Theme::TextDim);
    sf::FloatRect f({x + LabelW, y}, {w - LabelW - rightReserve, H});
    y += H + 6.f;
    return f;
  }
  void section(const std::string &title)
  {
    y += 6.f;
    ui.sectionTitle({x, y}, title);
    y += 20.f;
  }
  void note(const std::string &s, sf::Color c = Theme::TextFaint)
  {
    draw::text(*ui.rt, ui.font, draw::fit(ui.font, s, 11, w), {x, y}, 11, c);
    y += 18.f;
  }
};
}  // namespace

float Inspector::draw(Editor &ed, Canvas &canvas, sf::FloatRect r)
{
  int sel = ed.primary();
  if (sel >= 0)
  {
    const Node &n = ed.graph.nodes[sel];
    return n.isGate() ? drawGate(ed, canvas, n, r) : drawTerminal(ed, canvas, n, r);
  }
  if (ed.selection.size() > 1)
  {
    Ui &ui = ed.ui;
    float x = r.position.x + 16.f, y = r.position.y + 16.f;
    ui.sectionTitle({x, y}, "SELECTION");
    draw::text(*ui.rt, ui.bold, std::to_string(ed.selection.size()) + " nodes selected", {x, y + 22.f}, 15,
               Theme::TextDefault);
    if (ui.button({{x, y + 54.f}, {130.f, 30.f}}, "Delete selected", Ui::Style::Danger, ed.doc.editable()))
      canvas.deleteSelection();
    return 110.f;
  }
  return drawOverview(ed, canvas, r);
}

float Inspector::drawOverview(Editor &ed, Canvas &canvas, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  Rows row{ui, r.position.x + 16.f, r.size.x - 32.f, r.position.y + 14.f};
  row.section("CIRCUIT");
  int gates = 0, ins = 0, outs = 0;
  for (const auto &n : ed.graph.nodes)
    (n.isGate() ? gates : (n.kind == NodeKind::Input || n.kind == NodeKind::AInput) ? ins : outs)++;
  draw::text(*ui.rt, ui.bold, std::to_string(gates) + (gates == 1 ? " node" : " nodes"), {row.x, row.y}, 15,
             Theme::TextDefault);
  draw::text(*ui.rt, ui.font,
             std::to_string(ins) + (ins == 1 ? " input · " : " inputs · ") + std::to_string(outs) +
                 (outs == 1 ? " output" : " outputs"),
             {row.x + 90.f, row.y + 2.f}, 13, Theme::TextDim);
  row.y += 30.f;

  // Things worth knowing about the circuit, each one clickable.
  struct Item
  {
    std::string text;
    std::string key;
    sf::Color color;
  };
  std::vector<Item> items;
  for (const auto &n : ed.graph.nodes)
  {
    for (const auto &p : n.ins)
      if (p.dangling)
        items.push_back({n.name + " reads \"" + p.symbol + "\" — nothing drives it", n.key, Theme::Warning});
    if ((n.kind == NodeKind::Output || n.kind == NodeKind::AOutput))
    {
      bool driven = std::any_of(ed.graph.edges.begin(), ed.graph.edges.end(),
                                [&](const graph::Edge &e) { return &ed.graph.nodes[e.to] == &n; });
      if (!driven)
        items.push_back({"Output " + n.name + " is not driven by any node", n.key, Theme::Warning});
    }
    if (n.isGate())
    {
      bool unconnected = std::any_of(n.ins.begin(), n.ins.end(),
                                     [](const graph::Port &p) { return p.symbol.empty() && !p.spare; });
      if (unconnected)
        items.push_back({n.name + " has unconnected inputs (read as LOW)", n.key, Theme::TextDim});
    }
  }
  // Reading a signal before it is written is normal in step sequences, so
  // it is one summary line rather than one note per wire.
  int feedback = static_cast<int>(std::count_if(ed.graph.edges.begin(), ed.graph.edges.end(),
                                                [](const graph::Edge &e) { return e.feedback; }));
  if (feedback > 0)
    items.push_back({std::to_string(feedback) + (feedback == 1 ? " wire is" : " wires are") +
                         " read before written (dashed, 1-scan delay)",
                     "", Theme::TextFaint});

  row.section(items.empty() ? "NO ISSUES" : "NOTES");
  size_t shown = 0;
  for (const auto &it : items)
  {
    if (row.y > r.position.y + r.size.y - 60.f || shown >= 14)
    {
      row.note("… and " + std::to_string(items.size() - shown) + " more");
      break;
    }
    sf::FloatRect line({row.x - 6.f, row.y - 3.f}, {row.w + 12.f, 22.f});
    if (ui.hovering(line))
      draw::roundRect(*ui.rt, line, 4.f, Theme::ButtonDefault);
    draw::circle(*ui.rt, {row.x + 3.f, row.y + 8.f}, 3.f, it.color);
    draw::text(*ui.rt, ui.font, draw::fit(ui.font, it.text, 12, row.w - 14.f), {row.x + 14.f, row.y}, 12,
               Theme::TextDefault);
    if (!it.key.empty() && ui.takeClick(line))
    {
      ed.select(it.key);
      canvas.reveal(it.key);
    }
    row.y += 22.f;
    ++shown;
  }

  row.section("GETTING AROUND");
  row.note("Click a node to inspect it. Drag from a port to wire.", Theme::TextDim);
  row.note("Right-click the canvas or a port for more options.", Theme::TextDim);
  row.note("The .gll file stays the source of truth — edit it anywhere.", Theme::TextDim);
  return row.y - r.position.y + 8.f;
}

float Inspector::drawGate(Editor &ed, Canvas &canvas, const Node &n, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  const gll::Statement &st = ed.doc.script().lines[n.line];
  const auto &info = gll::typeInfo(n.type);
  const sf::Color cat = Theme::category(info.category);
  const int line = n.line;
  const std::string key = n.key, name = n.name;
  const bool editable = ed.doc.editable();
  const uint64_t rev = ed.doc.revision();
  // A committed field rebuilds the graph and invalidates `n` / `st`: stop drawing.
  auto stale = [&] { return ed.doc.revision() != rev; };
  Rows row{ui, r.position.x + 16.f, r.size.x - 32.f, r.position.y + 16.f};

  // Title
  float kw = draw::textWidth(ui.bold, info.keyword, 12) + 14.f;
  draw::roundRect(*ui.rt, {{row.x, row.y}, {kw, 22.f}}, 5.f, Theme::withAlpha(cat, 60));
  draw::text(*ui.rt, ui.bold, info.keyword, {row.x + kw / 2.f, row.y + 2.f}, 12, cat, draw::Align::Center);
  draw::text(*ui.rt, ui.font, gll::categoryName(info.category), {row.x + kw + 10.f, row.y + 3.f}, 12, Theme::TextDim);
  draw::text(*ui.rt, ui.font, "#" + std::to_string(n.order) + " in scan · line " + std::to_string(line + 1),
             {row.x + row.w, row.y + 3.f}, 11, Theme::TextFaint, draw::Align::Right);
  row.y += 30.f;
  std::string summary = info.summary;
  auto dash = summary.find("— ");
  row.note(dash == std::string::npos ? summary : summary.substr(dash + std::string("— ").size()), Theme::TextDim);
  row.y += 4.f;

  if (ed.focusField == "name")
  {
    ui.focus("name:" + key);
    ed.focusField.clear();
  }
  ui.textField("name:" + key, row.field("Name"), name, [&ed, line, key](const std::string &v)
  {
    std::string nn = trim(v);
    if (!acceptName(ed, nn))
      return;
    moveKey(ed, key, "g:" + nn);
    ed.apply("Rename node", [&](gll::Script &s)
    {
      edits::renameGate(s, line, nn);
      return true;
    });
  }, "", editable);
  if (stale())
    return row.y - r.position.y;

  // Preset
  if (info.presetTime || info.presetValue)
  {
    std::string label = info.presetTime ? "Preset PT" : "Preset PV";
    std::string runtime = ed.sim ? (info.presetTime ? parseFloatToTimeString(ed.sim->getPresetTime(name))
                                                    : std::to_string(ed.sim->getPresetCounterValue(name)))
                                 : "";
    ui.textField("preset:" + key, row.field(label), st.preset.value_or(""), [&ed, line, info](const std::string &v)
    {
      std::string p = trim(v);
      if (!p.empty() && info.presetTime && !validTime(p))
      {
        ed.doc.setStatus("Use a time like 500ms, 2s, 1.5m or 1h");
        return;
      }
      if (!p.empty() && info.presetValue &&
          !std::all_of(p.begin(), p.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
      {
        ed.doc.setStatus("The preset value must be a whole number");
        return;
      }
      ed.apply("Change preset", [&](gll::Script &s)
      {
        edits::setPreset(s, line, p.empty() ? std::nullopt : std::optional<std::string>(p));
        return true;
      });
    }, "default " + runtime, editable);
    if (stale())
      return row.y - r.position.y;
  }

  // Inputs
  if (!n.ins.empty())
    row.section("INPUTS");
  for (const auto &p : n.ins)
  {
    if (p.spare)
    {
      if (ui.button({{row.x + Rows::LabelW, row.y}, {110.f, 26.f}}, "+ Add input", Ui::Style::Ghost, editable))
      {
        int at = p.index;
        ed.apply("Add input", [&](gll::Script &s)
        {
          edits::connectInput(s, line, at, gll::kNotConnected);
          return true;
        });
        return row.y - r.position.y;
      }
      row.y += 32.f;
      continue;
    }
    const int arg = p.index;
    const bool lit = info.literals;
    std::string label = p.label.empty() ? "In " + std::to_string(arg + 1) : p.label;
    float chips = p.literal ? 0.f : 4 * 34.f + 6.f;
    sf::FloatRect f = row.field(label, chips);
    ui.textField("arg:" + key + ":" + std::to_string(arg), f, p.symbol, [&ed, line, arg, lit](const std::string &v)
    {
      std::string sym = trim(v);
      if (sym.empty())
      {
        ed.apply("Disconnect input", [&](gll::Script &s)
        {
          edits::disconnectInput(s, line, arg);
          return true;
        });
        return;
      }
      if (lit && gll::parseLiteral(sym))
      {
        ed.apply("Set constant", [&](gll::Script &s)
        {
          edits::setInputLiteral(s, line, arg, sym);
          return true;
        });
        return;
      }
      if (!gll::isValidIdentifier(sym))
      {
        ed.doc.setStatus("\"" + sym + "\" is not a valid signal name");
        return;
      }
      ed.apply("Connect input", [&](gll::Script &s)
      {
        edits::connectInput(s, line, arg, sym);
        return true;
      });
    }, lit ? "signal or constant" : "not connected", editable);
    if (stale())
      return row.y - r.position.y;
    if (p.dangling)
      draw::circle(*ui.rt, {f.position.x + f.size.x - 10.f, f.position.y + f.size.y / 2.f}, 3.5f, Theme::Warning);

    if (!p.literal)
    {
      static const std::pair<const char *, gll::Mod> mods[] = {
          {"—", gll::Mod::None}, {"NOT", gll::Mod::Not}, {"PS", gll::Mod::Ps}, {"NS", gll::Mod::Ns}};
      float cx = f.position.x + f.size.x + 6.f;
      for (const auto &[label, mod] : mods)
      {
        sf::FloatRect c({cx, f.position.y}, {32.f, f.size.y});
        if (ui.chip(c, label, p.mod == mod, Theme::category(gll::Category::Edge)) && editable && p.mod != mod)
        {
          gll::Mod m = mod;
          ed.apply("Change input modifier", [&](gll::Script &s)
          {
            edits::setInputMod(s, line, arg, m);
            return true;
          });
          return row.y - r.position.y;
        }
        ui.tooltip(c, mod == gll::Mod::None  ? "Plain input"
                      : mod == gll::Mod::Not ? "Inverted — NOT(x)"
                      : mod == gll::Mod::Ps  ? "Rising edge pulse — PS(x)"
                                             : "Falling edge pulse — NS(x)");
        cx += 34.f;
      }
    }
  }

  // Outputs
  row.section("OUTPUTS");
  std::vector<std::string> q;
  std::string cv;
  for (size_t i = 0; i < st.outputs.size(); ++i)
  {
    if (info.cvOutput && i == 1)
      cv = st.outputs[i].symbol;
    else
      q.push_back(st.outputs[i].symbol);
  }
  const bool hasCv = info.cvOutput;
  ui.textField("q:" + key, row.field("Q"), joinList(q), [&ed, line, q, cv, hasCv, name](const std::string &v)
  {
    std::vector<std::string> nq = splitList(v);
    for (const auto &s : nq)
      if (!gll::isValidIdentifier(s))
      {
        ed.doc.setStatus("\"" + s + "\" is not a valid signal name");
        return;
      }
    // Changing one name into another unused one renames the signal, so the
    // wires stay attached.
    if (nq.size() == q.size())
    {
      int diff = -1, count = 0;
      for (size_t i = 0; i < q.size(); ++i)
        if (q[i] != nq[i])
          diff = static_cast<int>(i), ++count;
      if (count == 1 && !edits::isNameTaken(ed.doc.script(), nq[diff]))
      {
        std::string from = q[diff], to = nq[diff];
        ed.apply("Rename signal", [&](gll::Script &s)
        {
          edits::renameSymbol(s, from, to);
          return true;
        });
        return;
      }
    }
    std::vector<std::string> outs = nq;
    if (hasCv && !cv.empty())
    {
      if (outs.empty())
        outs.push_back(name + "_Q");
      outs.insert(outs.begin() + 1, cv);
    }
    ed.apply("Change outputs", [&](gll::Script &s)
    {
      edits::setOutputs(s, line, outs);
      return true;
    });
  }, "no output signal", editable);
  if (stale())
    return row.y - r.position.y;
  if (hasCv)
  {
    ui.textField("cv:" + key, row.field("CV"), cv, [&ed, line, cv](const std::string &v)
    {
      std::string nv = trim(v);
      if (nv == cv)
        return;
      if (!acceptName(ed, nv))
        return;
      ed.apply(cv.empty() ? "Add counter value output" : "Rename signal", [&](gll::Script &s)
      {
        if (cv.empty())
        {
          edits::ensureOutput(s, line, 1);
          edits::renameSymbol(s, s.lines[line].outputs[1].symbol, nv);
        }
        else
          edits::renameSymbol(s, cv, nv);
        return true;
      });
    }, "not used", editable);
    if (stale())
      return row.y - r.position.y;
  }

  // Live state
  if (ed.sim)
  {
    row.section("LIVE");
    bool high = !n.outs.empty() && n.outs[0].signal >= 0 && ed.value(n.outs[0].signal);
    std::string state = std::string("Q is ") + (high ? "HIGH" : "LOW");
    if (n.type == gll::NodeType::TON_ && high)
      state += " · delay elapsed";
    else if (info.presetTime)
    {
      char buf[96];
      snprintf(buf, sizeof(buf), " · elapsed %.2fs of %s", ed.sim->getTimerElapsed(name),
               parseFloatToTimeString(ed.sim->getPresetTime(name)).c_str());
      state += buf;
    }
    if (info.presetValue)
      state += " · CV " + std::to_string(ed.sim->getCurrentCounterValue(name)) + " / PV " +
               std::to_string(ed.sim->getPresetCounterValue(name));
    if (n.type == gll::NodeType::BTN)
      state += ed.sim->isButtonLatched(name) ? " · latched" : "";
    draw::text(*ui.rt, ui.font, state, {row.x, row.y}, 13, high ? Theme::High : Theme::TextDim);
    row.y += 26.f;
  }

  row.y += 4.f;
  if (ui.button({{row.x, row.y}, {110.f, 30.f}}, "Delete node", Ui::Style::Danger, editable))
    canvas.deleteSelection();
  return row.y + 44.f - r.position.y;
}

float Inspector::drawTerminal(Editor &ed, Canvas &canvas, const Node &n, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  const gll::Statement &st = ed.doc.script().lines[n.line];
  const gll::DeclItem item = st.items[n.item];
  const std::string key = n.key;
  const bool editable = ed.doc.editable();
  const bool analog = n.isAnalog();
  const bool input = n.kind == NodeKind::Input || n.kind == NodeKind::AInput;
  const uint64_t rev = ed.doc.revision();
  auto stale = [&] { return ed.doc.revision() != rev; };
  Rows row{ui, r.position.x + 16.f, r.size.x - 32.f, r.position.y + 16.f};

  std::string kw = gll::declKeyword(st.decl);
  sf::Color accent = analog ? Theme::AnalogTerminal : Theme::Terminal;
  float kww = draw::textWidth(ui.bold, kw, 12) + 14.f;
  draw::roundRect(*ui.rt, {{row.x, row.y}, {kww, 22.f}}, 5.f, Theme::withAlpha(accent, 60));
  draw::text(*ui.rt, ui.bold, kw, {row.x + kww / 2.f, row.y + 2.f}, 12, accent, draw::Align::Center);
  std::string what = std::string(analog ? "Analog" : "Digital") + (input ? " input" : " output");
  draw::text(*ui.rt, ui.font, what, {row.x + kww + 10.f, row.y + 3.f}, 12, Theme::TextDim);
  draw::text(*ui.rt, ui.font, "line " + std::to_string(n.line + 1), {row.x + row.w, row.y + 3.f}, 11,
             Theme::TextFaint, draw::Align::Right);
  row.y += 32.f;

  static const char *prefix[] = {"i:", "o:", "ai:", "ao:"};
  const std::string pre = prefix[static_cast<int>(st.decl)];
  if (ed.focusField == "name")
  {
    ui.focus("tname:" + key);
    ed.focusField.clear();
  }
  ui.textField("tname:" + key, row.field("Name"), item.name, [&ed, item, key, pre](const std::string &v)
  {
    std::string nn = trim(v);
    if (nn == item.name || !acceptName(ed, nn))
      return;
    moveKey(ed, key, pre + nn);
    ed.apply("Rename signal", [&](gll::Script &s)
    {
      edits::renameSymbol(s, item.name, nn);
      return true;
    });
  }, "", editable);
  if (stale())
    return row.y - r.position.y;
  const int line = n.line, idx = n.item;
  ui.textField("alias:" + key, row.field("Alias"), item.alias, [&ed, line, idx, item](const std::string &v)
  {
    std::string a = trim(v);
    if (a == item.alias || (!a.empty() && !acceptName(ed, a)))
      return;
    ed.apply(a.empty() ? "Remove alias" : "Set alias", [&](gll::Script &s)
    {
      edits::setDeclAlias(s, {line, idx}, a);
      return true;
    });
  }, "optional display name", editable);
  if (stale())
    return row.y - r.position.y;
  row.note(modbusHint(n, item.name));

  row.section("VALUE");
  if (ed.sim)
  {
    if (n.kind == NodeKind::Input)
    {
      bool on = ed.sim->getSignalValue(n.name);
      if (ui.chip({{row.x, row.y}, {70.f, 28.f}}, on ? "HIGH" : "LOW", on, Theme::High))
        ed.sim->toggleSignal(n.name);
      draw::text(*ui.rt, ui.font, "click to toggle (also on the canvas)", {row.x + 82.f, row.y + 6.f}, 12,
                 Theme::TextFaint);
      row.y += 36.f;
    }
    else if (n.kind == NodeKind::AInput)
    {
      bool hex = hexMode_[key];
      uint64_t v = ed.sim->getAnalogSignalValue(n.name);
      char buf[32];
      snprintf(buf, sizeof(buf), hex ? "%llX" : "%llu", static_cast<unsigned long long>(v));
      if (ed.focusField == "value")
      {
        ui.focus("aval:" + key);
        ed.focusField.clear();
      }
      sf::FloatRect f = row.field("Value", 2 * 44.f + 6.f);
      Simulator *sim = ed.sim;
      ModbusManager *mb = &ed.modbus;
      std::string sig = n.name;
      ui.textField("aval:" + key, f, buf, [&ed, sim, mb, sig, hex](const std::string &t)
      {
        try
        {
          size_t used = 0;
          uint64_t val = std::stoull(trim(t), &used, hex ? 16 : 10);
          uint64_t max = mb->getAnalogRegisterMode() == ModbusManager::AnalogRegisterMode::BITS_32 ? 4294967295ULL
                                                                                                  : 65535ULL;
          if (val > max)
            ed.doc.setStatus("Value exceeds the " + std::string(max > 65535 ? "32" : "16") + "-bit register range");
          else
            sim->setAnalogSignal(sig, val);
        }
        catch (...)
        {
          ed.doc.setStatus(hex ? "Enter hex digits, e.g. 7F" : "Enter a whole number");
        }
      }, hex ? "hex" : "decimal");
      float cx = f.position.x + f.size.x + 6.f;
      if (ui.chip({{cx, f.position.y}, {42.f, f.size.y}}, "DEC", !hex))
        hexMode_[key] = false;
      if (ui.chip({{cx + 46.f, f.position.y}, {42.f, f.size.y}}, "HEX", hex))
        hexMode_[key] = true;
      snprintf(buf, sizeof(buf), "%llu  ·  0x%llX", static_cast<unsigned long long>(v),
               static_cast<unsigned long long>(v));
      row.note(buf, Theme::Analog);
    }
    else
    {
      uint64_t v = ed.value(n.signal);
      char buf[48];
      if (analog)
        snprintf(buf, sizeof(buf), "%llu  ·  0x%llX", static_cast<unsigned long long>(v),
                 static_cast<unsigned long long>(v));
      else
        snprintf(buf, sizeof(buf), "%s", v ? "HIGH" : "LOW");
      draw::text(*ui.rt, ui.bold, buf, {row.x, row.y}, 14, analog ? Theme::Analog : v ? Theme::High : Theme::TextDim);
      row.y += 28.f;
    }
  }

  row.y += 6.f;
  if (ui.button({{row.x, row.y}, {110.f, 30.f}}, "Delete", Ui::Style::Danger, editable))
    canvas.deleteSelection();
  return row.y + 44.f - r.position.y;
}

// ---- code view --------------------------------------------------------------------------

void CodeView::draw(Editor &ed, Canvas &canvas, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  const auto &lines = ed.doc.diskLines();
  const auto &err = ed.doc.error();
  auto prog = ed.doc.program();
  const float lh = Theme::CodeLineHeight;
  const unsigned fs = static_cast<unsigned>(Theme::CodeFontSize);

  float x = r.position.x + 16.f, y = r.position.y + 12.f;
  ui.sectionTitle({x, y}, "CODE");
  draw::text(*ui.rt, ui.font, "live from " + ed.doc.fileName() + " — edit it in any editor", {x + 44.f, y - 1.f}, 11,
             Theme::TextFaint);
  y += 24.f;

  if (err)
  {
    std::string msg = (err->line >= 0 ? "Line " + std::to_string(err->line + 1) + ": " : "") + err->msg;
    if (msg.rfind("Line ", 0) == 0 && err->msg.rfind("Line ", 0) == 0)
      msg = err->msg;
    sf::FloatRect box({x - 4.f, y}, {r.size.x - 24.f, 46.f});
    draw::roundRect(*ui.rt, box, 6.f, Theme::withAlpha(Theme::Error, 40), Theme::Error, 1.f);
    draw::text(*ui.rt, ui.bold, draw::fit(ui.bold, msg, 12, box.size.x - 16.f), {box.position.x + 8.f, y + 5.f}, 12,
               Theme::TextDefault);
    draw::text(*ui.rt, ui.font, "The graph shows the last valid version until this is fixed.",
               {box.position.x + 8.f, y + 24.f}, 11, Theme::TextDim);
    y += 54.f;
  }

  sf::FloatRect body({r.position.x, y}, {r.size.x, r.position.y + r.size.y - y});
  if (body.size.y <= 10.f)
    return;
  const float gutter = 40.f;
  float contentH = lines.size() * lh + 20.f;

  // Keep the selected node's line in view.
  int sel = ed.primary();
  std::string selKey = sel >= 0 ? ed.graph.nodes[sel].key : "";
  if (selKey != lastSelection_ && sel >= 0)
  {
    int l = ed.graph.nodes[sel].line;
    float ly = l * lh;
    if (ly < scroll_ || ly > scroll_ + body.size.y - 2 * lh)
      scroll_ = std::max(0.f, ly - body.size.y / 3.f);
  }
  lastSelection_ = selKey;
  if (ed.revealLine >= 0)
  {
    scroll_ = std::max(0.f, ed.revealLine * lh - body.size.y / 3.f);
    ed.revealLine = -1;
  }

  if (ui.hovering(body))
  {
    if (ui.in.shift)
      scrollX_ = std::max(0.f, scrollX_ - ui.in.wheel * 40.f);
    else
      scroll_ -= ui.in.wheel * 3 * lh;
    scrollX_ = std::max(0.f, scrollX_ - ui.in.wheelH * 40.f);
  }
  scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH - body.size.y));

  sf::View old = ui.rt->getView();
  sf::Vector2f win(ui.rt->getSize());
  sf::View clip(sf::FloatRect({0.f, scroll_}, body.size));
  clip.setViewport({{body.position.x / win.x, body.position.y / win.y}, {body.size.x / win.x, body.size.y / win.y}});
  ui.rt->setView(clip);

  const float tx = gutter + 10.f - scrollX_;
  std::set<int> selectedLines;
  for (const auto &k : ed.selection)
    if (int i = ed.graph.find(k); i >= 0)
      selectedLines.insert(ed.graph.nodes[i].line);
  int scanLine = ed.sim ? ed.sim->currentEvaluatingLine() : -1;

  int first = std::max(0, static_cast<int>(scroll_ / lh) - 1);
  int last = std::min(static_cast<int>(lines.size()), static_cast<int>((scroll_ + body.size.y) / lh) + 2);
  sf::Vector2f local = ui.in.mouse - body.position + sf::Vector2f(0.f, scroll_);
  int hoverLine = ui.hovering(body) ? static_cast<int>(local.y / lh) : -1;

  for (int i = first; i < last; ++i)
  {
    float ly = i * lh;
    sf::FloatRect band({0.f, ly}, {body.size.x, lh});
    if (err && err->line == i)
      draw::roundRect(*ui.rt, band, 0.f, Theme::withAlpha(Theme::Error, 50));
    else if (selectedLines.count(i))
    {
      draw::roundRect(*ui.rt, band, 0.f, Theme::withAlpha(Theme::Accent, 40));
      draw::roundRect(*ui.rt, {{0.f, ly}, {3.f, lh}}, 0.f, Theme::Accent);
    }
    else if (i == hoverLine && i < static_cast<int>(lines.size()))
      draw::roundRect(*ui.rt, band, 0.f, Theme::withAlpha(Theme::ButtonDefault, 160));
    if (!err && i == scanLine)
    {
      draw::roundRect(*ui.rt, band, 0.f, Theme::withAlpha(Theme::Scan, ed.showScan ? 45 : 18));
      draw::roundRect(*ui.rt, {{gutter - 4.f, ly}, {3.f, lh}}, 0.f, Theme::withAlpha(Theme::Scan, ed.showScan ? 255 : 90));
    }
    draw::text(*ui.rt, ui.font, std::to_string(i + 1), {gutter - 10.f, ly + 2.f}, 12, Theme::TextFaint,
               draw::Align::Right);
    const std::string &s = lines[i];
    std::string trimmed = trim(s);
    sf::Color c = (!trimmed.empty() && trimmed[0] == '#') ? Theme::TextFaint : Theme::TextDefault;
    draw::text(*ui.rt, ui.font, s, {tx, ly + 1.f}, fs, c);
  }

  // Live signal colours on every symbol (only when the text matches the program).
  if (!err && prog)
  {
    for (const auto &tok : prog->tokens)
    {
      if (tok.line < first || tok.line >= last || tok.line >= static_cast<int>(lines.size()))
        continue;
      auto it = prog->symbolToSignal.find(tok.symbol);
      if (it == prog->symbolToSignal.end() || tok.symbol == gll::kNotConnected)
        continue;
      const std::string &s = lines[tok.line];
      if (tok.col1 > static_cast<int>(s.size()))
        continue;
      float px = tx + draw::textWidth(ui.font, s.substr(0, tok.col0), fs);
      float w = draw::textWidth(ui.font, s.substr(0, tok.col1), fs) - (px - tx);
      int sig = it->second;
      sf::Color col = ed.isAnalog(sig) ? Theme::Analog : ed.value(sig) ? Theme::TextGreen : Theme::TextRed;
      float ly = tok.line * lh;
      draw::roundRect(*ui.rt, {{px - 1.f, ly + 2.f}, {w + 2.f, lh - 3.f}}, 3.f, Theme::withAlpha(col, 45));
      draw::text(*ui.rt, ui.font, tok.symbol, {px, ly + 1.f}, fs, col);
    }
  }
  ui.rt->setView(old);

  // Click a line to select what is defined there.
  if (hoverLine >= 0 && hoverLine < static_cast<int>(lines.size()) && ui.takeClick(body))
  {
    std::string key;
    int g = ed.graph.nodeForLine(hoverLine);
    if (g >= 0)
      key = ed.graph.nodes[g].key;
    else if (prog && !err)
    {
      float mx = local.x - tx;
      for (const auto &tok : prog->tokens)
      {
        if (tok.line != hoverLine)
          continue;
        const std::string &s = lines[tok.line];
        float a = draw::textWidth(ui.font, s.substr(0, tok.col0), fs), b = draw::textWidth(ui.font, s.substr(0, tok.col1), fs);
        if (mx >= a - 2.f && mx <= b + 2.f)
        {
          auto it = prog->symbolToSignal.find(tok.symbol);
          if (it != prog->symbolToSignal.end())
            if (int t = ed.graph.nodeForSignal(it->second); t >= 0 && ed.graph.nodes[t].line == hoverLine)
              key = ed.graph.nodes[t].key;
        }
      }
    }
    ed.select(key);
    if (!key.empty())
    {
      lastSelection_ = key;  // already in view, don't jump
      canvas.reveal(key);
    }
  }
}

// ---- status bar --------------------------------------------------------------------------

void StatusBar::draw(Editor &ed, const RunState &run, sf::FloatRect r)
{
  Ui &ui = ed.ui;
  panelBg(ui, r);
  hline(ui, r.position.x, r.position.x + r.size.x, r.position.y);
  float y = r.position.y + 5.f, x = r.position.x + 14.f;

  if (const auto &err = ed.doc.error())
  {
    draw::circle(*ui.rt, {x + 3.f, y + 8.f}, 4.f, Theme::Error);
    std::string msg = err->msg.rfind("Line ", 0) == 0 || err->line < 0 ? err->msg
                                                                       : "Line " + std::to_string(err->line + 1) + ": " + err->msg;
    draw::text(*ui.rt, ui.font, draw::fit(ui.font, msg, 12, r.size.x * 0.5f), {x + 14.f, y}, 12, Theme::Error);
  }
  else
  {
    std::string hint = ed.doc.statusMessage();
    if (hint.empty())
      hint = "Drag from a port to connect · Right-click for options · Del deletes · Ctrl+Z undoes";
    draw::text(*ui.rt, ui.font, draw::fit(ui.font, hint, 12, r.size.x * 0.55f), {x, y}, 12, Theme::TextDim);
  }

  std::string right;
  if (ed.sim)
    right = "scan " + std::to_string(ed.sim->scanCount());
  right += run.running ? " · running" : " · paused";
  if (ed.sim && !ed.sim->isValidTopology())
    right += " · invalid circuit";
  draw::text(*ui.rt, ui.font, right, {r.position.x + r.size.x - 14.f, y}, 12, Theme::TextFaint, draw::Align::Right);
}

// ---- modbus dialog --------------------------------------------------------------------------

void ModbusDialog::open(ModbusManager &mb)
{
  fields_[0] = mb.getIp();
  fields_[1] = std::to_string(mb.getPort());
  fields_[2] = std::to_string(mb.getSlaveId());
  fields_[3] = std::to_string(mb.getNumInputs());
  fields_[4] = std::to_string(mb.getNumOutputs());
  fields_[5] = std::to_string(mb.getNumAnalogInputs());
  fields_[6] = std::to_string(mb.getNumAnalogOutputs());
  mode32_ = mb.getAnalogRegisterMode() == ModbusManager::AnalogRegisterMode::BITS_32;
}

void ModbusDialog::apply(ModbusManager &mb)
{
  mb.setIp(trim(fields_[0]));
  auto num = [](const std::string &s, int fallback)
  {
    try
    {
      return std::stoi(s);
    }
    catch (...)
    {
      return fallback;
    }
  };
  mb.setPort(num(fields_[1], mb.getPort()));
  mb.setSlaveId(num(fields_[2], mb.getSlaveId()));
  mb.setNumInputs(num(fields_[3], mb.getNumInputs()));
  mb.setNumOutputs(num(fields_[4], mb.getNumOutputs()));
  mb.setNumAnalogInputs(num(fields_[5], mb.getNumAnalogInputs()));
  mb.setNumAnalogOutputs(num(fields_[6], mb.getNumAnalogOutputs()));
  mb.setAnalogRegisterMode(mode32_ ? ModbusManager::AnalogRegisterMode::BITS_32 : ModbusManager::AnalogRegisterMode::BITS_16);
}

void ModbusDialog::draw(Editor &ed, ViewState &view, sf::Vector2f window)
{
  Ui &ui = ed.ui;
  ModbusManager &mb = ed.modbus;
  sf::RectangleShape shade(window);
  shade.setFillColor(sf::Color(0, 0, 0, 140));
  ui.rt->draw(shade);

  sf::Vector2f size(460.f, 520.f);
  sf::FloatRect card((window - size) / 2.f, size);
  draw::roundRect(*ui.rt, {card.position + sf::Vector2f(0, 6), size}, 12.f, sf::Color(0, 0, 0, 90));
  draw::roundRect(*ui.rt, card, 12.f, Theme::PanelRaised, Theme::Border, 1.f);

  Rows row{ui, card.position.x + 24.f, size.x - 48.f, card.position.y + 22.f};
  draw::text(*ui.rt, ui.bold, "Modbus TCP", {row.x, row.y}, 18, Theme::TextDefault);
  row.y += 28.f;
  row.note("Signals named INPUT_n / OUTPUT_n / AINPUT_n / AOUTPUT_n are synced.", Theme::TextDim);
  row.y += 4.f;

  static const char *labels[] = {"IP address", "Port", "Slave ID", "Digital in", "Digital out", "Analog in", "Analog out"};
  for (int i = 0; i < 7; ++i)
  {
    if (i == 3)
      row.section("SIGNAL COUNTS");
    std::string *target = &fields_[i];
    ui.textField("mb" + std::to_string(i), row.field(labels[i]), *target, [target](const std::string &v) { *target = v; });
  }
  sf::FloatRect modeRow = row.field("Registers");
  if (ui.chip({modeRow.position, {modeRow.size.x / 2.f - 3.f, modeRow.size.y}}, "16-bit  0–65535", !mode32_))
    mode32_ = false;
  if (ui.chip({{modeRow.position.x + modeRow.size.x / 2.f + 3.f, modeRow.position.y},
               {modeRow.size.x / 2.f - 3.f, modeRow.size.y}},
              "32-bit  2 regs", mode32_))
    mode32_ = true;

  if (!mb.getLastError().empty())
    row.note(mb.getLastError(), Theme::Error);
  else if (mb.isConnected())
    row.note("Connected", Theme::High);

  float by = card.position.y + size.y - 54.f;
  bool conn = mb.isConnected();
  if (ui.button({{row.x, by}, {120.f, 32.f}}, conn ? "Disconnect" : "Connect", conn ? Ui::Style::Danger : Ui::Style::Primary))
  {
    ui.clearFocus(true);
    if (conn)
      mb.disconnect();
    else
    {
      apply(mb);
      mb.connect();
    }
  }
  bool close = ui.button({{card.position.x + size.x - 24.f - 100.f, by}, {100.f, 32.f}}, "Close");
  if (!ui.hasFocus() && ui.in.key(K::Escape))
    close = true;
  if (close)
  {
    ui.clearFocus(true);
    if (!mb.isConnected())
      apply(mb);
    mb.saveConfig();
    view.modbus = false;
  }
}

// ---- help --------------------------------------------------------------------------------

void drawHelp(Ui &ui, ViewState &view, sf::Vector2f window, bool canClose)
{
  sf::RectangleShape shade(window);
  shade.setFillColor(sf::Color(0, 0, 0, 140));
  ui.rt->draw(shade);
  static const std::pair<const char *, const char *> keys[] = {
      {"Space", "Run / pause"},
      {". (period)", "Evaluate one node"},
      {"+  /  -", "Faster / slower"},
      {"Ctrl+Z  /  Ctrl+Y", "Undo / redo"},
      {"Del / Backspace", "Delete selection"},
      {"Ctrl+A", "Select all"},
      {"F", "Fit circuit in view"},
      {"L", "Auto layout"},
      {"Tab", "Show / hide the side panel"},
      {"P", "Show / hide the palette"},
      {"Drag empty canvas", "Pan (also right / middle drag)"},
      {"Shift+drag", "Box select"},
      {"Wheel", "Zoom"},
      {"Drag from a port", "Connect; drag a connected input away to unplug"},
      {"Right-click", "Add node / input modifiers / delete"},
      {"Click IN switch", "Toggle input"},
      {"Click BTN", "Momentary press, Ctrl+click latches"},
      {"Alt while dragging", "Move without grid snapping"},
  };
  sf::Vector2f size(520.f, 64.f + sizeof(keys) / sizeof(keys[0]) * 24.f + 20.f);
  sf::FloatRect card((window - size) / 2.f, size);
  draw::roundRect(*ui.rt, card, 12.f, Theme::PanelRaised, Theme::Border, 1.f);
  float x = card.position.x + 24.f, y = card.position.y + 20.f;
  draw::text(*ui.rt, ui.bold, "Shortcuts", {x, y}, 18, Theme::TextDefault);
  y += 40.f;
  for (const auto &[k, what] : keys)
  {
    draw::text(*ui.rt, ui.bold, k, {x, y}, 12, Theme::TextDefault);
    draw::text(*ui.rt, ui.font, what, {x + 190.f, y}, 12, Theme::TextDim);
    y += 24.f;
  }
  if (canClose && (ui.in.pressed[0] || ui.in.key(K::Escape) || ui.in.key(K::F1)))
  {
    view.help = false;
    ui.mouseTaken = true;
  }
}
