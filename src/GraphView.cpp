#include "GraphView.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace graph
{
using namespace metrics;
using gll::Statement;

static bool hasExtraRow(gll::NodeType t)
{
  const auto &info = gll::typeInfo(t);
  return info.presetTime || info.presetValue || t == gll::NodeType::BTN;
}

static sf::Vector2f nodeSize(const Node &n)
{
  if (!n.isGate())
    return {TerminalWidth, TerminalHeight};
  size_t rows = std::max<size_t>({n.ins.size(), n.outs.size(), 1});
  float h = Header + BodyPad + rows * Row + BodyPad + (hasExtraRow(n.type) ? Extra : 0.f);
  return {GateWidth, h};
}

static int signalOf(const Program &prog, const std::string &symbol)
{
  auto it = prog.symbolToSignal.find(symbol);
  return it == prog.symbolToSignal.end() ? -1 : it->second;
}

void GraphView::build(const gll::Script &script, const Program &prog, Document::Positions &positions)
{
  nodes.clear();
  edges.clear();
  index_.clear();

  auto add = [&](Node n)
  {
    // Duplicate names are legal-ish in hand written files; keep keys unique.
    if (index_.count(n.key))
      n.key += "#" + std::to_string(n.line);
    index_[n.key] = static_cast<int>(nodes.size());
    nodes.push_back(std::move(n));
  };

  // Gate statement line -> Program node
  std::unordered_map<int, int> lineToProg;
  for (int i = 0; i < static_cast<int>(prog.nodes.size()); ++i)
    if (!prog.nodes[i].internal)
      lineToProg[prog.nodes[i].sourceLine] = i;

  // Terminals first (declaration order), then gates (scan order).
  for (int l = 0; l < static_cast<int>(script.lines.size()); ++l)
  {
    const Statement &st = script.lines[l];
    if (st.kind != Statement::Kind::Decl)
      continue;
    for (int j = 0; j < static_cast<int>(st.items.size()); ++j)
    {
      const auto &it = st.items[j];
      Node n;
      n.line = l;
      n.item = j;
      n.name = it.display();
      n.subtitle = it.alias.empty() ? "" : it.name;
      n.signal = signalOf(prog, it.name);
      Port p;
      p.index = 0;
      p.symbol = n.name;
      p.signal = n.signal;
      switch (st.decl)
      {
      case gll::DeclKind::In:
        n.kind = NodeKind::Input, n.key = "i:" + it.name, n.outs.push_back(p);
        break;
      case gll::DeclKind::AIn:
        n.kind = NodeKind::AInput, n.key = "ai:" + it.name, n.outs.push_back(p);
        break;
      case gll::DeclKind::Out:
        n.kind = NodeKind::Output, n.key = "o:" + it.name, p.symbol.clear(), n.ins.push_back(p);
        break;
      case gll::DeclKind::AOut:
        n.kind = NodeKind::AOutput, n.key = "ao:" + it.name, p.symbol.clear(), n.ins.push_back(p);
        break;
      }
      add(std::move(n));
    }
  }

  int order = 0;
  for (int l = 0; l < static_cast<int>(script.lines.size()); ++l)
  {
    const Statement &st = script.lines[l];
    if (st.kind != Statement::Kind::Gate)
      continue;
    const auto &info = gll::typeInfo(st.type);
    Node n;
    n.kind = NodeKind::Gate;
    n.key = "g:" + st.name;
    n.line = l;
    n.type = st.type;
    n.name = st.name;
    n.order = ++order;
    auto pi = lineToProg.find(l);
    n.progNode = pi == lineToProg.end() ? -1 : pi->second;

    size_t fixed = info.inputs.size();
    size_t count = info.variadic ? st.args.size() + 1 : std::max(fixed, st.args.size());
    for (size_t a = 0; a < count; ++a)
    {
      Port p;
      p.index = static_cast<int>(a);
      if (!info.variadic)
        p.label = a < fixed ? info.inputs[a] : "?";
      if (a < st.args.size())
      {
        const auto &arg = st.args[a];
        p.mods = arg.mods;
        if (arg.symbol != gll::kNotConnected)
        {
          p.symbol = arg.symbol;
          p.literal = info.literals && gll::parseLiteral(arg.symbol).has_value();
          if (p.literal)
          {
            auto lit = gll::parseLiteral(arg.symbol);
            p.signal = signalOf(prog, "_const_" + std::to_string(*lit));
          }
          else
            p.signal = signalOf(prog, arg.symbol);
        }
      }
      else
        p.spare = info.variadic;
      n.ins.push_back(p);
    }

    // Output ports: Q (all outputs except a counter's CV slot) and CV.
    Port q;
    q.index = 0;
    q.label = "Q";
    for (size_t o = 0; o < st.outputs.size(); ++o)
    {
      if (info.cvOutput && o == 1)
        continue;
      if (q.symbol.empty())
      {
        q.symbol = st.outputs[o].symbol;
        q.signal = signalOf(prog, q.symbol);
      }
    }
    n.outs.push_back(q);
    if (info.cvOutput)
    {
      Port cv;
      cv.index = 1;
      cv.label = "CV";
      if (st.outputs.size() > 1)
      {
        cv.symbol = st.outputs[1].symbol;
        cv.signal = signalOf(prog, cv.symbol);
      }
      n.outs.push_back(cv);
    }
    add(std::move(n));
  }

  // ---- edges ----------------------------------------------------------------
  std::unordered_map<int, std::vector<std::pair<int, int>>> producers;  // signal -> (node, port)
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
  {
    const Node &n = nodes[i];
    if (n.kind == NodeKind::Input || n.kind == NodeKind::AInput)
    {
      if (n.signal >= 0)
        producers[n.signal].push_back({i, 0});
      continue;
    }
    if (!n.isGate())
      continue;
    const Statement &st = script.lines[n.line];
    const bool cv = gll::typeInfo(st.type).cvOutput;
    for (size_t o = 0; o < st.outputs.size(); ++o)
    {
      int sig = signalOf(prog, st.outputs[o].symbol);
      if (sig >= 0)
        producers[sig].push_back({i, (cv && o == 1) ? 1 : 0});
    }
  }

  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
  {
    Node &n = nodes[i];
    for (int p = 0; p < static_cast<int>(n.ins.size()); ++p)
    {
      Port &port = n.ins[p];
      int sig = port.signal;
      if (n.kind == NodeKind::Output || n.kind == NodeKind::AOutput)
        sig = n.signal;
      if (sig < 0 || port.literal)
        continue;
      auto it = producers.find(sig);
      bool any = false;
      if (it != producers.end())
      {
        for (auto [from, fromPort] : it->second)
        {
          if (from == i && !n.isGate())
            continue;
          if (!n.isGate() && !nodes[from].isGate())
            continue;  // IN -> OUT of the same signal is not a wire
          Edge e;
          e.from = from;
          e.fromPort = fromPort;
          e.to = i;
          e.toPort = p;
          e.signal = sig;
          e.symbol = n.isGate() ? port.symbol : n.name;
          e.feedback = n.isGate() && nodes[from].isGate() && nodes[from].line >= n.line;
          edges.push_back(e);
          any = true;
        }
      }
      if (n.isGate() && !port.symbol.empty() && !any)
        port.dangling = true;
    }
  }

  autoLayout(positions, true);
}

int GraphView::find(const std::string &key) const
{
  auto it = index_.find(key);
  return it == index_.end() ? -1 : it->second;
}

int GraphView::nodeForLine(int line) const
{
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    if (nodes[i].isGate() && nodes[i].line == line)
      return i;
  return -1;
}

int GraphView::nodeForSignal(int signal) const
{
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    if (!nodes[i].isGate() && nodes[i].signal == signal)
      return i;
  return -1;
}

void GraphView::autoLayout(Document::Positions &positions, bool onlyMissing)
{
  const int count = static_cast<int>(nodes.size());
  if (count == 0)
    return;
  bool anyMissing = false;
  for (const auto &n : nodes)
    if (!positions.count(n.key))
      anyMissing = true;
  if (onlyMissing && !anyMissing)
  {
    place(positions);
    return;
  }

  // Layer = longest path from the inputs along the signal flow. Cycles
  // (latches, step sequences) are broken at DFS back edges, so the layout
  // follows data flow rather than scan order.
  std::vector<std::vector<int>> out(count);
  for (const auto &e : edges)
    if (nodes[e.from].isGate() && nodes[e.to].isGate() && e.from != e.to)
      out[e.from].push_back(e.to);
  std::vector<int> state(count, 0);  // 0 new, 1 on stack, 2 done
  std::vector<int> order;            // reverse post-order = topological order of the DAG
  std::set<std::pair<int, int>> back;
  for (int root = 0; root < count; ++root)
  {
    if (!nodes[root].isGate() || state[root])
      continue;
    std::vector<std::pair<int, size_t>> stack{{root, 0}};
    state[root] = 1;
    while (!stack.empty())
    {
      auto &[v, next] = stack.back();
      if (next < out[v].size())
      {
        int w = out[v][next++];
        if (state[w] == 1)
          back.insert({v, w});
        else if (state[w] == 0)
        {
          state[w] = 1;
          stack.push_back({w, 0});
        }
        continue;
      }
      state[v] = 2;
      order.push_back(v);
      stack.pop_back();
    }
  }
  std::reverse(order.begin(), order.end());

  std::vector<int> layer(count, 0);
  std::vector<std::vector<int>> preds(count), succs(count);
  for (const auto &e : edges)
  {
    if (back.count({e.from, e.to}) || e.from == e.to)
      continue;
    preds[e.to].push_back(e.from);
    succs[e.from].push_back(e.to);
  }
  int maxLayer = 0;
  for (int i : order)
  {
    int l = 1;
    for (int p : preds[i])
      if (nodes[p].isGate())
        l = std::max(l, layer[p] + 1);
    layer[i] = l;
    maxLayer = std::max(maxLayer, l);
  }
  for (int i = 0; i < count; ++i)
    if (nodes[i].kind == NodeKind::Output || nodes[i].kind == NodeKind::AOutput)
      layer[i] = maxLayer + 1;

  std::vector<std::vector<int>> columns(maxLayer + 2);
  for (int i = 0; i < count; ++i)
    columns[layer[i]].push_back(i);

  std::vector<float> y(count, 0.f);
  constexpr float gapY = 26.f;
  auto stack = [&](std::vector<int> &col)
  {
    float total = 0.f;
    for (int i : col)
      total += nodeSize(nodes[i]).y + gapY;
    float cur = -total / 2.f;
    for (int i : col)
    {
      float h = nodeSize(nodes[i]).y;
      y[i] = cur + h / 2.f;  // centre
      cur += h + gapY;
    }
  };
  for (auto &col : columns)
    stack(col);

  // Barycentre sweeps to reduce crossings.
  auto sweep = [&](bool down)
  {
    for (size_t c = 0; c < columns.size(); ++c)
    {
      auto &col = columns[down ? c : columns.size() - 1 - c];
      std::vector<std::pair<float, int>> keyed;
      for (int i : col)
      {
        const auto &nb = down ? preds[i] : succs[i];
        float key = y[i];
        if (!nb.empty())
        {
          float sum = 0.f;
          for (int j : nb)
            sum += y[j];
          key = sum / nb.size();
        }
        keyed.push_back({key, i});
      }
      std::stable_sort(keyed.begin(), keyed.end(), [](auto &a, auto &b) { return a.first < b.first; });
      for (size_t k = 0; k < col.size(); ++k)
        col[k] = keyed[k].second;
      stack(col);
    }
  };
  for (int pass = 0; pass < 4; ++pass)
  {
    sweep(true);
    sweep(false);
  }

  // Long sequential chains (step sequences) would make one very wide row.
  // Wrap the gate layers into bands of `perBand` columns so the whole
  // circuit stays close to a screen-like aspect ratio.
  constexpr float colGap = 96.f, bandGap = 70.f;
  const float gateCol = GateWidth + colGap, termCol = TerminalWidth + colGap;
  std::vector<float> colH(columns.size(), 0.f);
  for (size_t c = 0; c < columns.size(); ++c)
    for (int i : columns[c])
      colH[c] += nodeSize(nodes[i]).y + gapY;

  auto bandHeights = [&](int perBand)
  {
    std::vector<float> h((maxLayer + perBand - 1) / std::max(perBand, 1), 0.f);
    for (int l = 1; l <= maxLayer; ++l)
      h[(l - 1) / perBand] = std::max(h[(l - 1) / perBand], colH[l]);
    return h;
  };
  int perBand = std::max(maxLayer, 1);
  float bestScore = 1e9f;
  for (int k = std::max(maxLayer, 1); k >= 1; --k)
  {
    auto h = bandHeights(k);
    float total = 0.f;
    for (float b : h)
      total += b + bandGap;
    total = std::max({total, colH.front(), colH.back(), 1.f});
    float width = 2 * termCol + k * gateCol;
    float score = std::abs(std::log(width / total / 1.7f));
    if (score < bestScore - 0.01f)
      bestScore = score, perBand = k;
  }
  std::vector<float> bandH = maxLayer > 0 ? bandHeights(perBand) : std::vector<float>{};
  std::vector<float> bandMid(bandH.size(), 0.f);
  float totalH = 0.f;
  for (size_t b = 0; b < bandH.size(); ++b)
  {
    bandMid[b] = totalH + bandH[b] / 2.f;
    totalH += bandH[b] + bandGap;
  }
  totalH = std::max(totalH - bandGap, 0.f);

  auto slot = [&](int i) -> sf::Vector2f  // centre of node i
  {
    int l = layer[i];
    if (l == 0)
      return {TerminalWidth / 2.f, totalH / 2.f + y[i]};
    if (l > maxLayer)
      return {termCol + perBand * gateCol + TerminalWidth / 2.f, totalH / 2.f + y[i]};
    int b = (l - 1) / perBand, c = (l - 1) % perBand;
    return {termCol + c * gateCol + GateWidth / 2.f, bandMid[b] + y[i]};
  };

  std::vector<sf::FloatRect> placed;
  if (onlyMissing)
    for (const auto &n : nodes)
      if (auto it = positions.find(n.key); it != positions.end())
        placed.push_back({it->second, nodeSize(n)});

  bool allMissing = true;
  for (const auto &n : nodes)
    if (positions.count(n.key))
      allMissing = false;

  for (int i = 0; i < count; ++i)
  {
    if (onlyMissing && positions.count(nodes[i].key))
      continue;
    sf::Vector2f size = nodeSize(nodes[i]);
    sf::Vector2f pos = slot(i) - size / 2.f;
    if (onlyMissing && !allMissing)
    {
      // Don't drop new nodes on top of hand-placed ones.
      for (int guard = 0; guard < 200; ++guard)
      {
        sf::FloatRect r(pos - sf::Vector2f(10.f, 10.f), size + sf::Vector2f(20.f, 20.f));
        bool hit = false;
        for (const auto &p : placed)
          if (r.findIntersection(p))
            hit = true;
        if (!hit)
          break;
        pos.y += 30.f;
      }
      placed.push_back({pos, size});
    }
    positions[nodes[i].key] = {std::round(pos.x), std::round(pos.y)};
  }
  place(positions);
}

void GraphView::place(const Document::Positions &positions)
{
  for (auto &n : nodes)
  {
    auto it = positions.find(n.key);
    sf::Vector2f pos = it != positions.end() ? it->second : sf::Vector2f();
    n.rect = {pos, nodeSize(n)};
    if (n.isGate())
    {
      float top = pos.y + Header + BodyPad + Row / 2.f;
      for (size_t i = 0; i < n.ins.size(); ++i)
        n.ins[i].pos = {pos.x, top + i * Row};
      for (size_t i = 0; i < n.outs.size(); ++i)
        n.outs[i].pos = {pos.x + n.rect.size.x, top + i * Row};
    }
    else
    {
      float cy = pos.y + n.rect.size.y / 2.f;
      for (auto &p : n.ins)
        p.pos = {pos.x, cy};
      for (auto &p : n.outs)
        p.pos = {pos.x + n.rect.size.x, cy};
    }
  }
}

}  // namespace graph
