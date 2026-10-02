#include "Canvas.hpp"
#include "TimeUtils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

using graph::Node;
using graph::NodeKind;
using graph::Port;
namespace M = graph::metrics;

// ---- node specs ------------------------------------------------------------------

std::string NodeSpec::keyword() const
{
  return terminal ? gll::declKeyword(decl) : gll::typeInfo(type).keyword;
}

std::string NodeSpec::summary() const
{
  if (terminal)
  {
    switch (decl)
    {
    case gll::DeclKind::In: return "IN — digital input, click to toggle";
    case gll::DeclKind::Out: return "OUT — digital output";
    case gll::DeclKind::AIn: return "AIN — analog (integer) input";
    case gll::DeclKind::AOut: return "AOUT — analog (integer) output";
    }
  }
  return gll::typeInfo(type).summary;
}

sf::Color NodeSpec::color() const
{
  if (terminal)
    return (decl == gll::DeclKind::AIn || decl == gll::DeclKind::AOut) ? Theme::AnalogTerminal : Theme::Terminal;
  return Theme::category(gll::typeInfo(type).category);
}

const std::vector<NodeSpec> &paletteSpecs()
{
  static const std::vector<NodeSpec> specs = []
  {
    std::vector<NodeSpec> v;
    for (auto d : {gll::DeclKind::In, gll::DeclKind::Out, gll::DeclKind::AIn, gll::DeclKind::AOut})
      v.push_back(NodeSpec{true, gll::NodeType::AND_, d});
    for (auto t : gll::allTypes())
      v.push_back(NodeSpec{false, t, gll::DeclKind::In});
    return v;
  }();
  return specs;
}

static std::string shortSummary(const std::string &s)
{
  auto p = s.find("— ");
  return p == std::string::npos ? s : s.substr(p + std::string("— ").size());
}

static std::string formatValue(uint64_t v)
{
  char buf[48];
  snprintf(buf, sizeof(buf), "%llu (0x%llX)", static_cast<unsigned long long>(v), static_cast<unsigned long long>(v));
  return buf;
}

// ---- geometry ----------------------------------------------------------------------

void Canvas::setViewport(sf::FloatRect screen, sf::Vector2u windowSize)
{
  screen_ = screen;
  window_ = windowSize;
  if (!fitted_)
  {
    if (!ed_.graph.nodes.empty())
      fitView();
    fitted_ = true;
  }
}

sf::Vector2f Canvas::toWorld(sf::Vector2f s) const
{
  sf::Vector2f c = screen_.position + screen_.size / 2.f;
  return center_ + (s - c) / zoom_;
}

sf::Vector2f Canvas::toScreen(sf::Vector2f w) const
{
  sf::Vector2f c = screen_.position + screen_.size / 2.f;
  return c + (w - center_) * zoom_;
}

void Canvas::fitView()
{
  if (ed_.graph.nodes.empty())
  {
    center_ = {300.f, 0.f};
    zoom_ = 1.f;
    return;
  }
  sf::FloatRect box = ed_.graph.nodes[0].rect;
  for (const auto &n : ed_.graph.nodes)
  {
    float l = std::min(box.position.x, n.rect.position.x), t = std::min(box.position.y, n.rect.position.y);
    float r = std::max(box.position.x + box.size.x, n.rect.position.x + n.rect.size.x);
    float b = std::max(box.position.y + box.size.y, n.rect.position.y + n.rect.size.y);
    box = {{l, t}, {r - l, b - t}};
  }
  center_ = box.position + box.size / 2.f;
  float zx = screen_.size.x / (box.size.x + 120.f), zy = screen_.size.y / (box.size.y + 120.f);
  zoom_ = std::clamp(std::min(zx, zy), 0.25f, 1.25f);
}

void Canvas::zoomBy(float factor)
{
  zoom_ = std::clamp(zoom_ * factor, 0.2f, 2.5f);
}

void Canvas::relayout()
{
  ed_.doc.beginLayoutChange("Auto layout");
  ed_.graph.autoLayout(ed_.doc.positions(), false);
  ed_.doc.saveLayout();
  ed_.doc.setStatus("Auto layout");
  fitView();
}

void Canvas::reveal(const std::string &key)
{
  int i = ed_.graph.find(key);
  if (i < 0)
    return;
  sf::FloatRect r = ed_.graph.nodes[i].rect;
  sf::FloatRect visible(toWorld(screen_.position), screen_.size / zoom_);
  if (!visible.contains(r.position) || !visible.contains(r.position + r.size))
    center_ = r.position + r.size / 2.f;
}

void Canvas::wireCurve(sf::Vector2f a, sf::Vector2f b, sf::Vector2f out[4]) const
{
  float dx = std::abs(b.x - a.x) * 0.5f;
  if (b.x < a.x + 20.f)
    dx = 60.f + std::abs(a.x - b.x) * 0.25f + std::abs(a.y - b.y) * 0.15f;  // backwards wire: loop around
  dx = std::max(dx, 40.f);
  out[0] = a;
  out[1] = {a.x + dx, a.y};
  out[2] = {b.x - dx, b.y};
  out[3] = b;
}

// ---- hit testing ---------------------------------------------------------------------

Canvas::PortRef Canvas::portAt(sf::Vector2f w) const
{
  const float r = std::max(9.f, 9.f / zoom_);
  float best = r * r;
  PortRef hit;
  const auto &nodes = ed_.graph.nodes;
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
  {
    auto test = [&](const std::vector<Port> &ports, bool input)
    {
      for (int p = 0; p < static_cast<int>(ports.size()); ++p)
      {
        sf::Vector2f d = ports[p].pos - w;
        float dd = d.x * d.x + d.y * d.y;
        if (dd < best)
        {
          best = dd;
          hit = PortRef{i, p, input};
        }
      }
    };
    test(nodes[i].ins, true);
    test(nodes[i].outs, false);
  }
  return hit;
}

int Canvas::nodeAt(sf::Vector2f w) const
{
  const auto &nodes = ed_.graph.nodes;
  for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i)
    if (nodes[i].rect.contains(w))
      return i;
  return -1;
}

