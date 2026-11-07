#include "Widgets.hpp"
#include <algorithm>
#include <cmath>

// ---- drawing ------------------------------------------------------------------

namespace draw
{

sf::String utf8(const std::string &s)
{
  return sf::String::fromUtf8(s.begin(), s.end());
}

void roundRect(sf::RenderTarget &rt, sf::FloatRect r, float radius, sf::Color fill, sf::Color outline,
               float thickness)
{
  radius = std::min({radius, r.size.x / 2.f, r.size.y / 2.f});
  constexpr int seg = 6;
  sf::ConvexShape shape(4 * (seg + 1));
  const float pi = 3.14159265f;
  sf::Vector2f centers[4] = {
      {r.position.x + r.size.x - radius, r.position.y + radius},
      {r.position.x + r.size.x - radius, r.position.y + r.size.y - radius},
      {r.position.x + radius, r.position.y + r.size.y - radius},
      {r.position.x + radius, r.position.y + radius}};
  size_t idx = 0;
  for (int c = 0; c < 4; ++c)
  {
    float start = -pi / 2.f + c * pi / 2.f;
    for (int i = 0; i <= seg; ++i)
    {
      float a = start + (pi / 2.f) * i / seg;
      shape.setPoint(idx++, centers[c] + sf::Vector2f(std::cos(a), std::sin(a)) * radius);
    }
  }
  shape.setFillColor(fill);
  if (thickness > 0.f)
  {
    shape.setOutlineColor(outline);
    shape.setOutlineThickness(-thickness);
  }
  rt.draw(shape);
}

void line(sf::RenderTarget &rt, sf::Vector2f a, sf::Vector2f b, float width, sf::Color c)
{
  sf::Vector2f d = b - a;
  float len = std::sqrt(d.x * d.x + d.y * d.y);
  if (len < 0.001f)
    return;
  sf::Vector2f n(-d.y / len * width / 2.f, d.x / len * width / 2.f);
  sf::Vertex v[4] = {{a + n, c}, {b + n, c}, {a - n, c}, {b - n, c}};
  rt.draw(v, 4, sf::PrimitiveType::TriangleStrip);
}

void circle(sf::RenderTarget &rt, sf::Vector2f center, float radius, sf::Color fill, sf::Color outline,
            float thickness)
{
  sf::CircleShape c(radius, 20);
  c.setOrigin({radius, radius});
  c.setPosition(center);
  c.setFillColor(fill);
  if (thickness > 0.f)
  {
    c.setOutlineColor(outline);
    c.setOutlineThickness(thickness);
  }
  rt.draw(c);
}

void bezier(sf::RenderTarget &rt, sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3, float width,
            sf::Color c, float dash)
{
  constexpr int steps = 40;
  sf::Vector2f pts[steps + 1];
  for (int i = 0; i <= steps; ++i)
  {
    float t = static_cast<float>(i) / steps, u = 1.f - t;
    pts[i] = p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
  }
  if (dash <= 0.f)
  {
    sf::VertexArray strip(sf::PrimitiveType::TriangleStrip);
    for (int i = 0; i <= steps; ++i)
    {
      sf::Vector2f d = pts[std::min(i + 1, steps)] - pts[std::max(i - 1, 0)];
      float len = std::sqrt(d.x * d.x + d.y * d.y);
      if (len < 0.0001f)
        d = {1, 0}, len = 1;
      sf::Vector2f n(-d.y / len * width / 2.f, d.x / len * width / 2.f);
      strip.append({pts[i] + n, c});
      strip.append({pts[i] - n, c});
    }
    rt.draw(strip);
    return;
  }
  // Dashed: walk the polyline and emit segments alternately.
  float walked = 0.f;
  for (int i = 0; i < steps; ++i)
  {
    sf::Vector2f d = pts[i + 1] - pts[i];
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    bool on = static_cast<int>(walked / dash) % 2 == 0;
    if (on)
      line(rt, pts[i], pts[i + 1], width, c);
    walked += len;
  }
}

void text(sf::RenderTarget &rt, const sf::Font &font, const std::string &s, sf::Vector2f pos, unsigned size,
          sf::Color color, Align align, float scale)
{
  if (s.empty())
    return;
  unsigned px = std::max(1u, static_cast<unsigned>(std::lround(size * scale)));
  sf::Text t(font, utf8(s), px);
  t.setFillColor(color);
  float inv = static_cast<float>(size) / px;
  t.setScale({inv, inv});
  float w = t.getLocalBounds().size.x * inv;
  if (align == Align::Center)
    pos.x -= w / 2.f;
  else if (align == Align::Right)
    pos.x -= w;
  if (scale == 1.f)
    pos = {std::round(pos.x), std::round(pos.y)};
  t.setPosition(pos);
  rt.draw(t);
}

float textWidth(const sf::Font &font, const std::string &s, unsigned size)
{
  if (s.empty())
    return 0.f;
  sf::Text t(font, utf8(s), size);
  return t.getLocalBounds().size.x;
}

std::string fit(const sf::Font &font, const std::string &s, unsigned size, float maxWidth)
{
  if (textWidth(font, s, size) <= maxWidth)
    return s;
  sf::String u = utf8(s);
  while (!u.isEmpty())
  {
    u.erase(u.getSize() - 1);
    sf::String candidate = u + sf::String(U"…");
    sf::Text t(font, candidate, size);
    if (t.getLocalBounds().size.x <= maxWidth)
    {
      auto bytes = candidate.toUtf8();
      return std::string(bytes.begin(), bytes.end());
    }
  }
  return {};
}

}  // namespace draw

