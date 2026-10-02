#pragma once
// Visual graph derived from the script: one node per gate statement and per
// declared IN / OUT / AIN / AOUT signal, wires from signal producers to
// consumers. Rebuilt from scratch whenever the document changes; positions
// live in Document::positions() so they survive rebuilds.
#include "Document.hpp"
#include <SFML/Graphics/Rect.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace graph
{

enum class NodeKind
{
  Input,
  Output,
  AInput,
  AOutput,
  Gate
};

struct Port
{
  std::string label;      // "S", "CU", "Q", "CV" ... (empty for variadic inputs)
  int index = -1;         // input: argument index; output: 0 = Q, 1 = CV
  gll::Mod mod = gll::Mod::None;
  std::string symbol;     // connected signal name, empty when unconnected
  bool literal = false;   // comparator constant (symbol holds the literal)
  bool spare = false;     // the extra "+" input of AND / OR / XOR
  bool dangling = false;  // refers to a signal nothing drives
  int signal = -1;
  sf::Vector2f pos;       // world position of the port dot
};

struct Node
{
  std::string key;  // stable id: "g:name", "i:name", "o:name", "ai:name", "ao:name"
  NodeKind kind = NodeKind::Gate;
  int line = -1;    // statement line (gate or declaration)
  int item = -1;    // declaration item index (terminals)
  gll::NodeType type = gll::NodeType::AND_;
  std::string name;     // gate name / terminal display name
  std::string subtitle; // terminal: real name when an alias is used
  int signal = -1;      // terminal signal
  int progNode = -1;    // index into Program::nodes (gates)
  int order = 0;        // 1-based scan order (gates)
  std::vector<Port> ins, outs;
  sf::FloatRect rect;

  bool isGate() const { return kind == NodeKind::Gate; }
  bool isAnalog() const { return kind == NodeKind::AInput || kind == NodeKind::AOutput; }
};

struct Edge
{
  int from = -1, fromPort = -1;  // node / output port
  int to = -1, toPort = -1;      // node / input port
  int signal = -1;
  std::string symbol;
  bool feedback = false;  // consumer is scanned before the producer: 1-scan delay
};

// Layout metrics shared with the canvas renderer.
namespace metrics
{
inline constexpr float GateWidth = 184.f;
inline constexpr float TerminalWidth = 172.f;
inline constexpr float TerminalHeight = 42.f;
inline constexpr float Header = 30.f;
inline constexpr float Row = 22.f;
inline constexpr float BodyPad = 6.f;
inline constexpr float Extra = 28.f;  // timer bar / counter / button row
}  // namespace metrics

class GraphView
{
public:
  void build(const gll::Script &script, const Program &prog, Document::Positions &positions);
  // Layered left-to-right layout. With onlyMissing, nodes that already have a
  // position keep it and new ones are placed without overlapping them.
  void autoLayout(Document::Positions &positions, bool onlyMissing);
  // Apply positions to node rects and port coordinates.
  void place(const Document::Positions &positions);

  int find(const std::string &key) const;
  int nodeForLine(int line) const;  // gate node of a statement line
  int nodeForSignal(int signal) const;  // terminal node of a declared signal

  std::vector<Node> nodes;
  std::vector<Edge> edges;

private:
  std::unordered_map<std::string, int> index_;
};

}  // namespace graph