int Canvas::edgeAt(sf::Vector2f w) const
{
  const float tol = std::max(5.f, 6.f / zoom_);
  const auto &g = ed_.graph;
  for (int e = 0; e < static_cast<int>(g.edges.size()); ++e)
  {
    const auto &edge = g.edges[e];
    sf::Vector2f c[4];
    wireCurve(g.nodes[edge.from].outs[edge.fromPort].pos, g.nodes[edge.to].ins[edge.toPort].pos, c);
    for (int i = 0; i <= 24; ++i)
    {
      float t = i / 24.f, u = 1.f - t;
      sf::Vector2f p = c[0] * (u * u * u) + c[1] * (3 * u * u * t) + c[2] * (3 * u * t * t) + c[3] * (t * t * t);
      sf::Vector2f d = p - w;
      if (d.x * d.x + d.y * d.y < tol * tol)
        return e;
    }
  }
  return -1;
}

sf::FloatRect Canvas::switchRect(const Node &n) const
{
  return {{n.rect.position.x + 12.f, n.rect.position.y + n.rect.size.y / 2.f - 9.f}, {34.f, 18.f}};
}

sf::FloatRect Canvas::buttonRect(const Node &n) const
{
  return {{n.rect.position.x + 12.f, n.rect.position.y + n.rect.size.y - M::Extra - 2.f},
          {n.rect.size.x - 24.f, M::Extra - 6.f}};
}

sf::FloatRect Canvas::valueRect(const Node &n) const
{
  return {{n.rect.position.x + n.rect.size.x - 84.f, n.rect.position.y + 9.f}, {70.f, n.rect.size.y - 18.f}};
}

// ---- editing helpers --------------------------------------------------------------------

void Canvas::addNode(const NodeSpec &spec, std::optional<sf::Vector2f> screenPos)
{
  if (!ed_.doc.editable())
  {
    ed_.doc.setStatus("Fix the error in the file first — the graph is read-only meanwhile");
    return;
  }
  sf::Vector2f world = screenPos ? toWorld(*screenPos) : center_;
  const gll::Script &script = ed_.doc.script();
  std::string name = gll::edits::uniqueName(script, Editor::baseName(spec.keyword()));
  std::string key;
  sf::Vector2f size;
  if (spec.terminal)
  {
    static const char *prefix[] = {"i:", "o:", "ai:", "ao:"};
    key = prefix[static_cast<int>(spec.decl)] + name;
    size = {M::TerminalWidth, M::TerminalHeight};
  }
  else
  {
    key = "g:" + name;
    size = {M::GateWidth, 90.f};
  }
  sf::Vector2f pos = world - size / 2.f;
  ed_.doc.positions()[key] = {std::round(pos.x / 8.f) * 8.f, std::round(pos.y / 8.f) * 8.f};

  bool ok = ed_.apply("Add " + spec.keyword(), [&](gll::Script &s)
  {
    if (spec.terminal)
      gll::edits::addDecl(s, spec.decl, name);
    else
      gll::edits::addGate(s, spec.type, name);
    return true;
  });
  if (ok)
    ed_.select(key);
}

void Canvas::deleteSelection()
{
  auto &g = ed_.graph;
  if (ed_.selectedEdge >= 0 && ed_.selectedEdge < static_cast<int>(g.edges.size()))
  {
    const auto e = g.edges[ed_.selectedEdge];
    const Node &to = g.nodes[e.to];
    int toLine = to.line, arg = to.ins[e.toPort].index, fromLine = g.nodes[e.from].line;
    bool gate = to.isGate();
    std::string term = to.name;
    ed_.selectedEdge = -1;
    ed_.apply("Delete connection", [&](gll::Script &s)
    {
      if (gate)
        gll::edits::disconnectInput(s, toLine, arg);
      else
        gll::edits::undrive(s, fromLine, term);
      return true;
    });
    return;
  }
  if (ed_.selection.empty())
    return;

  std::vector<std::string> gates, decls;
  for (const auto &key : ed_.selection)
  {
    int i = g.find(key);
    if (i < 0)
      continue;
    const Node &n = g.nodes[i];
    if (n.isGate())
      gates.push_back(n.name);
    else
      decls.push_back(ed_.doc.script().lines[n.line].items[n.item].name);
  }
  size_t count = gates.size() + decls.size();
  ed_.apply(count == 1 ? "Delete node" : "Delete " + std::to_string(count) + " nodes", [&](gll::Script &s)
  {
    for (const auto &name : gates)
      gll::edits::deleteGate(s, gll::edits::findGate(s, name));
    for (const auto &name : decls)
      if (auto ref = gll::edits::findDecl(s, name))
        gll::edits::removeDecl(s, *ref);
    return true;
  });
  ed_.selection.clear();
}

bool Canvas::connect(PortRef out, PortRef in, std::optional<PortRef> detach)
{
  auto &g = ed_.graph;
  const Node &from = g.nodes[out.node];
  const Node &to = g.nodes[in.node];

  if (!to.isGate() && !from.isGate())
  {
    ed_.doc.setStatus("An output needs a node driving it — put e.g. an OR between input and output");
    return false;
  }

  // Capture everything by value: indices are invalid once the edit runs.
  const bool fromGate = from.isGate(), toGate = to.isGate();
  const int fromLine = from.line, fromPort = from.outs[out.port].index;
  const std::string fromName = from.name;
  const int toLine = to.line, toArg = to.ins[in.port].index;
  const std::string toName = to.name;
  struct Detach
  {
    bool gate;
    int line, arg;
    std::string term;
  };
  std::optional<Detach> det;
  if (detach && !(detach->node == in.node && detach->port == in.port))
  {
    const Node &d = g.nodes[detach->node];
    det = Detach{d.isGate(), d.line, d.ins[detach->port].index, d.name};
  }

  return ed_.apply("Connect", [&](gll::Script &s)
  {
    if (toGate)
    {
      std::string sym = fromGate ? gll::edits::ensureOutput(s, fromLine, fromPort) : fromName;
      gll::edits::connectInput(s, toLine, toArg, sym);
    }
    else
    {
      gll::edits::driveTerminal(s, fromLine, fromPort, toName);
    }
    if (det)
    {
      if (det->gate)
        gll::edits::disconnectInput(s, det->line, det->arg);
      else if (det->term != toName)
        gll::edits::undrive(s, fromLine, det->term);
    }
    return true;
  });
}

