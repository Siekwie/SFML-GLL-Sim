// Headless tests for the structural editing operations (src/Edits.cpp).
// Plain asserts, no framework. Linked into the gll_tests target; see test_main.cpp.
//
// Every test builds a Script from a small GLL text, applies edits and compares
// gll::toText() with the exact expected text. expectText() additionally checks
// that the result compiles and that the structured model of every statement is
// still consistent with its raw text (re-parsing the text gives the same thing).
#include "Edits.hpp"
#include "Gll.hpp"
#include "Parser.hpp"
#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

namespace ed = gll::edits;
using gll::DeclKind;
using gll::Mod;
using gll::NodeType;
using gll::Script;
using gll::Statement;

namespace
{
int g_checks = 0, g_failures = 0;

#define ECHECK(cond)                                                             \
  do                                                                             \
  {                                                                              \
    ++g_checks;                                                                  \
    if (!(cond))                                                                 \
    {                                                                            \
      ++g_failures;                                                              \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                            \
  } while (0)

#define EXPECT_TEXT(script, expected) expectText(__LINE__, (script), (expected))
#define EXPECT_STR(actual, expected) expectStr(__LINE__, (actual), (expected))

std::string escaped(const std::string &s)
{
  std::string out;
  for (char c : s)
  {
    if (c == '\n')
      out += "\\n\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else
      out += c;
  }
  return out;
}

void expectStr(int lineNo, const std::string &actual, const std::string &expected)
{
  ++g_checks;
  if (actual == expected)
    return;
  ++g_failures;
  std::printf("FAIL %s:%d  string mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n----------------\n", __FILE__, lineNo,
              escaped(expected).c_str(), escaped(actual).c_str());
}

// Lines joined with '\n' (no trailing newline, like toText()).
std::string J(std::initializer_list<const char *> lines)
{
  std::string out;
  bool first = true;
  for (const char *l : lines)
  {
    if (!first)
      out += '\n';
    first = false;
    out += l;
  }
  return out;
}

Script P(const std::string &text)
{
  Script s;
  gll::ParseError err;
  ++g_checks;
  if (!gll::parseScript(text, s, err))
  {
    ++g_failures;
    std::printf("FAIL %s: test script does not parse: %s\n", __FILE__, err.message.c_str());
  }
  return s;
}

Script S(std::initializer_list<const char *> lines)
{
  return P(J(lines));
}

bool compiles(const Script &s, Program &prog)
{
  return compile(s, prog).ok;
}

// toText() == expected, the result compiles, and the per-statement model matches its text.
void expectText(int lineNo, const Script &s, const std::string &expected)
{
  std::string actual = gll::toText(s);
  expectStr(lineNo, actual, expected);

  Program prog;
  ++g_checks;
  if (!compiles(s, prog))
  {
    ++g_failures;
    std::printf("FAIL %s:%d  result does not compile\n", __FILE__, lineNo);
  }

  Script again;
  gll::ParseError err;
  ++g_checks;
  if (!gll::parseScript(actual, again, err) || again.lines.size() != s.lines.size())
  {
    ++g_failures;
    std::printf("FAIL %s:%d  result does not re-parse to the same number of lines\n", __FILE__, lineNo);
    return;
  }
  for (size_t i = 0; i < s.lines.size(); ++i)
  {
    ++g_checks;
    bool same = s.lines[i].kind == again.lines[i].kind && s.lines[i].raw == again.lines[i].raw &&
                gll::formatStatement(s.lines[i]) == gll::formatStatement(again.lines[i]);
    if (!same)
    {
      ++g_failures;
      std::printf("FAIL %s:%d  statement %zu model is inconsistent with its raw text: '%s'\n", __FILE__, lineNo, i,
                  s.lines[i].raw.c_str());
    }
  }
}

// Every line of `after` except the `touched` ones is byte-identical to `before`.
void expectOthersSame(int lineNo, const Script &before, const Script &after, const std::set<size_t> &touched)
{
  ++g_checks;
  if (before.lines.size() != after.lines.size())
  {
    ++g_failures;
    std::printf("FAIL %s:%d  line count changed %zu -> %zu\n", __FILE__, lineNo, before.lines.size(), after.lines.size());
    return;
  }
  for (size_t i = 0; i < before.lines.size(); ++i)
  {
    if (touched.count(i))
      continue;
    ++g_checks;
    if (before.lines[i].raw != after.lines[i].raw)
    {
      ++g_failures;
      std::printf("FAIL %s:%d  untouched line %zu changed\n  before: '%s'\n  after:  '%s'\n", __FILE__, lineNo, i,
                  escaped(before.lines[i].raw).c_str(), escaped(after.lines[i].raw).c_str());
    }
  }
}
#define EXPECT_OTHERS_SAME(before, after, ...) expectOthersSame(__LINE__, (before), (after), std::set<size_t>{__VA_ARGS__})

// ------------------------------------------------------------------ lookups, names

void testLookupsAndNames()
{
  Script s = S({"# and9 zzz", "IN a(Start), b", "OUT q", "AND and1(a, NOT(b)) -> and1_Q", "OR or1(and1_Q, \"7\") -> q"});

  ECHECK(ed::findGate(s, "and1") == 3);
  ECHECK(ed::findGate(s, "or1") == 4);
  ECHECK(ed::findGate(s, "a") == -1);  // a signal, not a gate
  ECHECK(ed::findGate(s, "and9") == -1);

  auto byName = ed::findDecl(s, "a");
  auto byAlias = ed::findDecl(s, "Start");
  ECHECK(byName && byName->line == 1 && byName->item == 0);
  ECHECK(byAlias && byAlias->line == 1 && byAlias->item == 0);
  auto b = ed::findDecl(s, "b");
  ECHECK(b && b->line == 1 && b->item == 1);
  auto q = ed::findDecl(s, "q");
  ECHECK(q && q->line == 2 && q->item == 0);
  ECHECK(!ed::findDecl(s, "and1_Q"));
  ECHECK(!ed::findDecl(s, "zzz"));

  ECHECK(ed::isNameTaken(s, "a"));       // decl name
  ECHECK(ed::isNameTaken(s, "Start"));   // decl alias
  ECHECK(ed::isNameTaken(s, "and1"));    // gate name
  ECHECK(ed::isNameTaken(s, "and1_Q"));  // output (and arg)
  ECHECK(ed::isNameTaken(s, "q"));
  ECHECK(!ed::isNameTaken(s, "zzz"));    // only mentioned in a comment
  ECHECK(!ed::isNameTaken(s, "and9"));

  ECHECK(ed::uniqueName(s, "and") == "and2");  // and1 is a gate name
  ECHECK(ed::uniqueName(s, "or") == "or2");
  ECHECK(ed::uniqueName(s, "xor") == "xor1");
  ECHECK(ed::uniqueName(s, "and1_Q") == "and1_Q1");

  Script gates = S({"IN a", "AND and1(a, a) -> and1_Q", "AND and2(a, a) -> x"});
  ECHECK(ed::uniqueName(gates, "and") == "and3");
  Script onlyOutput = S({"IN a", "NOT n(a) -> or1"});
  ECHECK(ed::uniqueName(onlyOutput, "or") == "or2");
  Script onlyAlias = S({"IN a(or1)"});
  ECHECK(ed::uniqueName(onlyAlias, "or") == "or2");
  Script onlyArg = S({"NOT n(or1) -> y"});
  ECHECK(ed::uniqueName(onlyArg, "or") == "or2");
  Script empty;
  ECHECK(ed::uniqueName(empty, "and") == "and1");
  ECHECK(!ed::isNameTaken(empty, "a"));
}

// ------------------------------------------------------------------ addGate

void testAddGate()
{
  const auto base = [] { return S({"IN a, b", "OUT q"}); };
  {
    Script s = base();
    ECHECK(ed::addGate(s, NodeType::AND_, "and1") == 2);
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND and1(_nc, _nc) -> and1_Q"}));
    ECHECK(s.lines[2].kind == Statement::Kind::Gate && s.lines[2].args.size() == 2 && s.lines[2].outputs.size() == 1);
    ECHECK(s.lines[2].args[0].symbol == gll::kNotConnected);
    Program prog;
    ECHECK(compiles(s, prog) && prog.nodes.size() == 1 && prog.nodes[0].inputs.size() == 2);
  }
  {
    Script s = base();
    ed::addGate(s, NodeType::OR_, "or1");
    ed::addGate(s, NodeType::XOR_, "x1");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "OR or1(_nc, _nc) -> or1_Q", "XOR x1(_nc, _nc) -> x1_Q"}));
  }
  {
    Script s = base();
    ed::addGate(s, NodeType::SR_, "sr1");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "SR sr1(_nc, _nc) -> sr1_Q"}));
  }
  {
    Script s = base();
    ed::addGate(s, NodeType::RS_, "rs1");
    ed::addGate(s, NodeType::NOT_, "n1");
    ed::addGate(s, NodeType::PS_, "p1");
    ed::addGate(s, NodeType::LT_, "lt1");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "RS rs1(_nc, _nc) -> rs1_Q", "NOT n1(_nc) -> n1_Q", "PS p1(_nc) -> p1_Q",
                      "LT lt1(_nc, _nc) -> lt1_Q"}));
  }
  {
    Script s = base();
    ed::addGate(s, NodeType::TON_, "t1");
    ed::addGate(s, NodeType::TOF_, "t2");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "TON t1(_nc) -> t1_Q", "TOF t2(_nc) -> t2_Q"}));
    ECHECK(!s.lines[2].preset);
  }
  {
    Script s = base();
    ed::addGate(s, NodeType::CTU_, "c1");
    ed::addGate(s, NodeType::CTD_, "c2");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "CTU c1(_nc, _nc) -> c1_Q", "CTD c2(_nc, _nc) -> c2_Q"}));
    ECHECK(s.lines[2].outputs.size() == 1);  // no CV output until asked for
  }
  {
    Script s = base();
    ed::addGate(s, NodeType::BTN, "b1");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "BTN b1() -> b1_Q"}));
    ECHECK(s.lines[2].args.empty());
  }
  {
    // The output signal never collides with an existing name.
    Script s = S({"IN a", "NOT n(a) -> x_Q"});
    ed::addGate(s, NodeType::AND_, "x");
    EXPECT_TEXT(s, J({"IN a", "NOT n(a) -> x_Q", "AND x(_nc, _nc) -> x_Q2"}));
  }
  {
    // Name chosen with uniqueName(), as the editor does.
    Script s = S({"IN a"});
    for (int i = 0; i < 3; ++i)
      ed::addGate(s, NodeType::AND_, ed::uniqueName(s, "and"));
    EXPECT_TEXT(s, J({"IN a", "AND and1(_nc, _nc) -> and1_Q", "AND and2(_nc, _nc) -> and2_Q", "AND and3(_nc, _nc) -> and3_Q"}));
  }
  {
    Script s;
    ECHECK(ed::addGate(s, NodeType::AND_, "g") == 0);
    EXPECT_TEXT(s, "AND g(_nc, _nc) -> g_Q");
  }
}

