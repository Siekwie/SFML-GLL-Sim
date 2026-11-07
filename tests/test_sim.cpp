// Simulation tests for inline modifiers (NOT / PS / NS), including nested
// chains, and for edge-detector memory: it must update on every scan, no
// matter whether the gate that reads it currently cares (no short-circuit).
#include "Edits.hpp"
#include "Gll.hpp"
#include "Parser.hpp"
#include "Sim.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
int g_checks = 0, g_failures = 0;

#define SCHECK(cond)                                                       \
  do                                                                       \
  {                                                                        \
    ++g_checks;                                                            \
    if (!(cond))                                                           \
    {                                                                      \
      ++g_failures;                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                      \
  } while (0)

std::shared_ptr<const Program> build(const std::string &text)
{
  gll::Script script;
  gll::ParseError err;
  auto prog = std::make_shared<Program>();
  if (!gll::parseScript(text, script, err) || !compile(script, *prog).ok)
  {
    std::printf("FAIL: could not compile:\n%s\n", text.c_str());
    ++g_failures;
  }
  return prog;
}

// One complete scan: inputs are committed at its start, every node runs once.
void scan(Simulator &sim, const Program &prog)
{
  for (size_t i = 0; i < prog.nodes.size(); ++i)
    sim.update(0.f, 1.f, false, true);
}

struct Step
{
  std::vector<std::pair<std::string, bool>> set;  // inputs applied before the scan
  std::vector<std::pair<std::string, bool>> expect;
};

// Runs the steps and reports the first scan whose outputs differ.
void runSequence(const char *name, const std::string &text, const std::vector<Step> &steps)
{
  auto prog = build(text);
  Simulator sim(prog);
  for (size_t k = 0; k < steps.size(); ++k)
  {
    for (const auto &[sig, v] : steps[k].set)
      sim.setSignal(sig, v);
    scan(sim, *prog);
    for (const auto &[sig, v] : steps[k].expect)
    {
      ++g_checks;
      if (sim.getSignalValue(sig) != v)
      {
        ++g_failures;
        std::printf("FAIL %s: scan %zu: %s is %d, expected %d\n", name, k + 1, sig.c_str(), !v, v);
      }
    }
  }
}

// x := NOT(PS(a)) is LOW for exactly the scan in which `a` rises.
void testNotOfRisingEdge()
{
  const std::string text = "IN a\nOUT x\nOR gx(NOT(PS(a))) -> x\n";
  auto s = [](bool a, bool x) { return Step{{{"a", a}}, {{"x", x}}}; };
  runSequence("NOT(PS(a))", text,
              {s(0, 1), s(0, 1), s(0, 1), s(1, 0), s(1, 1), s(1, 1), s(0, 1), s(0, 1), s(1, 0), s(1, 1)});
}

// y := b AND PS(a). The edge memory of PS must follow `a` on every scan, also
// while b is LOW, so a rise that happened while b was LOW never fires later.
void testEdgeMemoryWhileGated()
{
  for (const char *text : {"IN a, b\nOUT y\nAND gy(b, PS(a)) -> y\n", "IN a, b\nOUT y\nAND gy(PS(a), b) -> y\n"})
  {
    auto s = [](std::vector<std::pair<std::string, bool>> set, bool y) { return Step{set, {{"y", y}}}; };
    // a rises at scan 3 while b = 0; b goes high at scan 6 (a still high):
    // no late pulse. a falls at 8 and rises again at 9 while b = 1: one pulse.
    runSequence("b AND PS(a), late rise", text,
                {s({{"a", 0}, {"b", 0}}, 0), s({}, 0), s({{"a", 1}}, 0), s({}, 0), s({}, 0), s({{"b", 1}}, 0),
                 s({}, 0), s({{"a", 0}}, 0), s({{"a", 1}}, 1), s({}, 0), s({}, 0)});
    // a rises in the same scan b goes high: one pulse.
    runSequence("b AND PS(a), same scan", text,
                {s({{"a", 0}, {"b", 0}}, 0), s({}, 0), s({{"a", 1}, {"b", 1}}, 1), s({}, 0), s({}, 0)});
    // a rises while b = 1, b drops during the pulse scan's successor: exactly one pulse.
    runSequence("b AND PS(a), b drops", text,
                {s({{"a", 0}, {"b", 1}}, 0), s({{"a", 1}}, 1), s({{"b", 0}}, 0), s({{"b", 1}}, 0), s({}, 0)});
  }
}

// x and y in ONE circuit, sharing the same inputs; two independent PS nodes
// read `a`. x is LOW exactly on the scans where a rises (3 and 9); y pulses
// only at 9, because the rise at 3 happened while b was LOW.
void testSharedInputs()
{
  const std::string text = "IN a, b\nOUT x, y\nOR gx(NOT(PS(a))) -> x\nAND gy(b, PS(a)) -> y\n";
  auto s = [](bool a, bool b, bool x, bool y) { return Step{{{"a", a}, {"b", b}}, {{"x", x}, {"y", y}}}; };
  runSequence("x and y shared inputs", text,
              {s(0, 0, 1, 0), s(0, 0, 1, 0), s(1, 0, 0, 0), s(1, 0, 1, 0), s(1, 0, 1, 0), s(1, 1, 1, 0),
               s(1, 1, 1, 0), s(0, 1, 1, 0), s(1, 1, 0, 1), s(1, 1, 1, 0)});
}

// PS(NOT(a)) pulses on the falling edge of a, like NS(a). It also pulses on
// scan 1 when a starts LOW: edge memory starts LOW (PLC power-up, same as V1),
// and NOT(a) is HIGH from the first scan. This is intended and pinned here.
void testInvertedRisingIsFallingEdge()
{
  const std::string text = "IN a\nOUT x, n\nOR gx(PS(NOT(a))) -> x\nOR gn(NS(a)) -> n\n";
  auto prog = build(text);
  Simulator sim(prog);
  const bool a[] = {0, 0, 1, 1, 1, 1, 1, 0, 1, 1};
  for (int k = 0; k < 10; ++k)
  {
    sim.setSignal("a", a[k]);
    scan(sim, *prog);
    bool fall = k == 7;  // scan 8
    SCHECK(sim.getSignalValue("x") == (fall || k == 0));  // + power-up pulse on scan 1
    SCHECK(sim.getSignalValue("n") == fall);              // NS(a) has none: a starts LOW
  }

  // The same rule for a plain PS(a) when a is already HIGH at power-up.
  auto p2 = build("IN a\nOUT x\nOR gx(PS(a)) -> x\n");
  Simulator s2(p2);
  s2.setSignal("a", true);
  scan(s2, *p2);
  SCHECK(s2.getSignalValue("x"));
  scan(s2, *p2);
  SCHECK(!s2.getSignalValue("x"));
}

// Modifiers on an unconnected input (`_nc`, an ordinary signal nothing drives,
// so LOW). The chain is applied to that LOW like to any other signal: edges
// never fire (except PS over an inner NOT on the power-up scan) and NOT flips.
// Pinned so this cannot change by accident; the editor warns about it instead.
void testUnconnectedWithModifiers()
{
  const std::string text = "IN a\n"
                           "OUT plain, ps, ns, nt, nps, nns, psn, nsn\n"
                           "OR g0(_nc) -> plain\n"
                           "OR g1(PS(_nc)) -> ps\n"
                           "OR g2(NS(_nc)) -> ns\n"
                           "OR g3(NOT(_nc)) -> nt\n"
                           "OR g4(NOT(PS(_nc))) -> nps\n"
                           "OR g5(NOT(NS(_nc))) -> nns\n"
                           "OR g6(PS(NOT(_nc))) -> psn\n"
                           "OR g7(NS(NOT(_nc))) -> nsn\n";
  struct Expect
  {
    const char *sig;
    bool first, later;
  } expect[] = {{"plain", 0, 0}, {"ps", 0, 0},  {"ns", 0, 0},  {"nt", 1, 1},
                {"nps", 1, 1},   {"nns", 1, 1}, {"psn", 1, 0}, {"nsn", 0, 0}};
  auto prog = build(text);
  Simulator sim(prog);
  const bool a[] = {0, 1, 0, 1, 1, 0};  // unrelated activity in the circuit
  for (int k = 0; k < 6; ++k)
  {
    sim.setSignal("a", a[k]);
    scan(sim, *prog);
    for (const auto &e : expect)
    {
      bool want = k == 0 ? e.first : e.later;
      ++g_checks;
      if (sim.getSignalValue(e.sig) != want)
      {
        ++g_failures;
        std::printf("FAIL unconnected %s: scan %d is %d, expected %d\n", e.sig, k + 1, !want, want);
      }
    }
  }
}

// Every inline chain must behave exactly like the same chain built from
// standalone NOT / PS / NS nodes, for any input sequence.
void testChainsMatchStandaloneNodes()
{
  const char *kw[] = {"NOT", "PS", "NS"};
  std::vector<std::vector<int>> chains;
  for (int a = 0; a < 3; ++a)
  {
    chains.push_back({a});
    for (int b = 0; b < 3; ++b)
    {
      chains.push_back({a, b});
      for (int c = 0; c < 3; ++c)
        chains.push_back({a, b, c});
    }
  }

  for (const auto &chain : chains)
  {
    // Inline: OR gi(K1(K2(K3(a)))) -> inl
    std::string inl = "a";
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
      inl = std::string(kw[*it]) + "(" + inl + ")";
    // Standalone: innermost first, each node reading the previous one.
    std::string standalone, prev = "a";
    for (size_t i = 0; i < chain.size(); ++i)
    {
      int k = chain[chain.size() - 1 - i];
      std::string out = "s" + std::to_string(i);
      standalone += std::string(kw[k]) + " n" + std::to_string(i) + "(" + prev + ") -> " + out + "\n";
      prev = out;
    }
    std::string text = "IN a\nOUT inl, ref\nOR gi(" + inl + ") -> inl\n" + standalone + "OR gr(" + prev + ") -> ref\n";

    auto prog = build(text);
    Simulator sim(prog);
    unsigned rng = 12345;
    bool mismatch = false;
    for (int k = 0; k < 300 && !mismatch; ++k)
    {
      rng = rng * 1103515245u + 12345u;
      sim.setSignal("a", (rng >> 16) % 3 == 0);
      scan(sim, *prog);
      mismatch = sim.getSignalValue("inl") != sim.getSignalValue("ref");
    }
    ++g_checks;
    if (mismatch)
    {
      ++g_failures;
      std::printf("FAIL inline chain %s differs from standalone nodes\n", inl.c_str());
    }
  }
}

// Changing the circuit (hot reload / graph edit) renumbers the internal PS
// node; the transferred simulator must not see a fake rising edge.
void testNoSpuriousEdgeAfterReload()
{
  const std::string before = "IN a, b\nOUT y\nAND gy(b, PS(a)) -> y\n";
  const std::string after = "IN a, b\nOUT y, z\nOR other(NOT(b)) -> z\nAND gy(b, PS(a)) -> y\n";
  auto p1 = build(before);
  Simulator s1(p1);
  s1.setSignal("a", true);
  s1.setSignal("b", true);
  scan(s1, *p1);
  SCHECK(s1.getSignalValue("y"));  // the real edge
  scan(s1, *p1);
  SCHECK(!s1.getSignalValue("y"));

  auto p2 = build(after);
  Simulator s2(p2);
  s2.transferStateFrom(s1);
  for (int k = 0; k < 3; ++k)
  {
    scan(s2, *p2);
    SCHECK(!s2.getSignalValue("y"));
  }
  s2.setSignal("a", false);
  scan(s2, *p2);
  s2.setSignal("a", true);
  scan(s2, *p2);
  SCHECK(s2.getSignalValue("y"));  // and real edges still work

  // Same, when the change is made through the editor's operations: a new gate
  // reading PS(a) is added while a is HIGH. Neither gy nor the new gate pulses.
  gll::Script script;
  gll::ParseError err;
  gll::parseScript(before, script, err);
  int line = gll::edits::addGate(script, gll::NodeType::OR_, "or1");
  gll::edits::connectInput(script, line, 0, "a");
  gll::edits::setInputMod(script, line, 0, gll::Mod::Ps);
  auto p3 = std::make_shared<Program>();
  SCHECK(compile(script, *p3).ok);
  Simulator s3(p3);
  s3.transferStateFrom(s1);
  for (int k = 0; k < 3; ++k)
  {
    scan(s3, *p3);
    SCHECK(!s3.getSignalValue("y"));
    SCHECK(!s3.getSignalValue("or1_Q"));
  }
}
}  // namespace

int runSimTests()
{
  testNotOfRisingEdge();
  testEdgeMemoryWhileGated();
  testSharedInputs();
  testInvertedRisingIsFallingEdge();
  testUnconnectedWithModifiers();
  testChainsMatchStandaloneNodes();
  testNoSpuriousEdgeAfterReload();
  std::printf("sim tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures;
}