void Canvas::detachOnly(PortRef in)
{
  auto &g = ed_.graph;
  const Node &to = g.nodes[in.node];
  const bool gate = to.isGate();
  const int line = to.line, arg = to.ins[in.port].index;
  const std::string term = to.name;
  const int fromLine = wireFrom_ ? g.nodes[wireFrom_.node].line : -1;
  ed_.apply("Disconnect", [&](gll::Script &s)
  {
    if (gate)
      gll::edits::disconnectInput(s, line, arg);
    else if (fromLine >= 0)
      gll::edits::undrive(s, fromLine, term);
    return true;
  });
}

void Canvas::beginWire(PortRef port)
{
  detach_.reset();
  wireFrom_ = port;
  if (!port.input)
    return;
  // Grabbing a connected input picks the wire up from its producer.
  for (const auto &e : ed_.graph.edges)
  {
    if (e.to == port.node && e.toPort == port.port)
    {
      wireFrom_ = PortRef{e.from, e.fromPort, false};
      detach_ = port;
      return;
    }
  }
}

void Canvas::finishWire(sf::Vector2f world)
{
  auto &g = ed_.graph;
  PortRef target = portAt(world);
  if (target && target.input == wireFrom_.input)
    target = PortRef{};

  if (!target)
  {
    // Dropped on a node body: use its first free matching port.
    int n = nodeAt(world);
    if (n >= 0)
    {
      const Node &node = g.nodes[n];
      if (!wireFrom_.input)
      {
        for (int p = 0; p < static_cast<int>(node.ins.size()) && !target; ++p)
          if (node.ins[p].symbol.empty() || node.ins[p].spare || !node.isGate())
            target = PortRef{n, p, true};
      }
      else if (!node.outs.empty())
        target = PortRef{n, 0, false};
    }
  }

  if (target)
  {
    PortRef out = wireFrom_.input ? target : wireFrom_;
    PortRef in = wireFrom_.input ? wireFrom_ : target;
    if (detach_ && detach_->node == in.node && detach_->port == in.port)
      return;  // dropped back where it came from
    connect(out, in, detach_);
    return;
  }

  if (detach_)
    detachOnly(*detach_);
  else if (!wireFrom_.input)
    openAddMenu(toScreen(world), wireFrom_);
}

// ---- menus --------------------------------------------------------------------------------

void Canvas::openAddMenu(sf::Vector2f screen, std::optional<PortRef> autoConnect)
{
  std::vector<MenuItem> items;
  std::optional<std::pair<std::string, int>> from;
  if (autoConnect)
    from = std::make_pair(ed_.graph.nodes[autoConnect->node].key, autoConnect->port);

  std::string lastGroup;
  for (const auto &spec : paletteSpecs())
  {
    if (from && !spec.terminal && spec.type == gll::NodeType::BTN)
      continue;  // a button has no inputs to connect to
    if (from && spec.terminal && spec.decl != gll::DeclKind::Out && spec.decl != gll::DeclKind::AOut)
      continue;  // wiring an output into an input terminal makes no sense
    std::string group = spec.terminal ? "Signals" : gll::categoryName(gll::typeInfo(spec.type).category);
    if (group != lastGroup)
    {
      items.push_back({group});
      lastGroup = group;
    }
    items.push_back({spec.keyword() + "\t" + shortSummary(spec.summary()), [this, spec, screen, from]
    {
      addNode(spec, screen);
      if (!from || ed_.selection.size() != 1)
        return;
      int src = ed_.graph.find(from->first), dst = ed_.graph.find(*ed_.selection.begin());
      if (src < 0 || dst < 0 || ed_.graph.nodes[dst].ins.empty())
        return;
      connect(PortRef{src, from->second, false}, PortRef{dst, 0, true}, std::nullopt);
    }});
  }
  ed_.ui.showMenu(screen, std::move(items));
}

void Canvas::openContextMenu(sf::Vector2f screen)
{
  auto &g = ed_.graph;
  sf::Vector2f w = toWorld(screen);
  PortRef port = portAt(w);

  if (port && port.input && g.nodes[port.node].isGate())
  {
    const Node &n = g.nodes[port.node];
    const Port &p = n.ins[port.port];
    const int line = n.line, arg = p.index;
    std::vector<MenuItem> items;
    items.push_back({"Input " + (p.label.empty() ? std::to_string(arg + 1) : p.label)});
    auto modItem = [&](const std::string &label, gll::Mod m)
    {
      MenuItem it{label, [this, line, arg, m]
      {
        ed_.apply("Change input modifier", [&](gll::Script &s)
        {
          gll::edits::setInputMod(s, line, arg, m);
          return true;
        });
      }};
      it.checked = p.mod == m;
      it.enabled = !p.spare && !p.literal;
      items.push_back(it);
    };
    modItem("Normal", gll::Mod::None);
    modItem("Inverted  NOT()", gll::Mod::Not);
    modItem("Rising edge  PS()", gll::Mod::Ps);
    modItem("Falling edge  NS()", gll::Mod::Ns);
    MenuItem disc{"Disconnect", [this, port] { detachOnly(port); }};
    disc.enabled = !p.symbol.empty() && !p.spare;
    items.push_back(disc);
    ed_.ui.showMenu(screen, std::move(items));
    return;
  }

  int n = nodeAt(w);
  if (n >= 0)
  {
    const Node &node = g.nodes[n];
    if (!ed_.selection.count(node.key))
      ed_.select(node.key);
    std::vector<MenuItem> items;
    items.push_back({node.isGate() ? std::string(gll::typeInfo(node.type).keyword) + " " + node.name : node.name});
    items.push_back({"Rename", [this] { ed_.focusField = "name"; }});
    items.push_back({ed_.selection.size() > 1 ? "Delete selected" : "Delete", [this] { deleteSelection(); }});
    ed_.ui.showMenu(screen, std::move(items));
    return;
  }

  int e = edgeAt(w);
  if (e >= 0)
  {
    ed_.select("");
    ed_.selectedEdge = e;
    ed_.ui.showMenu(screen, {{"Connection " + g.edges[e].symbol}, {"Delete connection", [this] { deleteSelection(); }}});
    return;
  }

  openAddMenu(screen, std::nullopt);
}

