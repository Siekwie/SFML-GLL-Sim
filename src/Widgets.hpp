#pragma once
// Drawing helpers and a small immediate-mode widget layer.
//
// Every panel is redrawn each frame; widgets both draw themselves and report
// interaction (`if (ui.button(...)) doThing();`). Text fields commit through
// a callback so a click that changes the selection still commits the edit to
// the object that was being edited.
#include "Theme.hpp"
#include <SFML/Graphics.hpp>
#include <functional>
#include <string>
#include <vector>

// ---- drawing ------------------------------------------------------------------

namespace draw
{
void roundRect(sf::RenderTarget &rt, sf::FloatRect r, float radius, sf::Color fill,
               sf::Color outline = sf::Color::Transparent, float thickness = 0.f);
void line(sf::RenderTarget &rt, sf::Vector2f a, sf::Vector2f b, float width, sf::Color c);
void circle(sf::RenderTarget &rt, sf::Vector2f center, float radius, sf::Color fill,
            sf::Color outline = sf::Color::Transparent, float thickness = 0.f);
// Cubic bezier as a triangle strip. `dash` > 0 draws a dashed curve.
void bezier(sf::RenderTarget &rt, sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3,
            float width, sf::Color c, float dash = 0.f);

enum class Align
{
  Left,
  Center,
  Right
};

// UTF-8 text. `scale` > 1 renders at a larger character size and scales the
// glyphs down again, which keeps text crisp inside a zoomed sf::View.
void text(sf::RenderTarget &rt, const sf::Font &font, const std::string &utf8, sf::Vector2f pos,
          unsigned size, sf::Color color, Align align = Align::Left, float scale = 1.f);
float textWidth(const sf::Font &font, const std::string &utf8, unsigned size);
// Shorten with an ellipsis so it fits `maxWidth`.
std::string fit(const sf::Font &font, const std::string &utf8, unsigned size, float maxWidth);
sf::String utf8(const std::string &s);
}  // namespace draw

// ---- input ----------------------------------------------------------------------

struct FrameInput
{
  sf::Vector2f mouse;
  bool down[3] = {};      // left, right, middle
  bool pressed[3] = {};
  bool released[3] = {};
  bool doubleClick = false;
  float wheel = 0.f;
  float wheelH = 0.f;
  std::vector<sf::Keyboard::Key> keys;  // key presses this frame
  std::string text;                     // UTF-8 typed this frame
  bool ctrl = false, shift = false, alt = false;

  bool key(sf::Keyboard::Key k) const;
};

// ---- widgets --------------------------------------------------------------------

struct MenuItem
{
  std::string label;
  std::function<void()> action;  // empty = section header
  bool enabled = true;
  bool checked = false;
};

class Ui
{
public:
  Ui(const sf::Font &regular, const sf::Font &bold) : font(regular), bold(bold) {}

  const sf::Font &font;
  const sf::Font &bold;
  sf::RenderTarget *rt = nullptr;
  FrameInput in;
  float time = 0.f;

  // Begin a frame: handles focus loss / menu clicks before anything else
  // sees this frame's input.
  void begin(sf::RenderTarget &target, const FrameInput &input, float dt);
  // Draw overlays (menu, tooltip).
  void end();

  // While false, widgets draw but ignore the mouse (used behind modals).
  bool interactive = true;
  // True once something consumed this frame's mouse press.
  bool mouseTaken = false;
  bool takeClick(sf::FloatRect r, int button = 0);  // pressed inside r and not taken yet
  bool hovering(sf::FloatRect r) const;

  enum class Style
  {
    Normal,
    Primary,
    Active,
    Danger,
    Ghost
  };
  bool button(sf::FloatRect r, const std::string &label, Style style = Style::Normal, bool enabled = true);
  // Segmented / chip toggle.
  bool chip(sf::FloatRect r, const std::string &label, bool on, sf::Color onColor = Theme::Accent);
  // Returns true while being dragged; v in [0,1].
  bool slider(const std::string &id, sf::FloatRect r, float &v);

  // Single line text field. `onCommit` runs on Enter, Tab or focus loss when
  // the text changed. Esc reverts.
  void textField(const std::string &id, sf::FloatRect r, const std::string &value,
                 std::function<void(const std::string &)> onCommit, const std::string &placeholder = {},
                 bool enabled = true);
  bool hasFocus() const { return !focus_.empty(); }
  bool isFocused(const std::string &id) const { return focus_ == id; }
  void focus(const std::string &id) { requestFocus_ = id; }
  void clearFocus(bool commit);

  void tooltip(sf::FloatRect r, const std::string &text);
  void showMenu(sf::Vector2f at, std::vector<MenuItem> items);
  bool menuOpen() const { return !menu_.empty(); }
  sf::FloatRect menuRect() const { return menuRect_; }

  void label(sf::Vector2f pos, const std::string &s, sf::Color c = Theme::TextDefault, unsigned size = 13,
             draw::Align a = draw::Align::Left);
  void sectionTitle(sf::Vector2f pos, const std::string &s);

private:
  std::string focus_, requestFocus_;
  std::string buffer_, original_;
  bool selectAll_ = false;
  sf::FloatRect focusRect_;
  std::function<void(const std::string &)> commit_;
  bool focusSeen_ = false;

  std::string sliderActive_;

  std::vector<MenuItem> menu_;
  sf::Vector2f menuPos_;
  sf::FloatRect menuRect_;

  std::string tip_;
  sf::FloatRect tipRect_;
  float tipTimer_ = 0.f;
  float dt_ = 0.f;
  bool tipSet_ = false;
};
