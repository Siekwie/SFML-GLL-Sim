#pragma once
// Shared state of the editor window: the document, the graph derived from
// it, the running simulator and the current selection. Canvas and panels
// read and act through this instead of talking to each other directly.
#include "Document.hpp"
#include "Edits.hpp"
#include "GraphView.hpp"
#include "ModbusManager.hpp"
#include "Sim.hpp"
#include "Widgets.hpp"
#include <functional>
#include <set>
#include <string>

struct Editor
{
  Document &doc;
  graph::GraphView &graph;
  ModbusManager &modbus;
  Ui &ui;
  Simulator *sim = nullptr;

  std::set<std::string> selection;  // node keys
  int selectedEdge = -1;
  std::string focusField;           // inspector field to focus next frame ("name", "value")
  bool showScan = false;            // highlight the node being evaluated (slow speeds)
  int revealLine = -1;              // code view should scroll to this line

  // Set by App: rebuild graph + simulator after the document changed.
  std::function<void()> rebuild;

  // Run a structural edit; the graph is rebuilt before this returns, so
  // node / port indices taken before the call are invalid afterwards.
  bool apply(const std::string &label, const std::function<bool(gll::Script &)> &fn)
  {
    uint64_t before = doc.revision();
    bool ok = doc.edit(label, fn);
    if (doc.revision() != before && rebuild)
      rebuild();
    return ok;
  }

  void select(const std::string &key, bool additive = false)
  {
    selectedEdge = -1;
    if (!additive)
      selection.clear();
    if (key.empty())
      return;
    if (additive && selection.count(key))
      selection.erase(key);
    else
      selection.insert(key);
  }

  // The single selected node, or -1.
  int primary() const
  {
    if (selection.size() != 1)
      return -1;
    return graph.find(*selection.begin());
  }

  // Signal value as shown to the user (includes not yet committed toggles).
  uint64_t value(int signal) const
  {
    if (!sim || signal < 0 || signal >= static_cast<int>(sim->signals().size()))
      return 0;
    return sim->signals()[signal];
  }
  bool isAnalog(int signal) const
  {
    auto p = doc.program();
    return p && p->analogSignals.count(signal) > 0;
  }

  // Default name for a new node of a kind ("and", "ton", "in" ...).
  static std::string baseName(const std::string &keyword)
  {
    std::string s;
    for (char c : keyword)
      s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }
};