// ---- input --------------------------------------------------------------------------------

void Canvas::handleKeys(const FrameInput &in)
{
  using K = sf::Keyboard::Key;
  if (ed_.ui.hasFocus() || ed_.ui.menuOpen())
    return;
  for (auto k : in.keys)
  {
    if (k == K::Delete || k == K::Backspace)
      deleteSelection();
    else if (k == K::Escape)
    {
      if (mode_ == Mode::Wire)
        mode_ = Mode::Idle;
      else
        ed_.select("");
    }
    else if (k == K::F && !in.ctrl)
      fitView();
    else if (k == K::L && !in.ctrl)
      relayout();
    else if (k == K::A && in.ctrl)
    {
      ed_.select("");
      for (const auto &n : ed_.graph.nodes)
        ed_.selection.insert(n.key);
    }
  }
}

void Canvas::handle(const FrameInput &in, bool mouseFree)
{
  auto &g = ed_.graph;
  auto &ui = ed_.ui;
  mouseWorld_ = toWorld(in.mouse);
  handleKeys(in);

  const bool canPress = mouseFree && !ui.mouseTaken && ui.interactive;
  hoverPort_ = canPress ? portAt(mouseWorld_) : PortRef{};
  hoverNode_ = canPress ? nodeAt(mouseWorld_) : -1;

  // Zoom around the cursor.
  if (mouseFree && in.wheel != 0.f && !ui.menuOpen())
  {
    sf::Vector2f before = toWorld(in.mouse);
    zoomBy(std::pow(1.15f, in.wheel));
    center_ += before - toWorld(in.mouse);
  }

  // Tooltips for ports and wires.
  if (canPress && mode_ == Mode::Idle)
  {
    sf::FloatRect around(in.mouse - sf::Vector2f(6.f, 6.f), {12.f, 12.f});
    if (hoverPort_)
    {
      const Port &p = hoverPort_.input ? g.nodes[hoverPort_.node].ins[hoverPort_.port]
                                       : g.nodes[hoverPort_.node].outs[hoverPort_.port];
      std::string tip;
      if (p.spare)
        tip = "Drop a wire here to add an input";
      else if (p.symbol.empty())
        tip = hoverPort_.input ? "Not connected — drag to an output" : "Drag to an input to connect";
      else if (p.literal)
        tip = "Constant " + p.symbol;
      else
      {
        uint64_t v = (g.nodes[hoverPort_.node].kind == NodeKind::Input && ed_.sim)
                         ? ed_.sim->getSignalValue(p.symbol)
                         : ed_.value(p.signal);
        tip = p.symbol + " = " + (ed_.isAnalog(p.signal) ? formatValue(v) : std::to_string(v != 0));
        if (p.dangling)
          tip += "  (nothing drives this signal)";
      }
      ui.tooltip(around, tip);
    }
    else if (hoverNode_ < 0)
    {
      int e = edgeAt(mouseWorld_);
      if (e >= 0)
        ui.tooltip(around, g.edges[e].symbol + (g.edges[e].feedback ? "  — read before it is written: 1 scan delay" : ""));
    }
  }

  // ---- press ----
  if (canPress && in.pressed[0])
  {
    ui.mouseTaken = true;
    pressScreen_ = lastScreen_ = in.mouse;
    if (hoverPort_)
    {
      beginWire(hoverPort_);
      mode_ = Mode::Wire;
    }
    else if (hoverNode_ >= 0)
    {
      const Node &n = g.nodes[hoverNode_];
      if (n.kind == NodeKind::Input && switchRect(n).contains(mouseWorld_) && ed_.sim)
      {
        ed_.sim->toggleSignal(n.name);
      }
      else if (n.isGate() && n.type == gll::NodeType::BTN && buttonRect(n).contains(mouseWorld_) && ed_.sim)
      {
        if (in.ctrl)
          ed_.sim->toggleLatch(n.name);
        else
        {
          ed_.sim->setMomentary(n.name, true);
          pressedButton_ = n.name;
          mode_ = Mode::Button;
        }
      }
      else
      {
        bool additive = in.ctrl || in.shift;
        if (!ed_.selection.count(n.key) || additive)
          ed_.select(n.key, additive);
        ed_.selectedEdge = -1;
        if (n.kind == NodeKind::AInput && valueRect(n).contains(mouseWorld_))
          ed_.focusField = "value";
        else if (in.doubleClick)
          ed_.focusField = "name";
        mode_ = Mode::MovePending;
        moveOrigin_ = mouseWorld_;
        moveStart_.clear();
        for (const auto &key : ed_.selection)
          if (auto it = ed_.doc.positions().find(key); it != ed_.doc.positions().end())
            moveStart_.push_back({key, it->second});
      }
    }
    else
    {
      int e = edgeAt(mouseWorld_);
      if (e >= 0)
      {
        ed_.select("");
        ed_.selectedEdge = e;
      }
      else
        mode_ = in.shift ? Mode::Box : Mode::Pan;
    }
  }

  // Right / middle button: drag pans, a right click opens the context menu.
  if (canPress && (in.pressed[1] || in.pressed[2]))
  {
    ui.mouseTaken = true;
    rightDrag_ = true;
    rightPress_ = lastScreen_ = in.mouse;
  }
  if (rightDrag_)
  {
    if (in.down[1] || in.down[2])
    {
      center_ -= (in.mouse - lastScreen_) / zoom_;
      lastScreen_ = in.mouse;
    }
    if (in.released[1] || in.released[2])
    {
      rightDrag_ = false;
      sf::Vector2f d = in.mouse - rightPress_;
      if (in.released[1] && d.x * d.x + d.y * d.y < 16.f && screen_.contains(in.mouse))
        openContextMenu(in.mouse);
    }
  }

  // ---- drag ----
  sf::Vector2f delta = in.mouse - pressScreen_;
  bool moved = delta.x * delta.x + delta.y * delta.y > 16.f;
  switch (mode_)
  {
  case Mode::Pan:
    center_ -= (in.mouse - lastScreen_) / zoom_;
    break;
  case Mode::MovePending:
    if (moved)
    {
      ed_.doc.beginLayoutChange("Move");
      mode_ = Mode::Move;
    }
    break;
  case Mode::Move:
  {
    sf::Vector2f d = mouseWorld_ - moveOrigin_;
    for (const auto &[key, start] : moveStart_)
    {
      sf::Vector2f p = start + d;
      if (!in.alt)
        p = {std::round(p.x / 8.f) * 8.f, std::round(p.y / 8.f) * 8.f};
      ed_.doc.positions()[key] = p;
    }
    g.place(ed_.doc.positions());
    break;
  }
  default:
    break;
  }
  lastScreen_ = in.mouse;

  // ---- release ----
  if (in.released[0])
  {
    switch (mode_)
    {
    case Mode::Pan:
      if (!moved)
        ed_.select("");
      break;
    case Mode::Box:
    {
      sf::Vector2f a = toWorld(pressScreen_), b = mouseWorld_;
      sf::FloatRect box({std::min(a.x, b.x), std::min(a.y, b.y)}, {std::abs(a.x - b.x), std::abs(a.y - b.y)});
      for (const auto &n : g.nodes)
        if (box.findIntersection(n.rect))
          ed_.selection.insert(n.key);
      break;
    }
    case Mode::Move:
      ed_.doc.saveLayout();
      break;
    case Mode::Wire:
      if (moved)
        finishWire(mouseWorld_);
      else if (wireFrom_ && !ed_.selection.count(g.nodes[wireFrom_.node].key))
        ed_.select(g.nodes[wireFrom_.node].key);
      break;
    case Mode::Button:
      if (ed_.sim)
        ed_.sim->setMomentary(pressedButton_, false);
      pressedButton_.clear();
      break;
    default:
      break;
    }
    mode_ = Mode::Idle;
  }
}