// ---- input ----------------------------------------------------------------------

bool FrameInput::key(sf::Keyboard::Key k) const
{
  return std::find(keys.begin(), keys.end(), k) != keys.end();
}

static void popUtf8(std::string &s)
{
  while (!s.empty())
  {
    unsigned char c = static_cast<unsigned char>(s.back());
    s.pop_back();
    if ((c & 0xC0) != 0x80)
      break;  // removed a lead byte (or ASCII)
  }
}

// ---- widgets --------------------------------------------------------------------

void Ui::begin(sf::RenderTarget &target, const FrameInput &input, float dt)
{
  rt = &target;
  in = input;
  time += dt;
  dt_ = dt;
  mouseTaken = false;
  tipSet_ = false;

  // Context menu sits above everything: it gets the click first.
  if (!menu_.empty())
  {
    if (in.key(sf::Keyboard::Key::Escape))
    {
      menu_.clear();
      in.keys.clear();
    }
    else if (in.pressed[0] || in.pressed[1])
    {
      if (menuRect_.contains(in.mouse))
      {
        float y = menuRect_.position.y + 4.f;
        for (auto &item : menu_)
        {
          float h = item.action ? 26.f : 22.f;
          if (item.action && item.enabled && in.mouse.y >= y && in.mouse.y < y + h)
          {
            auto action = item.action;
            menu_.clear();
            action();
            break;
          }
          y += h;
        }
      }
      else
        menu_.clear();
      mouseTaken = true;
    }
  }

  // A press outside the focused field commits it before anyone else reacts.
  if (!focus_.empty() && in.pressed[0] && !focusRect_.contains(in.mouse))
    clearFocus(true);

  // A field that was not drawn last frame has disappeared: commit it.
  if (!focus_.empty() && !focusSeen_)
    clearFocus(true);
  focusSeen_ = false;
}

void Ui::end()
{
  if (!menu_.empty())
  {
    float w = 180.f;
    // "KEY\tdescription" labels render as two aligned columns.
    float keyCol = 0.f;
    for (const auto &item : menu_)
      if (auto tab = item.label.find('\t'); tab != std::string::npos)
        keyCol = std::max(keyCol, draw::textWidth(bold, item.label.substr(0, tab), 13) + 14.f);
    for (const auto &item : menu_)
      w = std::max(w, draw::textWidth(font, item.label, 13) + keyCol + 40.f);
    float h = 8.f;
    for (const auto &item : menu_)
      h += item.action ? 26.f : 22.f;
    sf::Vector2f size = rt->getView().getSize();
    sf::Vector2f p = menuPos_;
    p.x = std::min(p.x, size.x - w - 4.f);
    p.y = std::min(p.y, size.y - h - 4.f);
    menuRect_ = {p, {w, h}};
    draw::roundRect(*rt, {p + sf::Vector2f(0, 3), {w, h}}, 8.f, sf::Color(0, 0, 0, 90));
    draw::roundRect(*rt, menuRect_, 8.f, Theme::PanelRaised, Theme::Border, 1.f);
    float y = p.y + 4.f;
    for (const auto &item : menu_)
    {
      if (!item.action)
      {
        draw::text(*rt, bold, item.label, {p.x + 12.f, y + 5.f}, 11, Theme::TextFaint);
        y += 22.f;
        continue;
      }
      sf::FloatRect row({p.x + 4.f, y}, {w - 8.f, 26.f});
      if (item.enabled && row.contains(in.mouse))
        draw::roundRect(*rt, row, 5.f, Theme::ButtonHover);
      if (item.checked)
        draw::circle(*rt, {p.x + 14.f, y + 13.f}, 3.f, Theme::Accent);
      sf::Color fg = item.enabled ? Theme::TextDefault : Theme::TextFaint;
      if (auto tab = item.label.find('\t'); tab != std::string::npos)
      {
        draw::text(*rt, bold, item.label.substr(0, tab), {p.x + 24.f, y + 5.f}, 13, fg);
        draw::text(*rt, font, item.label.substr(tab + 1), {p.x + 24.f + keyCol, y + 5.f}, 13, Theme::TextDim);
      }
      else
        draw::text(*rt, font, item.label, {p.x + 24.f, y + 5.f}, 13, fg);
      y += 26.f;
    }
  }

  if (tipSet_ && tipRect_.contains(in.mouse))
  {
    tipTimer_ += dt_;
    if (tipTimer_ > 0.45f && menu_.empty())
    {
      unsigned size = 12;
      float w = draw::textWidth(font, tip_, size) + 16.f;
      sf::Vector2f view = rt->getView().getSize();
      sf::Vector2f p = in.mouse + sf::Vector2f(14.f, 18.f);
      p.x = std::min(p.x, view.x - w - 4.f);
      p.y = std::min(p.y, view.y - 30.f);
      draw::roundRect(*rt, {p, {w, 24.f}}, 5.f, sf::Color(12, 13, 16, 235), Theme::Border, 1.f);
      draw::text(*rt, font, tip_, {p.x + 8.f, p.y + 4.f}, size, Theme::TextDefault);
    }
  }
  else
  {
    tipTimer_ = 0.f;
  }
}

