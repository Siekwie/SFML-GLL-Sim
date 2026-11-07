#pragma once
#include <SFML/Graphics/Color.hpp>
#include "Gll.hpp"

namespace Theme {
  // Surfaces
  inline const sf::Color Background(22, 23, 27);      // canvas
  inline const sf::Color GridDot(46, 49, 57);
  inline const sf::Color Panel(29, 31, 36);
  inline const sf::Color PanelRaised(36, 38, 45);
  inline const sf::Color Border(48, 51, 60);
  inline const sf::Color Field(22, 23, 27);
  inline const sf::Color FieldHover(30, 32, 38);

  // Text
  inline const sf::Color TextDefault(226, 228, 234);
  inline const sf::Color TextDim(150, 155, 168);
  inline const sf::Color TextFaint(102, 107, 121);

  // Signal colours (shared by canvas wires and the code view)
  inline const sf::Color TextGreen(100, 200, 100);   // code view: HIGH
  inline const sf::Color TextRed(200, 100, 100);     // code view: LOW
  inline const sf::Color High(61, 220, 132);
  inline const sf::Color LowWire(80, 85, 99);
  inline const sf::Color LowPort(150, 72, 72);
  inline const sf::Color Analog(240, 160, 75);

  // Accents
  inline const sf::Color Accent(91, 140, 255);        // selection / focus
  inline const sf::Color Scan(255, 209, 102);         // node being evaluated
  inline const sf::Color Error(255, 92, 92);
  inline const sf::Color Warning(242, 180, 67);

  // Nodes
  inline const sf::Color NodeBody(38, 41, 48);
  inline const sf::Color NodeHeader(45, 48, 57);
  inline const sf::Color NodeBorder(62, 66, 78);

  // Buttons
  inline const sf::Color ButtonDefault(44, 47, 56);
  inline const sf::Color ButtonHover(55, 59, 70);
  inline const sf::Color ButtonActive(66, 71, 84);
  inline const sf::Color ButtonRunning(46, 125, 84);
  inline const sf::Color ErrorColor(160, 58, 58);

  inline sf::Color category(gll::Category c)
  {
    switch (c)
    {
    case gll::Category::Logic: return {91, 140, 255};
    case gll::Category::Edge: return {176, 124, 255};
    case gll::Category::Memory: return {47, 196, 178};
    case gll::Category::Timer: return {242, 180, 67};
    case gll::Category::Counter: return {255, 122, 168};
    case gll::Category::Compare: return {76, 201, 240};
    case gll::Category::Input: return {154, 163, 181};
    }
    return {154, 163, 181};
  }
  inline const sf::Color Terminal(120, 200, 150);
  inline const sf::Color AnalogTerminal(240, 160, 75);

  inline sf::Color withAlpha(sf::Color c, uint8_t a) { c.a = a; return c; }
  inline sf::Color mix(sf::Color a, sf::Color b, float t)
  {
    auto l = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
  }

  // Font settings
  inline constexpr unsigned FontSize = 14;
  inline constexpr float CodeFontSize = 15.0f;
  inline constexpr float CodeLineHeight = 21.0f;
}