// ---- drawing --------------------------------------------------------------------------------

void Canvas::text(sf::RenderTarget &rt, const std::string &s, sf::Vector2f pos, unsigned size, sf::Color c,
                  draw::Align a, bool bold)
{
  draw::text(rt, bold ? ed_.ui.bold : ed_.ui.font, s, pos, size, c, a, zoom_);
}

sf::Color Canvas::signalColor(int signal, bool wire) const
{
  if (signal < 0)
    return wire ? Theme::LowWire : Theme::Border;
  if (ed_.isAnalog(signal))
    return Theme::Analog;
  return ed_.value(signal) ? Theme::High : (wire ? Theme::LowWire : Theme::LowPort);
}

bool Canvas::nodeHigh(const Node &n) const
{
  if (!ed_.sim)
    return false;
  switch (n.kind)
  {
  case NodeKind::Input:
    return ed_.sim->getSignalValue(n.name);
  case NodeKind::Output:
    return ed_.value(n.signal) != 0;
  case NodeKind::Gate:
    return !n.outs.empty() && n.outs[0].signal >= 0 && !ed_.isAnalog(n.outs[0].signal) && ed_.value(n.outs[0].signal);
  default:
    return false;
  }
}

void Canvas::drawGrid(sf::RenderTarget &rt)
{
  float step = 24.f;
  while (step * zoom_ < 14.f)
    step *= 2.f;
  sf::Vector2f tl = toWorld(screen_.position), br = toWorld(screen_.position + screen_.size);
  sf::VertexArray dots(sf::PrimitiveType::Triangles);
  float s = 1.1f / zoom_;
  for (float x = std::floor(tl.x / step) * step; x <= br.x; x += step)
    for (float y = std::floor(tl.y / step) * step; y <= br.y; y += step)
    {
      sf::Vector2f p(x, y);
      dots.append({p + sf::Vector2f(-s, -s), Theme::GridDot});
      dots.append({p + sf::Vector2f(s, -s), Theme::GridDot});
      dots.append({p + sf::Vector2f(s, s), Theme::GridDot});
      dots.append({p + sf::Vector2f(-s, -s), Theme::GridDot});
      dots.append({p + sf::Vector2f(s, s), Theme::GridDot});
      dots.append({p + sf::Vector2f(-s, s), Theme::GridDot});
    }
  rt.draw(dots);
}

void Canvas::drawEdges(sf::RenderTarget &rt)
{
  const auto &g = ed_.graph;
  std::set<int> touching;
  for (const auto &key : ed_.selection)
    touching.insert(g.find(key));

  for (int i = 0; i < static_cast<int>(g.edges.size()); ++i)
  {
    const auto &e = g.edges[i];
    // A wire being picked up is drawn as the drag ghost instead.
    if (mode_ == Mode::Wire && detach_ && detach_->node == e.to && detach_->port == e.toPort)
      continue;
    sf::Vector2f c[4];
    wireCurve(g.nodes[e.from].outs[e.fromPort].pos, g.nodes[e.to].ins[e.toPort].pos, c);
    sf::Color col = signalColor(e.signal, true);
    float width = 2.2f;
    bool selected = ed_.selectedEdge == i;
    bool related = touching.count(e.from) || touching.count(e.to);
    if (selected)
    {
      draw::bezier(rt, c[0], c[1], c[2], c[3], 7.f, Theme::withAlpha(Theme::Accent, 110));
    }
    else if (related)
      width = 3.f;
    if (!related && !selected && !ed_.selection.empty())
      col = Theme::withAlpha(col, 150);
    if (e.feedback && !related && !selected)
      col = Theme::withAlpha(col, col.a * 2 / 3);
    draw::bezier(rt, c[0], c[1], c[2], c[3], width, col, e.feedback ? 7.f : 0.f);
  }
}