// ------------------------------------------------------------------ gate inputs

void testConnectInput()
{
  {
    Script s = S({"IN a, b", "OUT q", "AND g(_nc, _nc) -> q"});
    ed::connectInput(s, 2, 0, "a");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND g(a, _nc) -> q"}));
    ed::connectInput(s, 2, 1, "b");
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND g(a, b) -> q"}));
    ed::connectInput(s, 2, 1, "a");  // replace a connected port
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND g(a, a) -> q"}));
  }
  {
    // Variadic spare: arg == args.size() appends.
    Script s = S({"AND g(a, b) -> q"});
    ed::connectInput(s, 0, 2, "c");
    EXPECT_TEXT(s, "AND g(a, b, c) -> q");
    ed::connectInput(s, 0, 3, "d");
    EXPECT_TEXT(s, "AND g(a, b, c, d) -> q");
    ECHECK(s.lines[0].args.size() == 4);
  }
  {
    Script s = S({"OR g(a, b) -> q", "XOR h(a, b) ->"});
    ed::connectInput(s, 0, 2, "c");
    ed::connectInput(s, 1, 2, "c");
    EXPECT_TEXT(s, J({"OR g(a, b, c) -> q", "XOR h(a, b, c) ->"}));
  }
  {
    // The modifier of the port survives re-connecting it.
    Script s = S({"AND g(NOT(a), PS(b), NS(c), d) -> q"});
    ed::connectInput(s, 0, 0, "w");
    ed::connectInput(s, 0, 1, "x");
    ed::connectInput(s, 0, 2, "y");
    ed::connectInput(s, 0, 3, "z");
    EXPECT_TEXT(s, "AND g(NOT(w), PS(x), NS(y), z) -> q");
    ECHECK(s.lines[0].args[0].mod == Mod::Not && s.lines[0].args[1].mod == Mod::Ps && s.lines[0].args[2].mod == Mod::Ns);
  }
  {
    // Fixed arity gate with preset: preset stays, only signal ports count.
    Script s = S({"TON t(\"500ms\", _nc) -> d", "TON u(500ms, _nc) -> e", "CTU c(\"3\", _nc, _nc) -> f, g"});
    ed::connectInput(s, 0, 0, "start");
    ed::connectInput(s, 1, 0, "start");
    ed::connectInput(s, 2, 1, "rst");
    ed::connectInput(s, 2, 0, "up");
    EXPECT_TEXT(s, J({"TON t(\"500ms\", start) -> d", "TON u(500ms, start) -> e", "CTU c(\"3\", up, rst) -> f, g"}));
    ECHECK(s.lines[0].preset == "500ms" && s.lines[0].args.size() == 1);
  }
  {
    // Connecting a comparator port that held a literal turns it into a signal.
    Script s = S({"LT c(x, \"5\") -> y"});
    ed::connectInput(s, 0, 1, "z");
    EXPECT_TEXT(s, "LT c(x, z) -> y");
    ECHECK(!s.lines[0].args[1].quoted);
  }
  {
    // Indentation of the edited line is kept; other lines are not touched.
    Script s = S({"  # c", "  AND g(_nc, _nc) -> q", "\tOR h(a,   b)->r"});
    ed::connectInput(s, 1, 0, "a");
    EXPECT_TEXT(s, J({"  # c", "  AND g(a, _nc) -> q", "\tOR h(a,   b)->r"}));
  }
  {
    // Invalid targets are ignored.
    Script s = S({"# comment", "", "AND g(a, b) -> q"});
    std::string before = gll::toText(s);
    ed::connectInput(s, 0, 0, "x");
    ed::connectInput(s, 1, 0, "x");
    ed::connectInput(s, 3, 0, "x");
    ed::connectInput(s, -1, 0, "x");
    ed::connectInput(s, 2, -1, "x");
    EXPECT_STR(gll::toText(s), before);
  }
}

void testDisconnectInput()
{
  {
    // Variadic with more than two inputs drops the port.
    Script s = S({"AND g(a, b, c) -> q"});
    ed::disconnectInput(s, 0, 1);
    EXPECT_TEXT(s, "AND g(a, c) -> q");
  }
  {
    Script s = S({"OR g(a, NOT(b), PS(c), d) -> q"});
    ed::disconnectInput(s, 0, 1);
    EXPECT_TEXT(s, "OR g(a, PS(c), d) -> q");
    ed::disconnectInput(s, 0, 2);
    EXPECT_TEXT(s, "OR g(a, PS(c)) -> q");
    ed::disconnectInput(s, 0, 0);  // exactly two left: the port stays
    EXPECT_TEXT(s, "OR g(_nc, PS(c)) -> q");
  }
  {
    // Variadic with exactly two inputs keeps both ports.
    Script s = S({"AND g(a, b) -> q", "XOR h(a, b) -> r"});
    ed::disconnectInput(s, 0, 0);
    ed::disconnectInput(s, 1, 1);
    EXPECT_TEXT(s, J({"AND g(_nc, b) -> q", "XOR h(a, _nc) -> r"}));
    ed::disconnectInput(s, 0, 1);
    EXPECT_TEXT(s, J({"AND g(_nc, _nc) -> q", "XOR h(a, _nc) -> r"}));
  }
  {
    // Fixed arity: always becomes _nc, preset is kept.
    Script s = S({"SR s(a, b) -> q", "NOT n(a) -> r", "TON t(\"1s\", a) -> d", "CTU c(\"3\", up, rst) -> f, g", "LT l(x, \"5\") -> y"});
    ed::disconnectInput(s, 0, 1);
    ed::disconnectInput(s, 1, 0);
    ed::disconnectInput(s, 2, 0);
    ed::disconnectInput(s, 3, 1);
    ed::disconnectInput(s, 4, 1);  // literal port
    EXPECT_TEXT(s, J({"SR s(a, _nc) -> q", "NOT n(_nc) -> r", "TON t(\"1s\", _nc) -> d", "CTU c(\"3\", up, _nc) -> f, g",
                      "LT l(x, _nc) -> y"}));
    ECHECK(!s.lines[4].args[1].quoted);
  }
  {
    // Out of range / not a gate: ignored.
    Script s = S({"# c", "SR s(a, b) -> q"});
    std::string before = gll::toText(s);
    ed::disconnectInput(s, 0, 0);
    ed::disconnectInput(s, 1, 2);
    ed::disconnectInput(s, 1, -1);
    ed::disconnectInput(s, 5, 0);
    EXPECT_STR(gll::toText(s), before);
  }
}