bool Ui::hovering(sf::FloatRect r) const
{
  return interactive && menu_.empty() && r.contains(in.mouse);
}

bool Ui::takeClick(sf::FloatRect r, int button)
{
  if (!interactive || mouseTaken || !in.pressed[button] || !r.contains(in.mouse))
    return false;
  mouseTaken = true;
  return true;
}

bool Ui::button(sf::FloatRect r, const std::string &text, Style style, bool enabled)
{
  bool hot = enabled && hovering(r);
  sf::Color fill = Theme::ButtonDefault;
  sf::Color fg = Theme::TextDefault;
  switch (style)
  {
  case Style::Primary:
    fill = Theme::Accent;
    fg = sf::Color::White;
    break;
  case Style::Active:
    fill = Theme::ButtonRunning;
    fg = sf::Color::White;
    break;
  case Style::Danger:
    fill = Theme::ErrorColor;
    fg = sf::Color::White;
    break;
  case Style::Ghost:
    fill = sf::Color::Transparent;
    fg = Theme::TextDim;
    break;
  default:
    break;
  }
  if (hot)
  {
    fill = style == Style::Ghost ? Theme::ButtonDefault : Theme::mix(fill, sf::Color::White, 0.08f);
    if (style == Style::Ghost)
      fg = Theme::TextDefault;
  }
  if (!enabled)
  {
    fill = Theme::withAlpha(fill, fill.a / 2);
    fg = Theme::TextFaint;
  }
  draw::roundRect(*rt, r, 6.f, fill);
  unsigned size = 13;
  draw::text(*rt, font, draw::fit(font, text, size, r.size.x - 8.f),
             {r.position.x + r.size.x / 2.f, r.position.y + (r.size.y - 17.f) / 2.f}, size, fg, draw::Align::Center);
  return enabled && takeClick(r);
}

bool Ui::chip(sf::FloatRect r, const std::string &text, bool on, sf::Color onColor)
{
  bool hot = hovering(r);
  sf::Color fill = on ? Theme::withAlpha(onColor, 70) : (hot ? Theme::ButtonHover : Theme::Field);
  sf::Color border = on ? onColor : Theme::Border;
  draw::roundRect(*rt, r, 5.f, fill, border, 1.f);
  draw::text(*rt, font, text, {r.position.x + r.size.x / 2.f, r.position.y + (r.size.y - 16.f) / 2.f}, 12,
             on ? Theme::TextDefault : Theme::TextDim, draw::Align::Center);
  return takeClick(r);
}

bool Ui::slider(const std::string &id, sf::FloatRect r, float &v)
{
  if (takeClick(r))
    sliderActive_ = id;
  bool active = sliderActive_ == id;
  if (active)
  {
    if (!in.down[0])
      sliderActive_.clear();
    else
      v = std::clamp((in.mouse.x - r.position.x) / r.size.x, 0.f, 1.f);
  }
  float cy = r.position.y + r.size.y / 2.f;
  draw::roundRect(*rt, {{r.position.x, cy - 2.f}, {r.size.x, 4.f}}, 2.f, Theme::Field);
  draw::roundRect(*rt, {{r.position.x, cy - 2.f}, {r.size.x * v, 4.f}}, 2.f, Theme::Accent);
  draw::circle(*rt, {r.position.x + r.size.x * v, cy}, active || hovering(r) ? 7.f : 6.f, Theme::TextDefault);
  return active;
}