void Canvas::drawPort(sf::RenderTarget &rt, const Port &p, bool input, bool gate)
{
  bool connected = !p.symbol.empty() || (!gate && !input) || (!gate && input);
  float r = 5.f;
  if (p.spare)
  {
    draw::circle(rt, p.pos, r - 0.5f, Theme::NodeBody, Theme::withAlpha(Theme::TextFaint, 160), 1.2f);
    draw::line(rt, p.pos - sf::Vector2f(2.5f, 0), p.pos + sf::Vector2f(2.5f, 0), 1.2f, Theme::TextFaint);
    draw::line(rt, p.pos - sf::Vector2f(0, 2.5f), p.pos + sf::Vector2f(0, 2.5f), 1.2f, Theme::TextFaint);
    return;
  }
  if (!connected || p.literal)
  {
    draw::circle(rt, p.pos, r - 0.5f, Theme::NodeBody, p.literal ? Theme::Analog : Theme::TextFaint, 1.5f);
    return;
  }
  sf::Color c = p.dangling ? Theme::Warning : signalColor(p.signal, false);
  draw::circle(rt, p.pos, r, c, Theme::NodeBody, 1.5f);
}

void Canvas::drawGate(sf::RenderTarget &rt, const Node &n, bool selected)
{
  const auto &info = gll::typeInfo(n.type);
  const sf::Color cat = Theme::category(info.category);
  const sf::FloatRect r = n.rect;
  const float x = r.position.x, y = r.position.y, w = r.size.x;
  const bool high = nodeHigh(n);
  const bool scanning = ed_.showScan && ed_.sim && n.progNode >= 0 && ed_.sim->currentEvaluatingNode() == n.progNode;

  draw::roundRect(rt, {r.position + sf::Vector2f(0, 4), r.size}, 9.f, sf::Color(0, 0, 0, 70));
  sf::Color border = selected ? Theme::Accent : scanning ? Theme::Scan : high ? Theme::withAlpha(Theme::High, 170)
                                                                              : Theme::NodeBorder;
  draw::roundRect(rt, r, 9.f, Theme::NodeBody, border, selected || scanning ? 2.f : 1.2f);

  // Header
  draw::roundRect(rt, {{x + 1.5f, y + 1.5f}, {w - 3.f, M::Header - 1.5f}}, 8.f, Theme::NodeHeader);
  draw::roundRect(rt, {{x + 1.5f, y + M::Header - 8.f}, {w - 3.f, 8.f}}, 0.f, Theme::NodeHeader);
  float kw = draw::textWidth(ed_.ui.bold, info.keyword, 11) + 12.f;
  draw::roundRect(rt, {{x + 9.f, y + 7.f}, {kw, 17.f}}, 4.f, Theme::withAlpha(cat, high ? 90 : 45));
  text(rt, info.keyword, {x + 9.f + kw / 2.f, y + 8.f}, 11, high ? sf::Color::White : cat, draw::Align::Center, true);
  const bool detail = zoom_ > 0.45f;
  if (detail)
  {
    float nameX = x + 9.f + kw + 7.f;
    text(rt, draw::fit(ed_.ui.bold, n.name, 13, x + w - 30.f - nameX), {nameX, y + 6.f}, 13, Theme::TextDefault,
         draw::Align::Left, true);
    text(rt, "#" + std::to_string(n.order), {x + w - 9.f, y + 8.f}, 10, Theme::TextFaint, draw::Align::Right);
  }
  else
  {
    // Zoomed out: drop the port details and show the name big enough to read.
    unsigned size = static_cast<unsigned>(std::min(12.f / zoom_, 44.f));
    std::string fitted = draw::fit(ed_.ui.bold, n.name, size, w - 16.f);
    text(rt, fitted, {x + w / 2.f, y + M::Header + (r.size.y - M::Header) / 2.f - size * 0.7f}, size,
         Theme::TextDefault, draw::Align::Center, true);
  }

  // Inputs
  for (const auto &p : n.ins)
  {
    float py = p.pos.y;
    drawPort(rt, p, true, true);
    float lx = x + 12.f;
    if (p.mod != gll::Mod::None)
    {
      sf::Color mc = Theme::category(gll::Category::Edge);
      if (p.mod == gll::Mod::Not)
        draw::circle(rt, {lx + 3.f, py}, 3.5f, Theme::NodeBody, mc, 1.5f);
      else
      {
        float d = p.mod == gll::Mod::Ps ? -1.f : 1.f;
        sf::ConvexShape tri(3);
        tri.setPoint(0, {lx + 3.f, py + 4.f * d});
        tri.setPoint(1, {lx - 1.f, py - 3.f * d});
        tri.setPoint(2, {lx + 7.f, py - 3.f * d});
        tri.setFillColor(mc);
        rt.draw(tri);
      }
      lx += 12.f;
    }
    if (!detail)
      continue;
    std::string label = p.label;
    sf::Color lc = Theme::TextDim;
    std::string sym;
    if (p.literal)
      sym = p.symbol;
    else if (!p.symbol.empty())
      sym = p.symbol;
    if (!label.empty())
    {
      text(rt, label, {lx, py - 9.f}, 11, lc, draw::Align::Left, true);
      lx += draw::textWidth(ed_.ui.bold, label, 11) + 6.f;
    }
    if (!sym.empty())
    {
      sf::Color sc = p.literal ? Theme::Analog : p.dangling ? Theme::Warning : Theme::TextFaint;
      text(rt, draw::fit(ed_.ui.font, sym, 11, x + w / 2.f + 18.f - lx), {lx, py - 9.f}, 11, sc);
    }
  }

  // Outputs
  for (const auto &p : n.outs)
  {
    drawPort(rt, p, false, true);
    if (!detail)
      continue;
    float rx = x + w - 12.f;
    text(rt, p.label, {rx, p.pos.y - 9.f}, 11, Theme::TextDim, draw::Align::Right, true);
    rx -= draw::textWidth(ed_.ui.bold, p.label, 11) + 6.f;
    if (p.signal >= 0 && ed_.isAnalog(p.signal))
      text(rt, std::to_string(ed_.value(p.signal)), {rx, p.pos.y - 9.f}, 11, Theme::Analog, draw::Align::Right);
    else if (!p.symbol.empty())
      text(rt, draw::fit(ed_.ui.font, p.symbol, 11, rx - (x + w / 2.f - 10.f)), {rx, p.pos.y - 9.f}, 11,
           Theme::TextFaint, draw::Align::Right);
  }

  if (!ed_.sim)
    return;

  // Extra row: timer progress, counter state, push button
  sf::FloatRect extra({x + 12.f, y + r.size.y - M::Extra - 2.f}, {w - 24.f, M::Extra - 6.f});
  if (info.presetTime)
  {
    float pt = ed_.sim->getPresetTime(n.name);
    float el = ed_.sim->getTimerElapsed(n.name);
    // A finished TON keeps its internal clock running; show it as full.
    bool tonDone = n.type == gll::NodeType::TON_ && high;
    float frac = tonDone ? 0.f : pt > 0.f ? std::clamp(el / pt, 0.f, 1.f) : 0.f;
    bool done = tonDone;
    sf::FloatRect bar({extra.position.x, extra.position.y + extra.size.y - 5.f}, {extra.size.x, 4.f});
    draw::roundRect(rt, bar, 2.f, Theme::Field);
    if (frac > 0.f)
      draw::roundRect(rt, {bar.position, {bar.size.x * frac, bar.size.y}}, 2.f, cat);
    else if (done)
      draw::roundRect(rt, bar, 2.f, Theme::withAlpha(Theme::High, 160));
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2fs", el);
    if (detail)
      text(rt, "PT " + parseFloatToTimeString(pt), {extra.position.x, extra.position.y - 1.f}, 11, Theme::TextDim);
    if (detail)
      text(rt, frac > 0.f ? buf : "", {extra.position.x + extra.size.x, extra.position.y - 1.f}, 11, cat,
         draw::Align::Right);
  }
  else if (info.presetValue)
  {
    int cv = ed_.sim->getCurrentCounterValue(n.name), pv = ed_.sim->getPresetCounterValue(n.name);
    if (detail)
    {
      text(rt, "CV " + std::to_string(cv), {extra.position.x, extra.position.y - 1.f}, 12, cat, draw::Align::Left, true);
      text(rt, "PV " + std::to_string(pv), {extra.position.x + extra.size.x, extra.position.y - 1.f}, 11,
           Theme::TextDim, draw::Align::Right);
    }
    float frac = pv > 0 ? std::clamp(static_cast<float>(cv) / pv, 0.f, 1.f) : 0.f;
    sf::FloatRect bar({extra.position.x, extra.position.y + extra.size.y - 5.f}, {extra.size.x, 4.f});
    draw::roundRect(rt, bar, 2.f, Theme::Field);
    if (frac > 0.f)
      draw::roundRect(rt, {bar.position, {bar.size.x * frac, bar.size.y}}, 2.f, cat);
  }
  else if (n.type == gll::NodeType::BTN)
  {
    bool pressed = ed_.sim->isButtonPressed(n.name), latched = ed_.sim->isButtonLatched(n.name);
    sf::FloatRect b = buttonRect(n);
    bool hot = hoverNode_ >= 0 && &ed_.graph.nodes[hoverNode_] == &n && b.contains(mouseWorld_);
    sf::Color fill = pressed ? Theme::withAlpha(Theme::High, 120)
                     : latched ? Theme::withAlpha(Theme::Warning, 110)
                     : hot     ? Theme::ButtonHover
                               : Theme::ButtonDefault;
    draw::roundRect(rt, b, 6.f, fill, pressed || latched ? Theme::High : Theme::Border, 1.f);
    if (detail)
      text(rt, latched ? "HOLD  (ctrl+click)" : "PRESS", {b.position.x + b.size.x / 2.f, b.position.y + 3.f}, 11,
         Theme::TextDefault, draw::Align::Center, true);
  }
}

