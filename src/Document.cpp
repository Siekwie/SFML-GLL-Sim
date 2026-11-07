#include "Document.hpp"
#include <fstream>
#include <sstream>
#include <system_error>

static constexpr size_t kMaxUndo = 200;

static bool readFile(const std::filesystem::path &p, std::string &out)
{
  std::ifstream f(p, std::ios::binary);
  if (!f.is_open())
    return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static std::vector<std::string> splitLines(const std::string &text)
{
  std::vector<std::string> lines;
  std::string cur;
  for (char c : text)
  {
    if (c == '\n')
    {
      if (!cur.empty() && cur.back() == '\r')
        cur.pop_back();
      lines.push_back(cur);
      cur.clear();
    }
    else
      cur += c;
  }
  if (!cur.empty())
    lines.push_back(cur);
  return lines;
}

bool Document::open(const std::filesystem::path &path, std::string &error)
{
  path_ = path;
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec))
  {
    // Starting a new circuit: create the file.
    std::ofstream f(path_, std::ios::binary);
    if (!f.is_open())
    {
      error = "Could not create file: " + path_.string();
      return false;
    }
  }

  std::string text;
  if (!readFile(path_, text))
  {
    error = "Could not open file: " + path_.string();
    return false;
  }
  mtime_ = std::filesystem::last_write_time(path_, ec);
  loadLayout();
  if (!load(text, true))
  {
    // Keep going with an empty program so the error is shown in the editor.
    program_ = std::make_shared<Program>();
    ++revision_;
  }
  return true;
}

bool Document::load(const std::string &text, bool fromDisk)
{
  eol_ = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  trailingNewline_ = text.empty() || text.back() == '\n';
  diskLines_ = splitLines(text);

  gll::Script script;
  gll::ParseError perr;
  if (!gll::parseScript(text, script, perr))
  {
    error_ = ParseResult{false, perr.message, perr.line};
    return false;
  }

  auto prog = std::make_shared<Program>();
  ParseResult res = compile(script, *prog);
  if (!res.ok)
  {
    error_ = res;
    return false;
  }

  if (fromDisk && error_.has_value())
    status_ = "Error fixed — reloaded";
  error_.reset();
  script_ = std::move(script);
  program_ = std::move(prog);
  ++revision_;
  return true;
}

bool Document::poll()
{
  std::error_code ec;
  auto t = std::filesystem::last_write_time(path_, ec);
  if (ec || t == mtime_)
    return false;
  mtime_ = t;

  std::string text;
  if (!readFile(path_, text))
    return false;
  if (text == currentText() && !error_)
    return false;  // touched but unchanged

  bool hadError = error_.has_value();
  bool ok = load(text, true);
  if (ok)
  {
    // History refers to text that no longer exists on disk.
    undo_.clear();
    redo_.clear();
    if (!hadError)
      status_ = "Reloaded external changes";
  }
  return true;
}

std::string Document::currentText() const
{
  std::string text = gll::toText(script_, eol_);
  if (trailingNewline_ && !script_.lines.empty())
    text += eol_;
  return text;
}

bool Document::writeFile(const std::string &text)
{
  std::ofstream f(path_, std::ios::binary | std::ios::trunc);
  if (!f.is_open())
  {
    status_ = "Could not write " + path_.string();
    return false;
  }
  f << text;
  f.close();
  std::error_code ec;
  mtime_ = std::filesystem::last_write_time(path_, ec);
  return true;
}

bool Document::edit(const std::string &label, const std::function<bool(gll::Script &)> &fn)
{
  if (!editable())
  {
    status_ = "Fix the error in the file first — the graph is read-only meanwhile";
    return false;
  }

  gll::Script copy = script_;
  if (!fn(copy))
    return false;

  std::string before = currentText();
  std::string text = gll::toText(copy, eol_);
  if (trailingNewline_ && !copy.lines.empty())
    text += eol_;
  if (text == before)
    return false;

  Snapshot snap{before, positions_, label};
  if (!load(text, false))
  {
    // An edit produced something that does not compile: refuse it.
    std::string why = error_ ? error_->msg : std::string("unknown error");
    load(before, false);
    status_ = "Edit rejected: " + why;
    return false;
  }

  undo_.push_back(std::move(snap));
  if (undo_.size() > kMaxUndo)
    undo_.erase(undo_.begin());
  redo_.clear();
  writeFile(text);
  saveLayout();
  status_ = label;
  return true;
}

void Document::beginLayoutChange(const std::string &label)
{
  undo_.push_back(Snapshot{currentText(), positions_, label});
  if (undo_.size() > kMaxUndo)
    undo_.erase(undo_.begin());
  redo_.clear();
}

void Document::restore(const Snapshot &snap)
{
  positions_ = snap.positions;
  if (snap.text != currentText())
  {
    load(snap.text, false);
    writeFile(snap.text);
  }
  saveLayout();
}

bool Document::undo()
{
  if (undo_.empty() || !editable())
    return false;
  Snapshot snap = std::move(undo_.back());
  undo_.pop_back();
  redo_.push_back(Snapshot{currentText(), positions_, snap.label});
  restore(snap);
  status_ = "Undo: " + snap.label;
  return true;
}

bool Document::redo()
{
  if (redo_.empty() || !editable())
    return false;
  Snapshot snap = std::move(redo_.back());
  redo_.pop_back();
  undo_.push_back(Snapshot{currentText(), positions_, snap.label});
  restore(snap);
  status_ = "Redo: " + snap.label;
  return true;
}

// ---- layout sidecar -----------------------------------------------------------
// One "x y key" line per node. GLL names contain no whitespace, so a plain
// whitespace separated format is enough.

void Document::loadLayout()
{
  positions_.clear();
  std::ifstream f(path_.string() + ".layout");
  std::string line;
  while (std::getline(f, line))
  {
    std::istringstream ss(line);
    float x, y;
    std::string key;
    if (ss >> x >> y >> key)
      positions_[key] = {x, y};
  }
}

void Document::saveLayout()
{
  std::ofstream f(path_.string() + ".layout", std::ios::trunc);
  if (!f.is_open())
    return;
  for (const auto &[key, p] : positions_)
    f << static_cast<int>(p.x) << ' ' << static_cast<int>(p.y) << ' ' << key << '\n';
}