void testSetInputMod()
{
  Script s = S({"IN a, b", "  AND g(a, b) -> q"});
  ed::setInputMod(s, 1, 0, Mod::Not);
  EXPECT_TEXT(s, J({"IN a, b", "  AND g(NOT(a), b) -> q"}));
  ed::setInputMod(s, 1, 1, Mod::Ps);
  EXPECT_TEXT(s, J({"IN a, b", "  AND g(NOT(a), PS(b)) -> q"}));
  ed::setInputMod(s, 1, 0, Mod::Ns);  // replace
  EXPECT_TEXT(s, J({"IN a, b", "  AND g(NS(a), PS(b)) -> q"}));
  ed::setInputMod(s, 1, 0, Mod::None);
  ed::setInputMod(s, 1, 1, Mod::None);
  EXPECT_TEXT(s, J({"IN a, b", "  AND g(a, b) -> q"}));
  ECHECK(s.lines[1].args[0].mod == Mod::None);

  // The compiled program gets an internal NOT node.
  ed::setInputMod(s, 1, 0, Mod::Not);
  Program prog;
  ECHECK(compiles(s, prog) && prog.nodes.size() == 2 && prog.nodes[0].internal);

  // Quoted literals cannot carry a modifier.
  Script lit = S({"LT c(x, \"5\") -> y"});
  ed::setInputMod(lit, 0, 1, Mod::Not);
  EXPECT_TEXT(lit, "LT c(x, \"5\") -> y");
  ed::setInputMod(lit, 0, 0, Mod::Not);
  EXPECT_TEXT(lit, "LT c(NOT(x), \"5\") -> y");

  // Invalid targets are ignored.
  std::string before = gll::toText(s);
  ed::setInputMod(s, 0, 0, Mod::Not);
  ed::setInputMod(s, 1, 2, Mod::Not);
  ed::setInputMod(s, 1, -1, Mod::Not);
  ed::setInputMod(s, 9, 0, Mod::Not);
  EXPECT_STR(gll::toText(s), before);
}

void testSetInputLiteral()
{
  Script s = S({"IN x, y", "LT c(x, y) -> o", "GT d(NOT(x), y) -> p", "EQ e(x, \"1\") -> r"});
  ed::setInputLiteral(s, 1, 1, "0x80");
  EXPECT_TEXT(s, J({"IN x, y", "LT c(x, \"0x80\") -> o", "GT d(NOT(x), y) -> p", "EQ e(x, \"1\") -> r"}));
  ECHECK(s.lines[1].args[1].quoted && s.lines[1].args[1].symbol == "0x80");

  ed::setInputLiteral(s, 2, 0, "5");  // the modifier is reset
  EXPECT_TEXT(s, J({"IN x, y", "LT c(x, \"0x80\") -> o", "GT d(\"5\", y) -> p", "EQ e(x, \"1\") -> r"}));
  ECHECK(s.lines[2].args[0].mod == Mod::None);

  ed::setInputLiteral(s, 3, 1, "128");  // replace a literal
  EXPECT_TEXT(s, J({"IN x, y", "LT c(x, \"0x80\") -> o", "GT d(\"5\", y) -> p", "EQ e(x, \"128\") -> r"}));

  Program prog;
  ECHECK(compiles(s, prog));
  ECHECK(prog.constantSignalValues.size() == 2);  // 128, 5 (0x80 and 128 share one constant)
  ECHECK(prog.symbolToSignal.count("_const_128") == 1);

  Script bad = S({"# c", "LT c(x, y) -> o"});
  std::string before = gll::toText(bad);
  ed::setInputLiteral(bad, 0, 0, "1");
  ed::setInputLiteral(bad, 1, -1, "1");
  EXPECT_STR(gll::toText(bad), before);
}

// ------------------------------------------------------------------ presets

void testSetPreset()
{
  {
    Script s = S({"IN a", "TON t(a) -> q"});
    ed::setPreset(s, 1, "500ms");
    EXPECT_TEXT(s, J({"IN a", "TON t(\"500ms\", a) -> q"}));
    ECHECK(s.lines[1].preset == "500ms" && s.lines[1].args.size() == 1);
    Program prog;
    ECHECK(compiles(s, prog) && prog.nodes[0].hardcodedPresetTime > 0.49f && prog.nodes[0].hardcodedPresetTime < 0.51f);

    ed::setPreset(s, 1, "2s");
    EXPECT_TEXT(s, J({"IN a", "TON t(\"2s\", a) -> q"}));
    ed::setPreset(s, 1, std::nullopt);
    EXPECT_TEXT(s, J({"IN a", "TON t(a) -> q"}));
    ECHECK(!s.lines[1].preset);
  }
  {
    // TOF and an unquoted preset in the source: written quoted afterwards.
    Script s = S({"TOF f(500ms, a) -> q"});
    ed::setPreset(s, 0, "1s");
    EXPECT_TEXT(s, "TOF f(\"1s\", a) -> q");
    ed::setPreset(s, 0, std::nullopt);
    EXPECT_TEXT(s, "TOF f(a) -> q");
  }
  {
    Script s = S({"CTU c(up, rst) -> q, cv"});
    ed::setPreset(s, 0, "10");
    EXPECT_TEXT(s, "CTU c(\"10\", up, rst) -> q, cv");
    Program prog;
    ECHECK(compiles(s, prog) && prog.nodes[0].hardcodedPresetValue == 10);
    ed::setPreset(s, 0, "5");
    EXPECT_TEXT(s, "CTU c(\"5\", up, rst) -> q, cv");
    ed::setPreset(s, 0, std::nullopt);
    EXPECT_TEXT(s, "CTU c(up, rst) -> q, cv");
    ECHECK(compiles(s, prog) && prog.nodes[0].hardcodedPresetValue == -1);
  }
  {
    Script s = S({"CTD c(\"3\", dn, ld) -> q"});
    ed::setPreset(s, 0, "7");
    EXPECT_TEXT(s, "CTD c(\"7\", dn, ld) -> q");
    ed::setPreset(s, 0, std::nullopt);
    EXPECT_TEXT(s, "CTD c(dn, ld) -> q");
  }
  {
    Script s = S({"# c", "TON t(a) -> q"});
    ed::setPreset(s, 0, "1s");
    ed::setPreset(s, 7, "1s");
    EXPECT_TEXT(s, J({"# c", "TON t(a) -> q"}));
  }
}

// ------------------------------------------------------------------ gate names / outputs

void testRenameGateAndSetOutputs()
{
  Script s = S({"  AND g(a, b) -> q", "# c"});
  ed::renameGate(s, 0, "h");
  EXPECT_TEXT(s, J({"  AND h(a, b) -> q", "# c"}));
  ed::renameGate(s, 1, "zz");  // not a gate
  EXPECT_TEXT(s, J({"  AND h(a, b) -> q", "# c"}));
  ed::setOutputs(s, 0, {"x", "y"});
  EXPECT_TEXT(s, J({"  AND h(a, b) -> x, y", "# c"}));
  ed::setOutputs(s, 0, {});
  EXPECT_TEXT(s, J({"  AND h(a, b) ->", "# c"}));
  ed::setOutputs(s, 0, {"q"});
  EXPECT_TEXT(s, J({"  AND h(a, b) -> q", "# c"}));
}

