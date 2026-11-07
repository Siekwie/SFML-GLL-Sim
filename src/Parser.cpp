#include "Parser.hpp"
#include "TimeUtils.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

using gll::Arg;
using gll::Mod;
using gll::Statement;

namespace
{
int getOrCreateSignal(Program &prog, const std::string &symbol)
{
  auto it = prog.symbolToSignal.find(symbol);
  if (it != prog.symbolToSignal.end())
    return it->second;
  int id = prog.signalCount++;
  prog.symbolToSignal[symbol] = id;
  return id;
}

// Literals are analog signals named after their value; they are never driven.
int getOrCreateConstantSignal(Program &prog, uint64_t value)
{
  int id = getOrCreateSignal(prog, "_const_" + std::to_string(value));
  prog.analogSignals.insert(id);
  prog.constantSignalValues[id] = value;
  return id;
}

void addToken(Program &prog, int line, const gll::Span &span, const std::string &symbol)
{
  if (span.col0 >= 0)
    prog.tokens.push_back({line, span.col0, span.col1, symbol});
}

void compileDecl(Program &prog, const Statement &st, int line)
{
  using gll::DeclKind;
  bool analog = st.decl == DeclKind::AIn || st.decl == DeclKind::AOut;
  auto &names = st.decl == DeclKind::In    ? prog.inputNames
                : st.decl == DeclKind::Out ? prog.outputNames
                : st.decl == DeclKind::AIn ? prog.analogInputNames
                                           : prog.analogOutputNames;
  for (const auto &item : st.items)
  {
    int id = getOrCreateSignal(prog, item.name);
    if (analog)
      prog.analogSignals.insert(id);
    if (!item.alias.empty())
      prog.symbolToSignal[item.alias] = id;
    names.push_back(item.display());
    addToken(prog, line, item.nameSpan, item.name);
    if (!item.alias.empty())
      addToken(prog, line, item.aliasSpan, item.alias);
  }
}

// Inline NOT()/PS()/NS() become internal nodes placed before the gate, so they are
// evaluated (and edge detectors update their memory) on every scan, whatever the
// gate does with the result. Chains are built inside out: for NOT(PS(a)) the PS
// node comes first and the NOT node reads its output.
// Nodes are created in rounds by depth (innermost modifiers first); within a
// round all NOT()s come first, then PS()s, then NS()s, each in argument order.
// For single modifiers this is exactly the V1 order.
// Returns, per argument, the output signal of its outermost modifier node (-1 if none).
std::vector<int> compileModifiers(Program &prog, const Statement &st, int line)
{
  static const struct
  {
    Mod mod;
    Program::Node::Type type;
    const char *prefix;
  } kMods[] = {
    {Mod::Not, Program::Node::NOT_, "_not_"},
    {Mod::Ps, Program::Node::PS_, "_ps_"},
    {Mod::Ns, Program::Node::NS_, "_ns_"},
  };
  std::vector<int> outputs(st.args.size(), -1);
  size_t depth = 0;
  for (const Arg &a : st.args)
    depth = std::max(depth, a.mods.size());

  for (size_t round = 0; round < depth; ++round)
    for (const auto &m : kMods)
      for (size_t i = 0; i < st.args.size(); ++i)
      {
        const Arg &arg = st.args[i];
        if (arg.mods.size() <= round || arg.mods[arg.mods.size() - 1 - round] != m.mod)
          continue;
        Program::Node node;
        node.type = m.type;
        node.name = m.prefix + std::to_string(prog.nodes.size());
        if (round == 0)
        {
          node.inputs.push_back(getOrCreateSignal(prog, arg.symbol));
          addToken(prog, line, arg.span, arg.symbol);
        }
        else
          node.inputs.push_back(outputs[i]);
        outputs[i] = getOrCreateSignal(prog, node.name + "_out");
        node.outputs.push_back(outputs[i]);
        node.sourceLine = line;
        node.internal = true;
        prog.nodes.push_back(std::move(node));
      }
  return outputs;
}

void compileGate(Program &prog, const Statement &st, int line)
{
  const gll::NodeTypeInfo &info = gll::typeInfo(st.type);
  std::vector<int> modOut = compileModifiers(prog, st, line);

  Program::Node node;
  node.type = st.type;
  node.name = st.name;
  node.sourceLine = line;

  if (st.preset && info.presetTime)
    node.hardcodedPresetTime = parseTimeStringToFloat(*st.preset);
  if (st.preset && info.presetValue)
  {
    try
    {
      node.hardcodedPresetValue = std::stoi(*st.preset);
    }
    catch (...)
    {
    }
  }

  for (size_t i = 0; i < st.args.size(); ++i)
  {
    const Arg &a = st.args[i];
    if (!a.mods.empty())
    {
      node.inputs.push_back(modOut[i]);
      continue;
    }
    if (info.literals)
      if (auto v = gll::parseLiteral(a.quoted ? "\"" + a.symbol + "\"" : a.symbol))
      {
        node.inputs.push_back(getOrCreateConstantSignal(prog, *v));
        continue;
      }
    node.inputs.push_back(getOrCreateSignal(prog, a.symbol));
    addToken(prog, line, a.span, a.symbol);
  }

  for (size_t i = 0; i < st.outputs.size(); ++i)
  {
    int sig = getOrCreateSignal(prog, st.outputs[i].symbol);
    if (info.cvOutput && i == 1)
      node.cvOutputSignal = sig;
    else
      node.outputs.push_back(sig);
    addToken(prog, line, st.outputs[i].span, st.outputs[i].symbol);
  }
  prog.nodes.push_back(std::move(node));
}
}  // namespace

ParseResult compile(const gll::Script &script, Program &out)
{
  out = Program{};
  for (size_t i = 0; i < script.lines.size(); ++i)
  {
    const Statement &st = script.lines[i];
    int line = static_cast<int>(i);
    out.sourceLines.push_back(st.raw);
    if (st.kind == Statement::Kind::Decl)
      compileDecl(out, st, line);
    else if (st.kind == Statement::Kind::Gate)
      compileGate(out, st, line);
  }
  return {true, ""};
}

ParseResult parseFile(const std::string &path, Program &out)
{
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return {false, "Could not open file: " + path};
  std::stringstream buf;
  buf << file.rdbuf();

  gll::Script script;
  gll::ParseError error;
  if (!gll::parseScript(buf.str(), script, error))
    return {false, "Line " + std::to_string(error.line + 1) + ": " + error.message, error.line};
  return compile(script, out);
}
