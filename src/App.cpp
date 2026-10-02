#include "App.hpp"
#include <algorithm>
#include <cstdio>

using K = sf::Keyboard::Key;

App::App(const std::filesystem::path &file, const std::filesystem::path &exeDir) : file_(file)
{
  std::string err;
  if (!doc_.open(file, err))
  {
    fprintf(stderr, "%s\n", err.c_str());
    return;
  }
  if (!loadFonts(exeDir))
  {
    fprintf(stderr, "Error: no usable font found (looked for vendored/Geist next to the executable and in the working directory).\n");
    return;
  }

  ui_ = std::make_unique<Ui>(regular_, bold_);
  ed_ = std::unique_ptr<Editor>(new Editor{doc_, graph_, modbus_, *ui_});
  ed_->rebuild = [this] { rebuild(); };
  canvas_ = std::make_unique<Canvas>(*ed_);
  rebuild();

  sf::ContextSettings settings;
  settings.antiAliasingLevel = 4;
  auto desktop = sf::VideoMode::getDesktopMode();
  sf::Vector2u size(std::max(1100u, desktop.size.x * 9 / 10), std::max(700u, desktop.size.y * 9 / 10));
  size = {std::min(size.x, desktop.size.x), std::min(size.y, desktop.size.y)};
  window_.create(sf::VideoMode(size), "GLL — " + doc_.fileName(), sf::Style::Default, sf::State::Windowed, settings);
  window_.setFramerateLimit(60);
  window_.setKeyRepeatEnabled(true);
  ok_ = true;
}