void testEnsureOutput()
{
  {
    Script s = S({"CTU c(a, b) ->"});
    ECHECK(ed::ensureOutput(s, 0, 0) == "c_Q");
    EXPECT_TEXT(s, "CTU c(a, b) -> c_Q");
    ECHECK(ed::ensureOutput(s, 0, 0) == "c_Q");  // idempotent
    EXPECT_TEXT(s, "CTU c(a, b) -> c_Q");
    ECHECK(ed::ensureOutput(s, 0, 1) == "c_CV");
    EXPECT_TEXT(s, "CTU c(a, b) -> c_Q, c_CV");
    ECHECK(ed::ensureOutput(s, 0, 1) == "c_CV");
    EXPECT_TEXT(s, "CTU c(a, b) -> c_Q, c_CV");
    Program prog;
    ECHECK(compiles(s, prog) && prog.nodes[0].cvOutputSignal >= 0);
  }
  {
    // CV requested while only a named Q exists: Q is kept, CV appended.
    Script s = S({"CTD c(\"3\", a, b) -> done"});
    ECHECK(ed::ensureOutput(s, 0, 1) == "c_CV");
    EXPECT_TEXT(s, "CTD c(\"3\", a, b) -> done, c_CV");
    ECHECK(ed::ensureOutput(s, 0, 0) == "done");
  }
  {
    // CV requested on a counter without any output: both slots appear.
    Script s = S({"CTU c(a, b) ->"});
    ECHECK(ed::ensureOutput(s, 0, 1) == "c_CV");
    EXPECT_TEXT(s, "CTU c(a, b) -> c_Q, c_CV");
  }
  {
    // Existing outputs are returned untouched.
    Script s = S({"CTU c(a, b) -> done, level"});
    ECHECK(ed::ensureOutput(s, 0, 0) == "done");
    ECHECK(ed::ensureOutput(s, 0, 1) == "level");
    EXPECT_TEXT(s, "CTU c(a, b) -> done, level");
  }
  {
    // Fresh names avoid collisions.
    Script s = S({"CTU c(a, b) -> done", "NOT n(c_CV) -> x"});
    ECHECK(ed::ensureOutput(s, 0, 1) == "c_CV2");
    EXPECT_TEXT(s, J({"CTU c(a, b) -> done, c_CV2", "NOT n(c_CV) -> x"}));
  }
  {
    // Gates without a CV port have only Q, whatever port is asked for.
    Script s = S({"AND g(a, b) ->", "AND h(a, b) -> y"});
    ECHECK(ed::ensureOutput(s, 0, 1) == "g_Q");
    EXPECT_TEXT(s, J({"AND g(a, b) -> g_Q", "AND h(a, b) -> y"}));
    ECHECK(ed::ensureOutput(s, 1, 1) == "y");
    ECHECK(ed::ensureOutput(s, 1, 0) == "y");
    EXPECT_TEXT(s, J({"AND g(a, b) -> g_Q", "AND h(a, b) -> y"}));
  }
  {
    Script s = S({"# c"});
    ECHECK(ed::ensureOutput(s, 0, 0).empty());
    ECHECK(ed::ensureOutput(s, 4, 0).empty());
  }
}

// ------------------------------------------------------------------ driveTerminal / undrive