void Canvas::drawTerminal(sf::RenderTarget &rt, const Node &n, bool selected)
{
  const sf::FloatRect r = n.rect;
  const float x = r.position.x, y = r.position.y, w = r.size.x, h = r.size.y;
  const bool analog = n.isAnalog();
  const bool input = n.kind == NodeKind::Input || n.kind == NodeKind::AInput;
  const sf::Color accent = analog ? Theme::AnalogTerminal : Theme::Terminal;
  const bool high = nodeHigh(n);

  draw::roundRect(rt, {r.position + sf::Vector2f(0, 4), r.size}, h / 2.f, sf::Color(0, 0, 0, 70));
  sf::Color border = selected ? Theme::Accent : high ? Theme::withAlpha(Theme::High, 170) : Theme::NodeBorder;
  draw::roundRect(rt, r, h / 2.f, Theme::NodeBody, border, selected ? 2.f : 1.2f);

  float textX = x + 16.f;
  if (n.kind == NodeKind::Input)
  {
    sf::FloatRect s = switchRect(n);
    draw::roundRect(rt, s, 9.f, high ? Theme::withAlpha(Theme::High, 200) : Theme::Field, Theme::Border, 1.f);
    draw::circle(rt, {high ? s.position.x + s.size.x - 9.f : s.position.x + 9.f, s.position.y + 9.f}, 6.5f,
                 sf::Color::White);
    textX = s.position.x + s.size.x + 10.f;
  }
  else if (n.kind == NodeKind::Output)
  {
    sf::Vector2f c(x + 24.f, y + h / 2.f);
    if (high)
      draw::circle(rt, c, 11.f, Theme::withAlpha(Theme::High, 60));
    draw::circle(rt, c, 7.f, high ? Theme::High : Theme::Field, Theme::Border, 1.f);
    textX = x + 40.f;
  }

  float maxText = analog ? w - (textX - x) - 92.f : w - (textX - x) - 16.f;
  std::string kw = n.kind == NodeKind::Input ? "IN" : n.kind == NodeKind::Output ? "OUT"
                 : n.kind == NodeKind::AInput ? "AIN" : "AOUT";
  std::string sub = n.subtitle.empty() ? kw : kw + " · " + n.subtitle;
  if (zoom_ > 0.45f)
  {
    text(rt, draw::fit(ed_.ui.bold, n.name, 13, maxText), {textX, y + 5.f}, 13, Theme::TextDefault,
         draw::Align::Left, true);
    text(rt, draw::fit(ed_.ui.font, sub, 10, maxText), {textX, y + 23.f}, 10, Theme::withAlpha(accent, 200));
  }
  else
  {
    unsigned size = static_cast<unsigned>(std::min(11.f / zoom_, 36.f));
    text(rt, draw::fit(ed_.ui.bold, n.name, size, x + w - textX - 10.f), {textX, y + h / 2.f - size * 0.7f}, size,
         Theme::TextDefault, draw::Align::Left, true);
  }

  if (analog && ed_.sim)
  {
    uint64_t v = ed_.sim->getAnalogSignalValue(n.name);
    sf::FloatRect vr = valueRect(n);
    bool hot = n.kind == NodeKind::AInput && vr.contains(mouseWorld_) && hoverNode_ >= 0;
    draw::roundRect(rt, vr, 6.f, hot ? Theme::FieldHover : Theme::Field, Theme::withAlpha(accent, 120), 1.f);
    text(rt, draw::fit(ed_.ui.font, std::to_string(v), 13, vr.size.x - 10.f),
         {vr.position.x + vr.size.x / 2.f, vr.position.y + vr.size.y / 2.f - 9.f}, 13, Theme::Analog,
         draw::Align::Center);
  }

  for (const auto &p : n.ins)
    drawPort(rt, p, true, false);
  for (const auto &p : n.outs)
    drawPort(rt, p, false, false);
  (void)input;
}

