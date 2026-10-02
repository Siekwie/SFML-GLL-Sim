#pragma once
// The node editor canvas: draws the graph with live signal values and turns
// mouse / keyboard input into edits (Edits.hpp via Editor::apply).
#include "Editor.hpp"
#include <SFML/Graphics.hpp>
#include <optional>

// Something that can be placed on the canvas: a gate type or a terminal.
struct NodeSpec
{
  bool terminal = false;
  gll::NodeType type = gll::NodeType::AND_;
  gll::DeclKind decl = gll::DeclKind::In;

  std::string keyword() const;
  std::string summary() const;
  sf::Color color() const;
};
const std::vector<NodeSpec> &paletteSpecs();  // grouped in palette order

class Canvas
{
public:
  explicit Canvas(Editor &ed) : ed_(ed) {}

  // Screen rectangle the canvas occupies inside a window of `windowSize`.
  void setViewport(sf::FloatRect screen, sf::Vector2u windowSize);
  sf::FloatRect viewport() const { return screen_; }

  // `mouseFree`: the cursor is over the canvas and not over a panel.
  void handle(const FrameInput &in, bool mouseFree);
  void draw(sf::RenderTarget &rt);

  // True while a canvas drag is in progress (keeps input even off-canvas).
  bool capturing() const { return mode_ != Mode::Idle; }

  void fitView();
  void zoomBy(float factor);
  void relayout();
  void reveal(const std::string &key);  // pan so the node is visible

  // Create a node; `screenPos` = where it was dropped (nullopt: view centre).
  void addNode(const NodeSpec &spec, std::optional<sf::Vector2f> screenPos = std::nullopt);
  void deleteSelection();

  sf::Vector2f toWorld(sf::Vector2f screen) const;
  sf::Vector2f toScreen(sf::Vector2f world) const;
  float zoom() const { return zoom_; }

private:
  enum class Mode
  {
    Idle,
    Pan,
    Box,
    MovePending,
    Move,
    Wire,
    Button
  };

  struct PortRef
  {
    int node = -1, port = -1;
    bool input = false;
    explicit operator bool() const { return node >= 0; }
  };

  PortRef portAt(sf::Vector2f world) const;
  int nodeAt(sf::Vector2f world) const;
  int edgeAt(sf::Vector2f world) const;
  void wireCurve(sf::Vector2f a, sf::Vector2f b, sf::Vector2f out[4]) const;

  void beginWire(PortRef port);
  void finishWire(sf::Vector2f world);
  bool connect(PortRef out, PortRef in, std::optional<PortRef> detach);
  void detachOnly(PortRef in);
  void openContextMenu(sf::Vector2f screen);
  void openAddMenu(sf::Vector2f screen, std::optional<PortRef> autoConnect);
  void handleKeys(const FrameInput &in);

  // Clickable parts of terminal / button nodes (world coordinates).
  sf::FloatRect switchRect(const graph::Node &n) const;
  sf::FloatRect buttonRect(const graph::Node &n) const;
  sf::FloatRect valueRect(const graph::Node &n) const;

  void drawGrid(sf::RenderTarget &rt);
  void drawEdges(sf::RenderTarget &rt);
  void drawNode(sf::RenderTarget &rt, int index);
  void drawGate(sf::RenderTarget &rt, const graph::Node &n, bool selected);
  void drawTerminal(sf::RenderTarget &rt, const graph::Node &n, bool selected);
  void drawPort(sf::RenderTarget &rt, const graph::Port &p, bool input, bool gate);
  sf::Color signalColor(int signal, bool wire) const;
  bool nodeHigh(const graph::Node &n) const;
  void text(sf::RenderTarget &rt, const std::string &s, sf::Vector2f pos, unsigned size, sf::Color c,
            draw::Align a = draw::Align::Left, bool bold = false);

  Editor &ed_;
  sf::FloatRect screen_;
  sf::Vector2u window_;
  sf::Vector2f center_{300.f, 0.f};
  float zoom_ = 1.f;
  bool fitted_ = false;

  Mode mode_ = Mode::Idle;
  sf::Vector2f pressScreen_, lastScreen_, mouseWorld_;
  bool rightDrag_ = false;
  sf::Vector2f rightPress_;
  std::vector<std::pair<std::string, sf::Vector2f>> moveStart_;
  sf::Vector2f moveOrigin_;

  PortRef wireFrom_;                 // fixed end of the wire being dragged
  std::optional<PortRef> detach_;    // input port the wire was picked up from
  PortRef hoverPort_;
  int hoverNode_ = -1;
  std::string pressedButton_;        // BTN node held down
};
