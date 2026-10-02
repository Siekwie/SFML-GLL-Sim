#include "Edits.hpp"
#include <algorithm>

namespace gll::edits
{

static bool isGate(const Script &s, int line)
{
  return line >= 0 && line < static_cast<int>(s.lines.size()) && s.lines[line].kind == Statement::Kind::Gate;
}

static bool hasCv(const Statement &st)
{
  return typeInfo(st.type).cvOutput;
}

// `base` itself if free, otherwise base2, base3 ...
static std::string freshName(const Script &s, const std::string &base)
{
  if (!isNameTaken(s, base))
    return base;
  for (int i = 2;; ++i)
  {
    std::string candidate = base + std::to_string(i);
    if (!isNameTaken(s, candidate))
      return candidate;
  }
}

static bool isDeclared(const Script &s, const std::string &symbol)
{
  return findDecl(s, symbol).has_value();
}

// Number of gate inputs (any line) that read `symbol`.
static int readers(const Script &s, const std::string &symbol)
{
  int n = 0;
  for (const auto &st : s.lines)
    if (st.kind == Statement::Kind::Gate)
      for (const auto &a : st.args)
        if (!a.quoted && a.symbol == symbol)
          ++n;
  return n;
}

static bool producedByOtherThan(const Script &s, const std::string &symbol, int exceptLine)
{
  for (int i = 0; i < static_cast<int>(s.lines.size()); ++i)
  {
    if (i == exceptLine || s.lines[i].kind != Statement::Kind::Gate)
      continue;
    for (const auto &o : s.lines[i].outputs)
      if (o.symbol == symbol)
        return true;
  }
  return false;
}

int findGate(const Script &s, std::string_view name)
{
  for (int i = 0; i < static_cast<int>(s.lines.size()); ++i)
    if (s.lines[i].kind == Statement::Kind::Gate && s.lines[i].name == name)
      return i;
  return -1;
}

std::optional<DeclRef> findDecl(const Script &s, std::string_view symbol)
{
  for (int i = 0; i < static_cast<int>(s.lines.size()); ++i)
  {
    const auto &st = s.lines[i];
    if (st.kind != Statement::Kind::Decl)
      continue;
    for (int j = 0; j < static_cast<int>(st.items.size()); ++j)
      if (st.items[j].name == symbol || (!st.items[j].alias.empty() && st.items[j].alias == symbol))
        return DeclRef{i, j};
  }
  return std::nullopt;
}

bool isNameTaken(const Script &s, std::string_view symbol)
{
  if (symbol.empty())
    return false;
  for (const auto &st : s.lines)
  {
    if (st.kind == Statement::Kind::Decl)
    {
      for (const auto &it : st.items)
        if (it.name == symbol || it.alias == symbol)
          return true;
    }
    else if (st.kind == Statement::Kind::Gate)
    {
      if (st.name == symbol)
        return true;
      for (const auto &a : st.args)
        if (a.symbol == symbol)
          return true;
      for (const auto &o : st.outputs)
        if (o.symbol == symbol)
          return true;
    }
  }
  return false;
}

std::string uniqueName(const Script &s, std::string_view base)
{
  for (int i = 1;; ++i)
  {
    std::string candidate = std::string(base) + std::to_string(i);
    if (!isNameTaken(s, candidate))
      return candidate;
  }
}

// ---- gates -------------------------------------------------------------------

int addGate(Script &s, NodeType type, const std::string &name)
{
  const auto &info = typeInfo(type);
  Statement st;
  st.kind = Statement::Kind::Gate;
  st.type = type;
  st.name = name;
  size_t ports = info.variadic ? 2 : info.inputs.size();
  for (size_t i = 0; i < ports; ++i)
    st.args.push_back(Arg{kNotConnected});
  st.outputs.push_back(Output{freshName(s, name + "_Q")});
  refresh(st);
  s.lines.push_back(std::move(st));
  return static_cast<int>(s.lines.size()) - 1;
}

void deleteGate(Script &s, int line)
{
  if (!isGate(s, line))
    return;
  std::vector<std::string> produced;
  for (const auto &o : s.lines[line].outputs)
    produced.push_back(o.symbol);
  s.lines.erase(s.lines.begin() + line);

  for (const auto &sym : produced)
    if (!isDeclared(s, sym) && !producedByOtherThan(s, sym, -1))
      detachSymbol(s, sym);
}

void renameGate(Script &s, int line, const std::string &newName)
{
  if (!isGate(s, line))
    return;
  s.lines[line].name = newName;
  refresh(s.lines[line]);
}

void setPreset(Script &s, int line, const std::optional<std::string> &preset)
{
  if (!isGate(s, line))
    return;
  auto &st = s.lines[line];
  st.preset = preset;
  st.presetQuoted = true;
  refresh(st);
}

// ---- gate inputs ---------------------------------------------------------------

void connectInput(Script &s, int line, int arg, const std::string &symbol)
{
  if (!isGate(s, line) || arg < 0)
    return;
  auto &st = s.lines[line];
  while (static_cast<int>(st.args.size()) <= arg)
    st.args.push_back(Arg{kNotConnected});
  st.args[arg].symbol = symbol;
  st.args[arg].quoted = false;
  refresh(st);
}

void disconnectInput(Script &s, int line, int arg)
{
  if (!isGate(s, line))
    return;
  auto &st = s.lines[line];
  if (arg < 0 || arg >= static_cast<int>(st.args.size()))
    return;
  if (typeInfo(st.type).variadic && st.args.size() > 2)
  {
    st.args.erase(st.args.begin() + arg);
  }
  else
  {
    // Reset the modifier too: NOT(_nc) would read HIGH, not "unconnected".
    st.args[arg] = Arg{kNotConnected};
  }
  refresh(st);
}

void setInputMods(Script &s, int line, int arg, const std::vector<Mod> &mods)
{
  if (!isGate(s, line))
    return;
  auto &st = s.lines[line];
  if (arg < 0 || arg >= static_cast<int>(st.args.size()))
    return;
  st.args[arg].mods.clear();
  if (!st.args[arg].quoted)
    for (Mod m : mods)
      if (m != Mod::None)
        st.args[arg].mods.push_back(m);
  refresh(st);
}

void setInputMod(Script &s, int line, int arg, Mod mod)
{
  setInputMods(s, line, arg, mod == Mod::None ? std::vector<Mod>{} : std::vector<Mod>{mod});
}

void setInputLiteral(Script &s, int line, int arg, const std::string &literal)
{
  if (!isGate(s, line) || arg < 0)
    return;
  auto &st = s.lines[line];
  while (static_cast<int>(st.args.size()) <= arg)
    st.args.push_back(Arg{kNotConnected});
  st.args[arg].symbol = literal;
  st.args[arg].quoted = true;
  st.args[arg].mods.clear();
  refresh(st);
}

// ---- gate outputs ----------------------------------------------------------------

std::string ensureOutput(Script &s, int line, int port)
{
  if (!isGate(s, line))
    return {};
  auto &st = s.lines[line];
  if (st.outputs.empty())
    st.outputs.push_back(Output{freshName(s, st.name + "_Q")});
  if (port == 1 && hasCv(st) && st.outputs.size() < 2)
    st.outputs.push_back(Output{freshName(s, st.name + "_CV")});
  refresh(st);
  return st.outputs[(port == 1 && hasCv(st)) ? 1 : 0].symbol;
}

void setOutputs(Script &s, int line, const std::vector<std::string> &outputs)
{
  if (!isGate(s, line))
    return;
  auto &st = s.lines[line];
  st.outputs.clear();
  for (const auto &o : outputs)
    st.outputs.push_back(Output{o});
  refresh(st);
}

void undrive(Script &s, int line, const std::string &symbol)
{
  if (!isGate(s, line))
    return;
  auto &st = s.lines[line];
  const bool cv = hasCv(st);
  bool changed = false;
  for (int i = static_cast<int>(st.outputs.size()) - 1; i >= 0; --i)
  {
    if (st.outputs[i].symbol != symbol)
      continue;
    changed = true;
    if (cv && i == 1)
    {
      // The CV slot is positional: keep it, under a fresh internal name.
      st.outputs[i].symbol = freshName(s, st.name + "_CV");
    }
    else if (cv && i == 0 && st.outputs.size() >= 2)
    {
      // Same for a counter's Q slot, or the CV signal would move into it.
      if (st.outputs.size() > 2)
      {
        st.outputs[0] = st.outputs[2];
        st.outputs.erase(st.outputs.begin() + 2);
      }
      else
        st.outputs[0].symbol = freshName(s, st.name + "_Q");
    }
    else
    {
      st.outputs.erase(st.outputs.begin() + i);
    }
  }
  if (changed)
    refresh(st);
}

void driveTerminal(Script &s, int line, int port, const std::string &symbol)
{
  if (!isGate(s, line))
    return;
  for (int i = 0; i < static_cast<int>(s.lines.size()); ++i)
    if (s.lines[i].kind == Statement::Kind::Gate)
      undrive(s, i, symbol);

  auto &st = s.lines[line];
  const bool cv = hasCv(st);

  if (port == 1 && cv)
  {
    std::string old = ensureOutput(s, line, 1);
    // Readers of the old CV name keep their connection under the new name.
    if (!isDeclared(s, old))
      renameSymbol(s, old, symbol);
    st.outputs[1].symbol = symbol;
    refresh(st);
    return;
  }

  // Q group: index 0 and, for counters, indices >= 2.
  std::vector<int> qIdx;
  for (int i = 0; i < static_cast<int>(st.outputs.size()); ++i)
    if (!(cv && i == 1))
      qIdx.push_back(i);

  if (qIdx.size() == 1)
  {
    const std::string &only = st.outputs[qIdx[0]].symbol;
    if (readers(s, only) == 0 && !isDeclared(s, only))
    {
      // Replace an unused auto-generated name rather than piling up aliases.
      st.outputs[qIdx[0]].symbol = symbol;
      refresh(st);
      return;
    }
  }

  if (st.outputs.empty())
    st.outputs.push_back(Output{symbol});
  else if (cv && st.outputs.size() == 1)
  {
    st.outputs.push_back(Output{freshName(s, st.name + "_CV")});
    st.outputs.push_back(Output{symbol});
  }
  else
    st.outputs.push_back(Output{symbol});
  refresh(st);
}

// ---- declarations ------------------------------------------------------------------

DeclRef addDecl(Script &s, DeclKind kind, const std::string &name)
{
  int lastOfKind = -1, lastDecl = -1;
  for (int i = 0; i < static_cast<int>(s.lines.size()); ++i)
  {
    if (s.lines[i].kind != Statement::Kind::Decl)
      continue;
    lastDecl = i;
    if (s.lines[i].decl == kind)
      lastOfKind = i;
  }

  if (lastOfKind >= 0)
  {
    auto &st = s.lines[lastOfKind];
    st.items.push_back(DeclItem{name});
    refresh(st);
    return DeclRef{lastOfKind, static_cast<int>(st.items.size()) - 1};
  }

  int at = lastDecl + 1;
  if (lastDecl < 0)
  {
    // No declarations yet: go below the leading comment block.
    at = 0;
    while (at < static_cast<int>(s.lines.size()) && s.lines[at].kind == Statement::Kind::Comment)
      ++at;
  }
  Statement st;
  st.kind = Statement::Kind::Decl;
  st.decl = kind;
  st.items.push_back(DeclItem{name});
  refresh(st);
  s.lines.insert(s.lines.begin() + at, std::move(st));
  return DeclRef{at, 0};
}

void removeDecl(Script &s, DeclRef ref)
{
  if (ref.line < 0 || ref.line >= static_cast<int>(s.lines.size()))
    return;
  auto &st = s.lines[ref.line];
  if (st.kind != Statement::Kind::Decl || ref.item < 0 || ref.item >= static_cast<int>(st.items.size()))
    return;
  DeclItem item = st.items[ref.item];
  st.items.erase(st.items.begin() + ref.item);
  if (st.items.empty())
    s.lines.erase(s.lines.begin() + ref.line);
  else
    refresh(st);

  detachSymbol(s, item.name);
  if (!item.alias.empty())
    detachSymbol(s, item.alias);
}

void setDeclAlias(Script &s, DeclRef ref, const std::string &alias)
{
  if (ref.line < 0 || ref.line >= static_cast<int>(s.lines.size()))
    return;
  auto &st = s.lines[ref.line];
  if (st.kind != Statement::Kind::Decl || ref.item < 0 || ref.item >= static_cast<int>(st.items.size()))
    return;
  std::string old = st.items[ref.item].display();
  st.items[ref.item].alias = alias;
  refresh(st);
  // Wires referred to the old display name; keep them attached.
  const std::string &now = s.lines[ref.line].items[ref.item].display();
  if (old != now)
  {
    for (auto &line : s.lines)
    {
      if (line.kind != Statement::Kind::Gate)
        continue;
      bool changed = false;
      for (auto &a : line.args)
        if (!a.quoted && a.symbol == old)
          a.symbol = now, changed = true;
      for (auto &o : line.outputs)
        if (o.symbol == old)
          o.symbol = now, changed = true;
      if (changed)
        refresh(line);
    }
  }
}

// ---- signals ---------------------------------------------------------------------

void renameSymbol(Script &s, const std::string &from, const std::string &to)
{
  if (from == to)
    return;
  for (auto &st : s.lines)
  {
    bool changed = false;
    if (st.kind == Statement::Kind::Decl)
    {
      for (auto &it : st.items)
      {
        if (it.name == from)
          it.name = to, changed = true;
        if (!it.alias.empty() && it.alias == from)
          it.alias = to, changed = true;
      }
    }
    else if (st.kind == Statement::Kind::Gate)
    {
      for (auto &a : st.args)
        if (!a.quoted && a.symbol == from)
          a.symbol = to, changed = true;
      for (auto &o : st.outputs)
        if (o.symbol == from)
          o.symbol = to, changed = true;
    }
    if (changed)
      refresh(st);
  }
}

void detachSymbol(Script &s, const std::string &symbol)
{
  for (int i = 0; i < static_cast<int>(s.lines.size()); ++i)
  {
    auto &st = s.lines[i];
    if (st.kind != Statement::Kind::Gate)
      continue;
    for (int a = static_cast<int>(st.args.size()) - 1; a >= 0; --a)
      if (!st.args[a].quoted && st.args[a].symbol == symbol)
        disconnectInput(s, i, a);
    undrive(s, i, symbol);
  }
}

}  // namespace gll::edits
