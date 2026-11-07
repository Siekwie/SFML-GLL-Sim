#pragma once
#include "Canvas.hpp"
#include "Panels.hpp"
#include <SFML/Graphics.hpp>
#include <filesystem>
#include <memory>

class App
{
public:
  // `exeDir` is used to find the vendored font next to the executable.
  App(const std::filesystem::path &file, const std::filesystem::path &exeDir);
  int run();

private:
  bool loadFonts(const std::filesystem::path &exeDir);
  void rebuild();
  FrameInput collectInput();
  void globalShortcuts(const FrameInput &in);
  void frame(float dt);

  std::filesystem::path file_;
  bool ok_ = false;

  sf::RenderWindow window_;
  sf::Font regular_, bold_;
  Document doc_;
  graph::GraphView graph_;
  ModbusManager modbus_;
  std::unique_ptr<Ui> ui_;
  std::unique_ptr<Editor> ed_;
  std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<Simulator> sim_;
  const Program *simProgram_ = nullptr;

  RunState run_;
  ViewState view_;
  Toolbar toolbar_;
  Palette palette_;
  Inspector inspector_;
  CodeView code_;
  StatusBar status_;
  ModbusDialog modbusDialog_;

  bool wasStepping_ = false;
  bool dockDrag_ = false;
  float inspectorShare_ = 0.5f;  // fraction of the dock height for the inspector
  sf::Clock clickClock_;
  sf::Vector2f lastClickPos_;
};