void Ui::clearFocus(bool commit)
{
  if (focus_.empty())
    return;
  auto fn = std::move(commit_);
  std::string value = buffer_, original = original_;
  focus_.clear();
  commit_ = nullptr;
  if (commit && fn && value != original)
    fn(value);
}

void Ui::textField(const std::string &id, sf::FloatRect r, const std::string &value,
                   std::function<void(const std::string &)> onCommit, const std::string &placeholder, bool enabled)
{
  bool focused = focus_ == id;
  const bool justFocused = !focused;

  if (enabled && (requestFocus_ == id || (!focused && takeClick(r))))
  {
    clearFocus(true);
    focus_ = id;
    buffer_ = original_ = value;
    selectAll_ = true;
    focused = true;
    requestFocus_.clear();
  }

  if (focused)
  {
    focusSeen_ = true;
    focusRect_ = r;
    commit_ = onCommit;  // refreshed each frame so it targets the current object
    if (in.pressed[0] && r.contains(in.mouse))
    {
      mouseTaken = true;
      if (!justFocused)
        selectAll_ = false;  // second click places the caret
    }
    if (!in.text.empty())
    {
      if (selectAll_)
        buffer_.clear();
      selectAll_ = false;
      buffer_ += in.text;
    }
    for (auto k : in.keys)
    {
      if (k == sf::Keyboard::Key::Backspace)
      {
        if (selectAll_)
          buffer_.clear();
        else if (in.ctrl)
          buffer_.clear();
        else
          popUtf8(buffer_);
        selectAll_ = false;
      }
      else if (k == sf::Keyboard::Key::Enter || k == sf::Keyboard::Key::Tab)
      {
        clearFocus(true);
        focused = false;
        break;
      }
      else if (k == sf::Keyboard::Key::Escape)
      {
        clearFocus(false);
        focused = false;
        break;
      }
      else if (k == sf::Keyboard::Key::End || k == sf::Keyboard::Key::Right || k == sf::Keyboard::Key::Left ||
               k == sf::Keyboard::Key::Home)
      {
        selectAll_ = false;
      }
    }
  }

  bool hot = enabled && hovering(r);
  sf::Color fill = focused ? Theme::Field : (hot ? Theme::FieldHover : Theme::Field);
  sf::Color border = focused ? Theme::Accent : (hot ? Theme::NodeBorder : Theme::Border);
  draw::roundRect(*rt, r, 5.f, fill, border, 1.f);

  const std::string &shown = focused ? buffer_ : value;
  float tx = r.position.x + 8.f, ty = r.position.y + (r.size.y - 17.f) / 2.f;
  unsigned size = 13;
  if (shown.empty() && !focused)
  {
    draw::text(*rt, font, placeholder, {tx, ty}, size, Theme::TextFaint);
  }
  else
  {
    std::string fitted = draw::fit(font, shown, size, r.size.x - 16.f);
    if (focused && selectAll_ && !shown.empty())
    {
      float w = draw::textWidth(font, fitted, size);
      draw::roundRect(*rt, {{tx - 2.f, ty}, {w + 4.f, 18.f}}, 3.f, Theme::withAlpha(Theme::Accent, 90));
    }
    draw::text(*rt, font, fitted, {tx, ty}, size, enabled ? Theme::TextDefault : Theme::TextDim);
  }
  if (focused && !selectAll_ && std::fmod(time, 1.f) < 0.6f)
  {
    float w = draw::textWidth(font, draw::fit(font, shown, size, r.size.x - 16.f), size);
    draw::line(*rt, {tx + w + 2.f, ty + 1.f}, {tx + w + 2.f, ty + 17.f}, 1.5f, Theme::TextDefault);
  }
}

void Ui::tooltip(sf::FloatRect r, const std::string &text)
{
  if (!hovering(r))
    return;
  if (tip_ != text || tipRect_ != r)
    tipTimer_ = 0.f;
  tip_ = text;
  tipRect_ = r;
  tipSet_ = true;
}

void Ui::showMenu(sf::Vector2f at, std::vector<MenuItem> items)
{
  menu_ = std::move(items);
  menuPos_ = at;
  // Real rect is computed when drawn; make sure this frame's click does not
  // immediately close it.
  menuRect_ = {at, {200.f, 400.f}};
  mouseTaken = true;
}

void Ui::label(sf::Vector2f pos, const std::string &s, sf::Color c, unsigned size, draw::Align a)
{
  draw::text(*rt, font, s, pos, size, c, a);
}

void Ui::sectionTitle(sf::Vector2f pos, const std::string &s)
{
  draw::text(*rt, bold, s, pos, 11, Theme::TextFaint);
}