void testDriveTerminal()
{
  {
    // An unused auto-generated output name is replaced.
    Script s = S({"IN x, y", "OUT o", "AND a(x, y) -> a_Q"});
    ed::driveTerminal(s, 2, 0, "o");
    EXPECT_TEXT(s, J({"IN x, y", "OUT o", "AND a(x, y) -> o"}));
  }
  {
    // The old Q is read elsewhere: keep it and append the terminal.
    Script s = S({"IN x, y", "OUT o", "AND a(x, y) -> a_Q", "NOT n(a_Q) -> n_Q"});
    ed::driveTerminal(s, 2, 0, "o");
    EXPECT_TEXT(s, J({"IN x, y", "OUT o", "AND a(x, y) -> a_Q, o", "NOT n(a_Q) -> n_Q"}));
  }
  {
    // The old Q is itself a declared signal: keep it.
    Script s = S({"OUT o, p", "AND a(x, y) -> p"});
    ed::driveTerminal(s, 1, 0, "o");
    EXPECT_TEXT(s, J({"OUT o, p", "AND a(x, y) -> p, o"}));
  }
  {
    // A gate without outputs just gets the terminal.
    Script s = S({"OUT o", "AND a(x, y) ->"});
    ed::driveTerminal(s, 1, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) -> o"}));
  }
  {
    // Single driver: another gate stops writing the terminal.
    Script s = S({"OUT o", "AND a(x, y) -> o", "OR b(x, y) -> b_Q"});
    ed::driveTerminal(s, 2, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) ->", "OR b(x, y) -> o"}));
  }
  {
    Script s = S({"OUT o", "AND a(x, y) -> a_Q, o", "NOT n(a_Q) -> n_Q", "OR b(x, y) -> b_Q"});
    ed::driveTerminal(s, 3, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) -> a_Q", "NOT n(a_Q) -> n_Q", "OR b(x, y) -> o"}));
  }
  {
    // Driving the same terminal again is a no-op.
    Script s = S({"OUT o", "AND a(x, y) -> a_Q, o", "NOT n(a_Q) -> n_Q"});
    ed::driveTerminal(s, 1, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) -> a_Q, o", "NOT n(a_Q) -> n_Q"}));
    Script t = S({"OUT o", "AND a(x, y) -> o"});
    ed::driveTerminal(t, 1, 0, "o");
    EXPECT_TEXT(t, J({"OUT o", "AND a(x, y) -> o"}));
  }
  {
    // Counter Q port: the CV slot is positional and never used as Q.
    Script s = S({"OUT o", "CTU c(u, r) -> c_Q, c_CV"});
    ed::driveTerminal(s, 1, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> o, c_CV"}));
    Script t = S({"OUT o", "CTU c(u, r) -> c_Q, c_CV", "NOT n(c_Q) -> n_Q"});
    ed::driveTerminal(t, 1, 0, "o");
    EXPECT_TEXT(t, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV, o", "NOT n(c_Q) -> n_Q"}));
    Script u = S({"OUT o", "CTU c(u, r) -> c_Q", "NOT n(c_Q) -> n_Q"});
    ed::driveTerminal(u, 1, 0, "o");
    EXPECT_TEXT(u, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV, o", "NOT n(c_Q) -> n_Q"}));
  }
  {
    // Counter CV port: readers of the old CV name follow the rename.
    Script s = S({"OUT o", "CTU c(u, r) -> c_Q, c_CV", "NOT n(c_CV) -> n_Q"});
    ed::driveTerminal(s, 1, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> c_Q, o", "NOT n(o) -> n_Q"}));
    Program prog;
    ECHECK(compiles(s, prog));
    ECHECK(prog.nodes.size() == 2 && prog.nodes[0].cvOutputSignal == prog.symbolToSignal["o"]);
    ECHECK(prog.nodes[1].inputs.size() == 1 && prog.nodes[1].inputs[0] == prog.symbolToSignal["o"]);
  }
  {
    // CV slot does not exist yet: it is created, then renamed.
    Script s = S({"OUT o", "CTU c(u, r) -> done"});
    ed::driveTerminal(s, 1, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> done, o"}));
    Script t = S({"OUT o", "CTU c(u, r) ->"});
    ed::driveTerminal(t, 1, 1, "o");
    EXPECT_TEXT(t, J({"OUT o", "CTU c(u, r) -> c_Q, o"}));
  }
  {
    // CV slot held a declared signal: that one is left alone, only the slot moves.
    Script s = S({"OUT o, p", "CTU c(u, r) -> c_Q, p", "NOT n(p) -> n_Q"});
    ed::driveTerminal(s, 1, 1, "o");
    EXPECT_TEXT(s, J({"OUT o, p", "CTU c(u, r) -> c_Q, o", "NOT n(p) -> n_Q"}));
  }
  {
    // Moving a counter's CV terminal from another counter.
    Script s = S({"OUT o", "CTU c(u, r) -> c_Q, o", "CTD d(u, r) -> d_Q, d_CV"});
    ed::driveTerminal(s, 2, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV", "CTD d(u, r) -> d_Q, o"}));
  }
  {
    Script s = S({"OUT o", "# c"});
    ed::driveTerminal(s, 1, 0, "o");
    ed::driveTerminal(s, 9, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "# c"}));
  }
}

void testUndrive()
{
  {
    Script s = S({"OUT o", "AND a(x, y) -> a_Q, o"});
    ed::undrive(s, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) -> a_Q"}));
    ed::undrive(s, 1, "a_Q");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) ->"}));
    ed::undrive(s, 1, "nothing");
    EXPECT_TEXT(s, J({"OUT o", "AND a(x, y) ->"}));
  }
  {
    // Counter CV slot is kept under a fresh name.
    Script s = S({"OUT o", "CTU c(u, r) -> c_Q, o"});
    ed::undrive(s, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV"}));
    Program prog;
    ECHECK(compiles(s, prog) && prog.nodes[0].cvOutputSignal >= 0);
  }
  {
    // ... and the fresh name avoids existing ones.
    Script s = S({"OUT o", "CTU c(u, r) -> c_Q, o", "NOT n(c_CV) -> n_Q"});
    ed::undrive(s, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV2", "NOT n(c_CV) -> n_Q"}));
  }
  {
    // Erasing a counter's Q (slot 0) shifts the CV slot into the Q position.
    // Expected: the Q slot is kept under a fresh name, like the CV slot is. See testCounterQSlot().
  }
  {
    // Same symbol on two slots of one gate.
    Script s = S({"AND a(x, y) -> o, o, k"});
    ed::undrive(s, 0, "o");
    EXPECT_TEXT(s, "AND a(x, y) -> k");
  }
}

// A counter's output list is positional: [Q, CV, extra Q ...]. Removing the Q signal must keep
// the Q slot (under a fresh name) so the CV signal does not become the counter's Q output.
void testCounterQSlot()
{
  {
    Script s = S({"OUT o", "CTU c(u, r) -> o, c_CV"});
    ed::undrive(s, 1, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV"}));  // actual: "CTU c(u, r) -> c_CV"
  }
  {
    Script s = S({"OUT o", "CTU c(u, r) -> o, c_CV", "NOT n(c_CV) -> n_Q"});
    ed::removeDecl(s, {0, 0});
    EXPECT_TEXT(s, J({"CTU c(u, r) -> c_Q, c_CV", "NOT n(c_CV) -> n_Q"}));
  }
  {
    // Moving the OUT to another gate must not turn the CV of the old counter into its Q.
    Script s = S({"OUT o", "CTU c(u, r) -> o, c_CV", "AND a(x, y) -> a_Q"});
    ed::driveTerminal(s, 2, 0, "o");
    EXPECT_TEXT(s, J({"OUT o", "CTU c(u, r) -> c_Q, c_CV", "AND a(x, y) -> o"}));
  }
}

// ------------------------------------------------------------------ deleteGate

void testDeleteGate()
{
  {
    // Readers of the gate's private output are disconnected.
    Script s = S({"IN a, b", "OUT o", "AND g(a, b) -> g_Q", "NOT n(g_Q) -> o"});
    ed::deleteGate(s, 2);
    EXPECT_TEXT(s, J({"IN a, b", "OUT o", "NOT n(_nc) -> o"}));
  }
  {
    // Variadic readers drop the port (or keep two ports as _nc).
    Script s = S({"IN a, b, c", "OUT o, p", "AND g(a, b) -> g_Q", "OR r(g_Q, b, c) -> o", "XOR x(g_Q, c) -> p", "SR s(g_Q, a) -> s_Q"});
    ed::deleteGate(s, 2);
    EXPECT_TEXT(s, J({"IN a, b, c", "OUT o, p", "OR r(b, c) -> o", "XOR x(_nc, c) -> p", "SR s(_nc, a) -> s_Q"}));
  }
  {
    // Readers of a declared OUT name are kept.
    Script s = S({"IN a, b", "OUT o", "AND g(a, b) -> o", "NOT n(o) -> n_Q"});
    ed::deleteGate(s, 2);
    EXPECT_TEXT(s, J({"IN a, b", "OUT o", "NOT n(o) -> n_Q"}));
  }
  {
    // ... also when the OUT is referenced through its alias.
    Script s = S({"IN a, b", "OUT o(Lamp)", "AND g(a, b) -> Lamp", "NOT n(Lamp) -> n_Q"});
    ed::deleteGate(s, 2);
    EXPECT_TEXT(s, J({"IN a, b", "OUT o(Lamp)", "NOT n(Lamp) -> n_Q"}));
  }
  {
    // Mixed: private output detached, declared output kept.
    Script s = S({"IN a, b", "OUT o", "AND g(a, b) -> g_Q, o", "NOT n(g_Q) -> n_Q", "NOT m(o) -> m_Q"});
    ed::deleteGate(s, 2);
    EXPECT_TEXT(s, J({"IN a, b", "OUT o", "NOT n(_nc) -> n_Q", "NOT m(o) -> m_Q"}));
  }
  {
    // A signal still produced by another gate stays connected.
    Script s = S({"IN a", "AND g(a, a) -> s", "OR h(a, a) -> s", "NOT n(s) -> n_Q"});
    ed::deleteGate(s, 1);
    EXPECT_TEXT(s, J({"IN a", "OR h(a, a) -> s", "NOT n(s) -> n_Q"}));
  }
  {
    // Counter: both private outputs are detached.
    Script s = S({"IN u, r", "CTU c(u, r) -> c_Q, c_CV", "NOT n(c_Q) -> n_Q", "LT l(c_CV, \"5\") -> l_Q"});
    ed::deleteGate(s, 1);
    EXPECT_TEXT(s, J({"IN u, r", "NOT n(_nc) -> n_Q", "LT l(_nc, \"5\") -> l_Q"}));
  }
  {
    // Gates that only read its inputs are unaffected; modifiers on dropped ports go with them.
    Script s = S({"IN a", "NOT g(a) -> g_Q", "AND h(NOT(g_Q), a, PS(a)) -> h_Q"});
    ed::deleteGate(s, 1);
    EXPECT_TEXT(s, J({"IN a", "AND h(a, PS(a)) -> h_Q"}));
  }
  {
    Script s = S({"IN a", "NOT g(a) -> g_Q"});
    ed::deleteGate(s, 0);  // a declaration: ignored
    ed::deleteGate(s, 5);
    ed::deleteGate(s, -1);
    EXPECT_TEXT(s, J({"IN a", "NOT g(a) -> g_Q"}));
    ed::deleteGate(s, 1);
    EXPECT_TEXT(s, "IN a");
  }
}

// ------------------------------------------------------------------ declarations

void testAddDecl()
{
  {
    // Into the existing declaration line of that kind.
    Script s = S({"IN a", "OUT q", "AND g(a, a) -> q"});
    auto r = ed::addDecl(s, DeclKind::In, "b");
    ECHECK(r.line == 0 && r.item == 1);
    EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND g(a, a) -> q"}));
    r = ed::addDecl(s, DeclKind::Out, "r");
    ECHECK(r.line == 1 && r.item == 1);
    EXPECT_TEXT(s, J({"IN a, b", "OUT q, r", "AND g(a, a) -> q"}));
  }
  {
    // The last declaration line of that kind is used; indent is kept.
    Script s = S({"IN a", "OUT q", "  IN b", "AND g(a, b) -> q"});
    auto r = ed::addDecl(s, DeclKind::In, "c");
    ECHECK(r.line == 2 && r.item == 1);
    EXPECT_TEXT(s, J({"IN a", "OUT q", "  IN b, c", "AND g(a, b) -> q"}));
  }
  {
    // No line of that kind: new line after the existing declarations.
    Script s = S({"IN a", "AND g(a, a) -> q"});
    auto r = ed::addDecl(s, DeclKind::Out, "q");
    ECHECK(r.line == 1 && r.item == 0);
    EXPECT_TEXT(s, J({"IN a", "OUT q", "AND g(a, a) -> q"}));
  }
  {
    Script s = S({"IN a", "OUT q", "", "AND g(a, a) -> q"});
    auto r = ed::addDecl(s, DeclKind::AIn, "x");
    ECHECK(r.line == 2 && r.item == 0);
    EXPECT_TEXT(s, J({"IN a", "OUT q", "AIN x", "", "AND g(a, a) -> q"}));
    r = ed::addDecl(s, DeclKind::AOut, "y");
    ECHECK(r.line == 3 && r.item == 0);
    EXPECT_TEXT(s, J({"IN a", "OUT q", "AIN x", "AOUT y", "", "AND g(a, a) -> q"}));
  }
  {
    // Declarations interleaved with gates: after the last declaration line.
    Script s = S({"IN a", "AND g(a, a) -> q", "OUT q", "# tail"});
    auto r = ed::addDecl(s, DeclKind::AIn, "x");
    ECHECK(r.line == 3);
    EXPECT_TEXT(s, J({"IN a", "AND g(a, a) -> q", "OUT q", "AIN x", "# tail"}));
  }
  {
    // No declarations: below the leading comment block.
    Script s = S({"# title", "# more", "", "AND g(a, a) -> q"});
    auto r = ed::addDecl(s, DeclKind::In, "a");
    ECHECK(r.line == 2 && r.item == 0);
    EXPECT_TEXT(s, J({"# title", "# more", "IN a", "", "AND g(a, a) -> q"}));
    r = ed::addDecl(s, DeclKind::In, "b");
    ECHECK(r.line == 2 && r.item == 1);
    EXPECT_TEXT(s, J({"# title", "# more", "IN a, b", "", "AND g(a, a) -> q"}));
  }
  {
    // No declarations, no comments: first line.
    Script s = S({"AND g(a, a) -> q"});
    auto r = ed::addDecl(s, DeclKind::In, "a");
    ECHECK(r.line == 0 && r.item == 0);
    EXPECT_TEXT(s, J({"IN a", "AND g(a, a) -> q"}));
  }
  {
    // Only comments.
    Script s = S({"# only", "# comments"});
    auto r = ed::addDecl(s, DeclKind::In, "a");
    ECHECK(r.line == 2);
    EXPECT_TEXT(s, J({"# only", "# comments", "IN a"}));
  }
  {
    Script s;
    auto r = ed::addDecl(s, DeclKind::In, "a");
    ECHECK(r.line == 0 && r.item == 0);
    EXPECT_TEXT(s, "IN a");
  }
}

void testRemoveDecl()
{
  {
    // Item removed from a multi-item line; readers are disconnected.
    Script s = S({"IN a, b", "OUT q", "AND g(a, b) -> q"});
    ed::removeDecl(s, {0, 0});
    EXPECT_TEXT(s, J({"IN b", "OUT q", "AND g(_nc, b) -> q"}));
  }
  {
    // Last item of a line: the whole line goes away. A driver stops writing it.
    Script s = S({"IN a, b", "OUT q", "AND g(a, b) -> q"});
    ed::removeDecl(s, {1, 0});
    EXPECT_TEXT(s, J({"IN a, b", "AND g(a, b) ->"}));
  }
  {
    // Aliased item: readers by name and by alias are both detached.
    Script s = S({"IN a(Start), b", "AND g(Start, b) -> g_Q", "AND h(a, b) -> h_Q", "OR k(Start, a, b) -> k_Q"});
    ed::removeDecl(s, {0, 0});
    EXPECT_TEXT(s, J({"IN b", "AND g(_nc, b) -> g_Q", "AND h(_nc, b) -> h_Q", "OR k(_nc, b) -> k_Q"}));
  }
  {
    // Item in the middle keeps the others (and their aliases).
    Script s = S({"IN a, b(B), c(C)", "AND g(a, c) -> g_Q"});
    ed::removeDecl(s, {0, 1});
    EXPECT_TEXT(s, J({"IN a, c(C)", "AND g(a, c) -> g_Q"}));
  }
  {
    // Variadic readers shrink.
    Script s = S({"IN a, b, c", "AND g(a, b, c) -> g_Q", "OR h(a, b, c, NOT(a)) -> h_Q"});
    ed::removeDecl(s, {0, 0});
    EXPECT_TEXT(s, J({"IN b, c", "AND g(b, c) -> g_Q", "OR h(b, c) -> h_Q"}));
  }
  {
    // A counter's CV slot driving a removed OUT keeps its position.
    Script s = S({"IN u, r", "OUT o", "CTU c(u, r) -> c_Q, o"});
    ed::removeDecl(s, {1, 0});
    EXPECT_TEXT(s, J({"IN u, r", "CTU c(u, r) -> c_Q, c_CV"}));
  }
  {
    // Quoted literals with the same text are not signals.
    Script s = S({"IN a", "AIN x", "LT l(x, \"a\") -> l_Q"});
    ed::removeDecl(s, {0, 0});
    EXPECT_TEXT(s, J({"AIN x", "LT l(x, \"a\") -> l_Q"}));
  }
  {
    Script s = S({"IN a", "# c", "AND g(a, a) -> q"});
    std::string before = gll::toText(s);
    ed::removeDecl(s, {1, 0});   // a comment
    ed::removeDecl(s, {0, 3});   // no such item
    ed::removeDecl(s, {0, -1});
    ed::removeDecl(s, {-1, 0});
    ed::removeDecl(s, {17, 0});
    EXPECT_STR(gll::toText(s), before);
  }
}

void testSetDeclAlias()
{
  {
    // New alias: wires that used the display name follow it and stay attached.
    Script s = S({"IN a, b", "OUT q", "AND g(a, b) -> q", "NOT n(a) -> n_Q"});
    ed::setDeclAlias(s, {0, 0}, "Start");
    EXPECT_TEXT(s, J({"IN a(Start), b", "OUT q", "AND g(Start, b) -> q", "NOT n(Start) -> n_Q"}));
    Program prog;
    ECHECK(compiles(s, prog));
    ECHECK(prog.symbolToSignal["Start"] == prog.symbolToSignal["a"]);
    ECHECK(prog.nodes[0].inputs[0] == prog.symbolToSignal["a"] && prog.nodes[1].inputs[0] == prog.symbolToSignal["a"]);
    ECHECK(prog.inputNames.size() == 2 && prog.inputNames[0] == "Start");
  }
  {
    // Change alias.
    Script s = S({"IN a(Start), b", "AND g(Start, b) -> g_Q"});
    ed::setDeclAlias(s, {0, 0}, "Go");
    EXPECT_TEXT(s, J({"IN a(Go), b", "AND g(Go, b) -> g_Q"}));
  }
  {
    // Remove alias: readers fall back to the name.
    Script s = S({"IN a(Start), b", "AND g(Start, b) -> g_Q"});
    ed::setDeclAlias(s, {0, 0}, "");
    EXPECT_TEXT(s, J({"IN a, b", "AND g(a, b) -> g_Q"}));
    Program prog;
    ECHECK(compiles(s, prog) && prog.nodes[0].inputs[0] == prog.symbolToSignal["a"]);
  }
  {
    // Outputs of gates driving an OUT follow too; modifiers survive.
    Script s = S({"OUT o", "AND g(x, NOT(y)) -> o", "NOT n(NOT(o)) -> n_Q"});
    ed::setDeclAlias(s, {0, 0}, "Lamp");
    EXPECT_TEXT(s, J({"OUT o(Lamp)", "AND g(x, NOT(y)) -> Lamp", "NOT n(NOT(Lamp)) -> n_Q"}));
  }
  {
    // Readers that used the real name keep it when an alias exists. Unchanged display name: nothing moves.
    Script s = S({"IN a(Start), b", "AND  g( a ,b )  ->  g_Q"});
    ed::setDeclAlias(s, {0, 0}, "Go");
    EXPECT_TEXT(s, J({"IN a(Go), b", "AND  g( a ,b )  ->  g_Q"}));
    ed::setDeclAlias(s, {0, 0}, "Go");
    EXPECT_TEXT(s, J({"IN a(Go), b", "AND  g( a ,b )  ->  g_Q"}));
  }
  {
    // Quoted literals are not renamed.
    Script s = S({"AIN a", "LT l(a, \"a\") -> l_Q"});
    ed::setDeclAlias(s, {0, 0}, "Z");
    EXPECT_TEXT(s, J({"AIN a(Z)", "LT l(Z, \"a\") -> l_Q"}));
  }
  {
    Script s = S({"IN a", "# c"});
    ed::setDeclAlias(s, {1, 0}, "x");
    ed::setDeclAlias(s, {0, 5}, "x");
    ed::setDeclAlias(s, {9, 0}, "x");
    EXPECT_TEXT(s, J({"IN a", "# c"}));
  }
}

// ------------------------------------------------------------------ signals

void testRenameSymbol()
{
  {
    Script s = S({"IN a(A), b", "OUT q", "AND g(a, NOT(b)) -> g_Q", "LT h(g_Q, \"a\") -> q", "OR k(PS(a), A, b) -> k_Q"});
    ed::renameSymbol(s, "a", "z");
    EXPECT_TEXT(s, J({"IN z(A), b", "OUT q", "AND g(z, NOT(b)) -> g_Q", "LT h(g_Q, \"a\") -> q", "OR k(PS(z), A, b) -> k_Q"}));
    ed::renameSymbol(s, "A", "Alias");  // an alias
    EXPECT_TEXT(s, J({"IN z(Alias), b", "OUT q", "AND g(z, NOT(b)) -> g_Q", "LT h(g_Q, \"a\") -> q", "OR k(PS(z), Alias, b) -> k_Q"}));
    ed::renameSymbol(s, "g_Q", "m");  // output and its reader
    EXPECT_TEXT(s, J({"IN z(Alias), b", "OUT q", "AND g(z, NOT(b)) -> m", "LT h(m, \"a\") -> q", "OR k(PS(z), Alias, b) -> k_Q"}));
    ed::renameSymbol(s, "q", "out1");  // decl and output
    EXPECT_TEXT(s, J({"IN z(Alias), b", "OUT out1", "AND g(z, NOT(b)) -> m", "LT h(m, \"a\") -> out1", "OR k(PS(z), Alias, b) -> k_Q"}));
    Program prog;
    ECHECK(compiles(s, prog) && prog.outputNames.size() == 1 && prog.outputNames[0] == "out1");
  }
  {
    // Quoted literals are never touched, even when they spell the symbol.
    Script s = S({"AIN x", "LT l(x, \"5\") -> l_Q", "EQ e(\"5\", x) -> e_Q"});
    ed::renameSymbol(s, "5", "five");
    EXPECT_TEXT(s, J({"AIN x", "LT l(x, \"5\") -> l_Q", "EQ e(\"5\", x) -> e_Q"}));
    ed::renameSymbol(s, "x", "ax");
    EXPECT_TEXT(s, J({"AIN ax", "LT l(ax, \"5\") -> l_Q", "EQ e(\"5\", ax) -> e_Q"}));
  }
  {
    // Gate names and presets are not signals.
    Script s = S({"IN a", "TON a_t(\"500ms\", a) -> d", "AND a2(a, a) -> a"});
    ed::renameSymbol(s, "a_t", "x");
    ed::renameSymbol(s, "500ms", "x");
    ed::renameSymbol(s, "a2", "x");
    EXPECT_TEXT(s, J({"IN a", "TON a_t(\"500ms\", a) -> d", "AND a2(a, a) -> a"}));
    ed::renameSymbol(s, "a", "w");
    EXPECT_TEXT(s, J({"IN w", "TON a_t(\"500ms\", w) -> d", "AND a2(w, w) -> w"}));
  }
  {
    // Unknown symbol and from == to leave every line byte for byte alone.
    Script s = S({"IN  a ,b ( B )", "AND   g ( a ,b )->q"});
    std::string before = gll::toText(s);
    ed::renameSymbol(s, "nope", "x");
    ed::renameSymbol(s, "a", "a");
    EXPECT_STR(gll::toText(s), before);
  }
  {
    // Counter outputs and the CV slot.
    Script s = S({"CTU c(u, r) -> c_Q, c_CV", "NOT n(c_CV) -> n_Q"});
    ed::renameSymbol(s, "c_CV", "level");
    EXPECT_TEXT(s, J({"CTU c(u, r) -> c_Q, level", "NOT n(level) -> n_Q"}));
  }
}

void testDetachSymbol()
{
  Script s = S({"IN a, b", "AND g(a, b, a) -> a_out, x", "NOT n(x) -> x", "SR s(a, b) -> a", "# a"});
  ed::detachSymbol(s, "a");
  EXPECT_TEXT(s, J({"IN a, b", "AND g(_nc, b) -> a_out, x", "NOT n(x) -> x", "SR s(_nc, b) ->", "# a"}));
  ed::detachSymbol(s, "x");
  EXPECT_TEXT(s, J({"IN a, b", "AND g(_nc, b) -> a_out", "NOT n(_nc) ->", "SR s(_nc, b) ->", "# a"}));
}

// ------------------------------------------------------------------ preservation

Script preservationScript()
{
  return S({"# Header comment  ",              // 0 trailing spaces
            "",                                // 1
            "\t# tabbed comment",              // 2
            "IN  a ,b ( B )",                  // 3 odd spacing
            "OUT q",                           // 4
            "",                                // 5
            "   AND   g1 ( a ,b )->q",         // 6 odd spacing, indent
            "  OR h(a, b) -> h_Q",             // 7 canonical, indent
            "some unknown line",               // 8
            "\t\tXOR  x ( a,b,  h_Q) -> x_Q   ",  // 9
            "# end"});                         // 10
}

void testPreservation()
{
  const Script orig = preservationScript();
  ECHECK(orig.lines.size() == 11);
  ECHECK(orig.lines[8].kind == Statement::Kind::Unknown && orig.lines[3].items.size() == 2);
  EXPECT_STR(gll::toText(orig), J({"# Header comment  ", "", "\t# tabbed comment", "IN  a ,b ( B )", "OUT q", "",
                                   "   AND   g1 ( a ,b )->q", "  OR h(a, b) -> h_Q", "some unknown line",
                                   "\t\tXOR  x ( a,b,  h_Q) -> x_Q   ", "# end"}));

  {
    Script s = orig;
    ed::connectInput(s, 7, 1, "q");
    EXPECT_OTHERS_SAME(orig, s, 7);
    EXPECT_STR(s.lines[7].raw, "  OR h(a, q) -> h_Q");
  }
  {
    Script s = orig;
    ed::setInputMod(s, 9, 0, Mod::Not);
    EXPECT_OTHERS_SAME(orig, s, 9);
    EXPECT_STR(s.lines[9].raw, "\t\tXOR x(NOT(a), b, h_Q) -> x_Q");  // indent kept, spacing canonical
  }
  {
    Script s = orig;
    ed::disconnectInput(s, 6, 0);
    EXPECT_OTHERS_SAME(orig, s, 6);
    EXPECT_STR(s.lines[6].raw, "   AND g1(_nc, b) -> q");
  }
  {
    Script s = orig;
    ed::renameGate(s, 7, "hh");
    ed::setOutputs(s, 7, {"h_Q", "extra"});
    EXPECT_OTHERS_SAME(orig, s, 7);
    EXPECT_STR(s.lines[7].raw, "  OR hh(a, b) -> h_Q, extra");
  }
  {
    // Rename touches exactly the lines that mention the symbol.
    Script s = orig;
    ed::renameSymbol(s, "b", "bb");
    EXPECT_OTHERS_SAME(orig, s, 3, 6, 7, 9);
    EXPECT_STR(s.lines[3].raw, "IN a, bb(B)");
    EXPECT_STR(s.lines[6].raw, "   AND g1(a, bb) -> q");
    EXPECT_STR(s.lines[9].raw, "\t\tXOR x(a, bb, h_Q) -> x_Q");
  }
  {
    Script s = orig;
    ed::renameSymbol(s, "h_Q", "hq");
    EXPECT_OTHERS_SAME(orig, s, 7, 9);
  }
  {
    // addDecl shifts everything below; nothing else changes.
    Script s = orig;
    auto r = ed::addDecl(s, DeclKind::AIn, "x1");
    ECHECK(r.line == 5);
    ECHECK(s.lines.size() == orig.lines.size() + 1);
    for (size_t i = 0; i < orig.lines.size(); ++i)
      EXPECT_STR(s.lines[i + (i >= 5 ? 1 : 0)].raw, orig.lines[i].raw);
    EXPECT_STR(s.lines[5].raw, "AIN x1");
  }
  {
    Script s = orig;
    auto r = ed::addDecl(s, DeclKind::In, "c");
    ECHECK(r.line == 3 && r.item == 2);
    EXPECT_OTHERS_SAME(orig, s, 3);
    EXPECT_STR(s.lines[3].raw, "IN a, b(B), c");
  }
  {
    // Appending a gate leaves every existing line alone.
    Script s = orig;
    int at = ed::addGate(s, NodeType::SR_, "sr1");
    ECHECK(at == 11 && s.lines.size() == 12);
    for (size_t i = 0; i < orig.lines.size(); ++i)
      EXPECT_STR(s.lines[i].raw, orig.lines[i].raw);
  }
  {
    // Alias: only the lines that mention the old display name change.
    Script s = orig;
    ed::setDeclAlias(s, {3, 0}, "Aa");
    EXPECT_OTHERS_SAME(orig, s, 3, 6, 7, 9);
    EXPECT_STR(s.lines[3].raw, "IN a(Aa), b(B)");
    EXPECT_STR(s.lines[7].raw, "  OR h(Aa, b) -> h_Q");
  }
  {
    // Removing a declaration: lines that do not read it are byte-identical (only line numbers shift).
    Script s = orig;
    ed::removeDecl(s, {4, 0});  // OUT q: removes the whole line, g1 stops driving q
    ECHECK(s.lines.size() == orig.lines.size() - 1);
    for (size_t i = 0; i < orig.lines.size(); ++i)
    {
      if (i == 4 || i == 6 || i == 9)  // 9: reformatted by undrive(), see testUntouchedGateLinesKeepSpacing()
        continue;
      EXPECT_STR(s.lines[i - (i > 4 ? 1 : 0)].raw, orig.lines[i].raw);
    }
    EXPECT_STR(s.lines[5].raw, "   AND g1(a, b) ->");
  }
}

// Structural edits must not reformat gate lines they have no reason to touch.
void testUntouchedGateLinesKeepSpacing()
{
  {
    const Script orig = preservationScript();
    Script s = orig;
    ed::driveTerminal(s, 7, 0, "extra");  // h writes `extra`: only line 7 changes
    EXPECT_OTHERS_SAME(orig, s, 7);
  }
  {
    const Script orig = preservationScript();
    Script s = orig;
    ed::deleteGate(s, 7);  // h: only x (line 9) reads its output; g1 (line 6) has nothing to do with it
    ECHECK(s.lines.size() == orig.lines.size() - 1);
    EXPECT_STR(s.lines[6].raw, orig.lines[6].raw);
  }
  {
    const Script orig = preservationScript();
    Script s = orig;
    ed::removeDecl(s, {4, 0});  // OUT q: only g1 writes it, x is not involved
    EXPECT_STR(s.lines[8].raw, orig.lines[9].raw);
  }
  // What does hold today: comments, blanks and unknown lines are never reformatted, gate indent is kept.
  const Script orig = preservationScript();
  Script s = orig;
  ed::driveTerminal(s, 7, 0, "extra");
  for (size_t i : {0u, 1u, 2u, 3u, 4u, 5u, 8u, 10u})
    EXPECT_STR(s.lines[i].raw, orig.lines[i].raw);
  ECHECK(s.lines[6].indent == "   " && s.lines[9].indent == "\t\t");
}

// ------------------------------------------------------------------ CRLF

void testCrlf()
{
  const std::string crlf = "\r\n";
  std::string text = "# header" + crlf + "IN a, b" + crlf + "OUT q" + crlf + crlf + "  AND g(_nc, _nc) -> q" + crlf;
  Script s = P(text);
  ECHECK(s.lines.size() == 5);  // trailing newline does not add a line
  for (const auto &st : s.lines)
    ECHECK(st.raw.find('\r') == std::string::npos);

  ed::connectInput(s, 4, 0, "a");
  ed::connectInput(s, 4, 1, "b");
  ed::addDecl(s, DeclKind::In, "c");
  ed::connectInput(s, 4, 2, "c");
  int g = ed::addGate(s, NodeType::NOT_, "n1");
  ed::connectInput(s, g, 0, "a");
  ed::setDeclAlias(s, {1, 0}, "Start");
  for (const auto &st : s.lines)
    ECHECK(st.raw.find('\r') == std::string::npos);

  std::string expected = "# header" + crlf + "IN a(Start), b, c" + crlf + "OUT q" + crlf + crlf +
                         "  AND g(Start, b, c) -> q" + crlf + "NOT n1(Start) -> n1_Q";
  EXPECT_STR(gll::toText(s, crlf), expected);
  EXPECT_TEXT(s, "# header\nIN a(Start), b, c\nOUT q\n\n  AND g(Start, b, c) -> q\nNOT n1(Start) -> n1_Q");

  // Untouched CRLF script round-trips exactly (minus the final newline).
  Script t = P(text);
  EXPECT_STR(gll::toText(t, crlf), text.substr(0, text.size() - 2));
  Script u = P(text);
  ed::renameSymbol(u, "nothing", "x");
  EXPECT_STR(gll::toText(u, crlf), text.substr(0, text.size() - 2));
}

// ------------------------------------------------------------------ whole workflow

void testBuildFromScratch()
{
  Script s;
  ed::addDecl(s, DeclKind::In, "a");
  ed::addDecl(s, DeclKind::In, "b");
  ed::addDecl(s, DeclKind::Out, "q");
  EXPECT_TEXT(s, J({"IN a, b", "OUT q"}));
  int g = ed::addGate(s, NodeType::AND_, ed::uniqueName(s, "and"));
  ECHECK(g == 2);
  ed::connectInput(s, g, 0, "a");
  ed::connectInput(s, g, 1, "b");
  ed::setInputMod(s, g, 1, Mod::Not);
  ed::driveTerminal(s, g, 0, "q");
  EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND and1(a, NOT(b)) -> q"}));

  int t = ed::addGate(s, NodeType::TON_, ed::uniqueName(s, "t"));
  ed::setPreset(s, t, "1s");
  ed::connectInput(s, t, 0, ed::ensureOutput(s, g, 0));
  int c = ed::addGate(s, NodeType::CTU_, ed::uniqueName(s, "c"));
  ed::connectInput(s, c, 0, ed::ensureOutput(s, t, 0));
  ed::setPreset(s, c, "3");
  std::string cv = ed::ensureOutput(s, c, 1);
  ECHECK(cv == "c1_CV");
  EXPECT_TEXT(s, J({"IN a, b", "OUT q", "AND and1(a, NOT(b)) -> q", "TON t1(\"1s\", q) -> t1_Q",
                    "CTU c1(\"3\", t1_Q, _nc) -> c1_Q, c1_CV"}));

  Program prog;
  ECHECK(compiles(s, prog));
  ECHECK(prog.inputNames.size() == 2 && prog.outputNames.size() == 1);
  ECHECK(prog.nodes.size() == 4);  // and1 + internal NOT + t1 + c1 (NOT first)
  ECHECK(prog.nodes[0].internal && prog.nodes[1].name == "and1");

  // Take it apart again.
  ed::deleteGate(s, g);  // q is a declared OUT: t1 keeps reading it
  EXPECT_TEXT(s, J({"IN a, b", "OUT q", "TON t1(\"1s\", q) -> t1_Q", "CTU c1(\"3\", t1_Q, _nc) -> c1_Q, c1_CV"}));
  ed::deleteGate(s, 2);  // t1_Q is private: c1 loses its input
  EXPECT_TEXT(s, J({"IN a, b", "OUT q", "CTU c1(\"3\", _nc, _nc) -> c1_Q, c1_CV"}));
}
}  // namespace

int runEditTests()
{
  g_checks = 0;
  g_failures = 0;
  testLookupsAndNames();
  testAddGate();
  testConnectInput();
  testDisconnectInput();
  testSetInputMod();
  testSetInputLiteral();
  testSetPreset();
  testRenameGateAndSetOutputs();
  testEnsureOutput();
  testDriveTerminal();
  testUndrive();
  testCounterQSlot();
  testDeleteGate();
  testAddDecl();
  testRemoveDecl();
  testSetDeclAlias();
  testRenameSymbol();
  testDetachSymbol();
  testPreservation();
  testUntouchedGateLinesKeepSpacing();
  testCrlf();
  testBuildFromScratch();
  std::printf("edit tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures;
}