void Canvas::drawNode(sf::RenderTarget &rt, int index)
{
  const Node &n = ed_.graph.nodes[index];
  bool selected = ed_.selection.count(n.key) > 0;
  if (n.isGate())
    drawGate(rt, n, selected);
  else
    drawTerminal(rt, n, selected);
}

void Canvas::draw(sf::RenderTarget &rt)
{
  sf::RectangleShape bg(screen_.size);
  bg.setPosition(screen_.position);
  bg.setFillColor(Theme::Background);
  rt.draw(bg);

  sf::View old = rt.getView();
  sf::View view(center_, screen_.size / zoom_);
  sf::Vector2f win(static_cast<float>(window_.x), static_cast<float>(window_.y));
  view.setViewport({{screen_.position.x / win.x, screen_.position.y / win.y},
                    {screen_.size.x / win.x, screen_.size.y / win.y}});
  rt.setView(view);

  drawGrid(rt);
  drawEdges(rt);

  // Selected nodes on top.
  const auto &nodes = ed_.graph.nodes;
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    if (!ed_.selection.count(nodes[i].key))
      drawNode(rt, i);
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    if (ed_.selection.count(nodes[i].key))
      drawNode(rt, i);

  // Hover ring on ports, highlighting valid targets while wiring.
  if (mode_ == Mode::Wire && wireFrom_)
  {
    const auto &fromNode = nodes[wireFrom_.node];
    sf::Vector2f a = wireFrom_.input ? fromNode.ins[wireFrom_.port].pos : fromNode.outs[wireFrom_.port].pos;
    sf::Vector2f c[4];
    PortRef snap = portAt(mouseWorld_);
    sf::Vector2f b = mouseWorld_;
    if (snap && snap.input != wireFrom_.input)
      b = snap.input ? nodes[snap.node].ins[snap.port].pos : nodes[snap.node].outs[snap.port].pos;
    if (wireFrom_.input)
      wireCurve(b, a, c);
    else
      wireCurve(a, b, c);
    draw::bezier(rt, c[0], c[1], c[2], c[3], 2.5f, Theme::Accent);
    for (const auto &n : nodes)
    {
      const auto &ports = wireFrom_.input ? n.outs : n.ins;
      for (const auto &p : ports)
        draw::circle(rt, p.pos, 8.f, sf::Color::Transparent, Theme::withAlpha(Theme::Accent, 90), 1.5f);
    }
    if (snap && snap.input != wireFrom_.input)
      draw::circle(rt, b, 9.f, Theme::withAlpha(Theme::Accent, 60), Theme::Accent, 2.f);
  }
  else if (hoverPort_)
  {
    const auto &p = hoverPort_.input ? nodes[hoverPort_.node].ins[hoverPort_.port]
                                     : nodes[hoverPort_.node].outs[hoverPort_.port];
    draw::circle(rt, p.pos, 8.f, Theme::withAlpha(Theme::Accent, 50), Theme::Accent, 1.5f);
  }

  if (mode_ == Mode::Box)
  {
    sf::Vector2f a = toWorld(pressScreen_), b = mouseWorld_;
    sf::FloatRect box({std::min(a.x, b.x), std::min(a.y, b.y)}, {std::abs(a.x - b.x), std::abs(a.y - b.y)});
    draw::roundRect(rt, box, 2.f, Theme::withAlpha(Theme::Accent, 30), Theme::Accent, 1.f / zoom_);
  }

  rt.setView(old);

  if (nodes.empty())
  {
    sf::Vector2f c = screen_.position + screen_.size / 2.f;
    draw::text(rt, ed_.ui.bold, "Empty circuit", c - sf::Vector2f(0, 30.f), 18, Theme::TextDim, draw::Align::Center);
    draw::text(rt, ed_.ui.font, "Drag nodes in from the palette, or right-click to add one.", c, 13,
               Theme::TextFaint, draw::Align::Center);
  }
}