bool App::loadFonts(const std::filesystem::path &exeDir)
{
  std::vector<std::filesystem::path> roots = {".", exeDir, exeDir / "..", exeDir / ".." / ".."};
  for (const auto &root : roots)
  {
    auto dir = root / "vendored" / "Geist" / "static";
    if (regular_.openFromFile(dir / "Geist-Regular.ttf"))
    {
      if (!bold_.openFromFile(dir / "Geist-SemiBold.ttf"))
        bold_ = regular_;
      return true;
    }
  }
  // System fallbacks, as in V1.
  static const char *fallbacks[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                    "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                                    "/System/Library/Fonts/Helvetica.ttc"};
  for (const char *f : fallbacks)
    if (regular_.openFromFile(f))
    {
      bold_ = regular_;
      return true;
    }
  return false;
}

void App::rebuild()
{
  auto prog = doc_.program();
  if (!sim_ || prog.get() != simProgram_)
  {
    auto sim = std::make_unique<Simulator>(prog);
    if (sim_)
      sim->transferStateFrom(*sim_);
    sim_ = std::move(sim);
    simProgram_ = prog.get();
    wasStepping_ = false;
  }
  ed_->sim = sim_.get();
  graph_.build(doc_.script(), *prog, doc_.positions());

  ed_->selectedEdge = -1;
  for (auto it = ed_->selection.begin(); it != ed_->selection.end();)
    it = graph_.find(*it) < 0 ? ed_->selection.erase(it) : std::next(it);
}

FrameInput App::collectInput()
{
  FrameInput in;
  while (auto ev = window_.pollEvent())
  {
    if (ev->is<sf::Event::Closed>())
      window_.close();
    else if (auto *r = ev->getIf<sf::Event::Resized>())
      window_.setView(sf::View(sf::FloatRect({0.f, 0.f}, {static_cast<float>(r->size.x), static_cast<float>(r->size.y)})));
    else if (auto *m = ev->getIf<sf::Event::MouseButtonPressed>())
    {
      int b = m->button == sf::Mouse::Button::Left ? 0 : m->button == sf::Mouse::Button::Right ? 1
            : m->button == sf::Mouse::Button::Middle ? 2 : -1;
      if (b < 0)
        continue;
      in.pressed[b] = true;
      if (b == 0)
      {
        sf::Vector2f p(static_cast<float>(m->position.x), static_cast<float>(m->position.y));
        sf::Vector2f d = p - lastClickPos_;
        in.doubleClick = clickClock_.getElapsedTime().asSeconds() < 0.35f && d.x * d.x + d.y * d.y < 25.f;
        clickClock_.restart();
        lastClickPos_ = p;
      }
    }
    else if (auto *m = ev->getIf<sf::Event::MouseButtonReleased>())
    {
      int b = m->button == sf::Mouse::Button::Left ? 0 : m->button == sf::Mouse::Button::Right ? 1
            : m->button == sf::Mouse::Button::Middle ? 2 : -1;
      if (b >= 0)
        in.released[b] = true;
    }
    else if (auto *w = ev->getIf<sf::Event::MouseWheelScrolled>())
    {
      if (w->wheel == sf::Mouse::Wheel::Vertical)
        in.wheel += w->delta;
      else
        in.wheelH += w->delta;
    }
    else if (auto *k = ev->getIf<sf::Event::KeyPressed>())
    {
      in.keys.push_back(k->code);
      // Modifiers from the event itself: polling alone misses fast combos.
      in.ctrl |= k->control || k->system;
      in.shift |= k->shift;
      in.alt |= k->alt;
    }
    else if (auto *t = ev->getIf<sf::Event::TextEntered>())
    {
      if (t->unicode >= 32 && t->unicode != 127)
      {
        auto u = sf::String(t->unicode).toUtf8();
        in.text.append(u.begin(), u.end());
      }
    }
  }
  sf::Vector2i mp = sf::Mouse::getPosition(window_);
  in.mouse = {static_cast<float>(mp.x), static_cast<float>(mp.y)};
  in.down[0] = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
  in.down[1] = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
  in.down[2] = sf::Mouse::isButtonPressed(sf::Mouse::Button::Middle);
  in.ctrl = in.ctrl || sf::Keyboard::isKeyPressed(K::LControl) || sf::Keyboard::isKeyPressed(K::RControl) ||
            sf::Keyboard::isKeyPressed(K::LSystem) || sf::Keyboard::isKeyPressed(K::RSystem);
  in.shift = in.shift || sf::Keyboard::isKeyPressed(K::LShift) || sf::Keyboard::isKeyPressed(K::RShift);
  in.alt = in.alt || sf::Keyboard::isKeyPressed(K::LAlt) || sf::Keyboard::isKeyPressed(K::RAlt);
  // A key combo with Ctrl must not also type text.
  if (in.ctrl)
    in.text.clear();
  return in;
}

void App::globalShortcuts(const FrameInput &in)
{
  if (ui_->hasFocus() || ui_->menuOpen() || view_.modbus)
    return;
  for (auto k : in.keys)
  {
    if (in.ctrl && (k == K::Z || k == K::Y))
    {
      bool redo = k == K::Y || in.shift;
      if (redo ? doc_.redo() : doc_.undo())
        rebuild();
      continue;
    }
    if (in.ctrl)
      continue;
    switch (k)
    {
    case K::Space:
      run_.running = !run_.running;
      break;
    case K::Period:
      run_.stepRequested = true;
      break;
    case K::Equal:
    case K::Add:
      run_.slider = std::min(1.f, run_.slider + 0.1f);
      break;
    case K::Hyphen:
    case K::Subtract:
      run_.slider = std::max(0.f, run_.slider - 0.1f);
      break;
    case K::Tab:
      view_.dock = !view_.dock;
      break;
    case K::P:
      view_.palette = !view_.palette;
      break;
    case K::F1:
      view_.help = !view_.help;
      break;
    default:
      break;
    }
  }
}

void App::frame(float dt)
{
  if (doc_.poll())
    rebuild();

  FrameInput in = collectInput();
  if (!window_.isOpen())
    return;
  sf::Vector2u ws = window_.getSize();
  sf::Vector2f W(static_cast<float>(ws.x), static_cast<float>(ws.y));

  // ---- layout ----
  sf::FloatRect toolbar({0.f, 0.f}, {W.x, layout::Toolbar});
  sf::FloatRect status({0.f, W.y - layout::StatusBar}, {W.x, layout::StatusBar});
  float top = layout::Toolbar, bottom = W.y - layout::StatusBar;
  float left = view_.palette ? layout::Palette : 0.f;
  view_.dockWidth = std::clamp(view_.dockWidth, 300.f, std::max(300.f, W.x * 0.5f));
  float right = view_.dock ? W.x - view_.dockWidth : W.x;
  sf::FloatRect palette({0.f, top}, {left, bottom - top});
  sf::FloatRect dock({right, top}, {W.x - right, bottom - top});
  sf::FloatRect canvasRect({left, top}, {right - left, bottom - top});
  canvas_->setViewport(canvasRect, ws);

  ed_->showScan = (run_.running && run_.hz() < 40.f) || (!run_.running && sim_->isSteppingThrough());

  // ---- input routing ----
  const bool modal = view_.modbus || view_.help;
  const bool helpWasOpen = view_.help;
  ui_->interactive = !modal;
  window_.clear(Theme::Background);
  ui_->begin(window_, in, dt);
  if (!modal)
    globalShortcuts(in);

  // Dock splitter
  sf::FloatRect splitter({right - 3.f, top}, {6.f, bottom - top});
  if (view_.dock && !modal && ui_->takeClick(splitter))
    dockDrag_ = true;
  if (dockDrag_)
  {
    view_.dockWidth = W.x - in.mouse.x;
    if (!in.down[0])
      dockDrag_ = false;
  }

  bool overPanel = toolbar.contains(in.mouse) || status.contains(in.mouse) ||
                   (view_.palette && palette.contains(in.mouse)) || (view_.dock && dock.contains(in.mouse)) ||
                   (ui_->menuOpen() && ui_->menuRect().contains(in.mouse)) || splitter.contains(in.mouse);
  bool canvasMouse = !modal && !palette_.dragging() && canvasRect.contains(in.mouse) && !overPanel;
  if (!modal && (canvasMouse || canvas_->capturing()))
    canvas_->handle(in, canvasMouse);
  else if (!modal)
  {
    // Keyboard shortcuts and releases still reach the canvas; clicks don't.
    FrameInput passive = in;
    std::fill(std::begin(passive.pressed), std::end(passive.pressed), false);
    passive.wheel = passive.wheelH = 0.f;
    passive.doubleClick = false;
    canvas_->handle(passive, false);
  }

  // ---- simulation ----
  if (sim_)
  {
    bool step = run_.stepRequested;
    run_.stepRequested = false;
    sim_->update(dt, run_.hz(), run_.running, step);
    bool stepping = sim_->isSteppingThrough();
    if (wasStepping_ && !stepping && run_.running && !run_.repeat)
      run_.running = false;
    wasStepping_ = stepping;
    if (modbus_.isConnected())
      modbus_.sync(*sim_);
  }

  // ---- draw ----
  canvas_->draw(window_);
  if (view_.palette)
    palette_.draw(*ed_, *canvas_, palette);
  if (view_.dock)
  {
    sf::RectangleShape bg(dock.size);
    bg.setPosition(dock.position);
    bg.setFillColor(Theme::Panel);
    window_.draw(bg);
    sf::RectangleShape edge({1.f, dock.size.y});
    edge.setPosition(dock.position);
    edge.setFillColor(splitter.contains(in.mouse) || dockDrag_ ? Theme::Accent : Theme::Border);
    window_.draw(edge);

    // Inspector on top, code below. The inspector gets what it needs, up to
    // a share of the height the user can drag.
    float maxInspector = view_.code ? dock.size.y * 0.62f : dock.size.y;
    sf::FloatRect insp(dock.position, {dock.size.x, maxInspector});
    sf::View old = window_.getView();
    sf::View clip(insp);
    clip.setViewport({{insp.position.x / W.x, insp.position.y / W.y}, {insp.size.x / W.x, insp.size.y / W.y}});
    window_.setView(clip);
    float used = std::min(maxInspector, inspector_.draw(*ed_, *canvas_, insp));
    window_.setView(old);

    if (view_.code)
    {
      float codeTop = dock.position.y + std::max(used, 120.f);
      sf::RectangleShape sep({dock.size.x, 1.f});
      sep.setPosition({dock.position.x, codeTop});
      sep.setFillColor(Theme::Border);
      window_.draw(sep);
      code_.draw(*ed_, *canvas_, {{dock.position.x + 1.f, codeTop + 1.f}, {dock.size.x - 1.f, bottom - codeTop - 1.f}});
    }
  }
  toolbar_.draw(*ed_, run_, view_, *canvas_, toolbar);
  status_.draw(*ed_, run_, status);
  palette_.drawDrag(*ed_);

  if (view_.modbus)
  {
    ui_->interactive = true;
    modbusDialog_.draw(*ed_, view_, W);
  }
  else if (view_.help)
  {
    ui_->interactive = true;
    drawHelp(*ui_, view_, W, helpWasOpen);
  }
  ui_->end();
  window_.display();
}

int App::run()
{
  if (!ok_)
    return 1;
  bool modbusWasOpen = false;
  sf::Clock clock;
  while (window_.isOpen())
  {
    if (view_.modbus && !modbusWasOpen)
      modbusDialog_.open(modbus_);
    modbusWasOpen = view_.modbus;
    frame(std::min(clock.restart().asSeconds(), 0.1f));
  }
  return 0;
}
