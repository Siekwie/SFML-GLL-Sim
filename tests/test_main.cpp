// Headless tests for the GLL language core (syntax layer + compiler).
// Plain asserts, no framework. Build target: gll_tests.
#include "Gll.hpp"
#include "LegacyParser.hpp"
#include "Parser.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;

int runEditTests();  // tests/test_edits.cpp

#define CHECK(cond)                                                              \
  do                                                                             \
  {                                                                              \
    ++g_checks;                                                                  \
    if (!(cond))                                                                 \
    {                                                                            \
      ++g_failures;                                                              \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                            \
  } while (0)

#define CHECK_MSG(cond, msg)                                                     \
  do                                                                             \
  {                                                                              \
    ++g_checks;                                                                  \
    if (!(cond))                                                                 \
    {                                                                            \
      ++g_failures;                                                              \
      std::printf("FAIL %s:%d  %s  [%s]\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); \
    }                                                                            \
  } while (0)

// ---------------------------------------------------------------- helpers

static std::string readFile(const fs::path &p)
{
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::vector<fs::path> sampleFiles()
{
  std::vector<fs::path> files;
  for (const char *dir : {"samples", "samples/tests"})
  {
    fs::path d = fs::path(GLL_SOURCE_DIR) / dir;
    if (!fs::exists(d))
      continue;
    for (const auto &e : fs::directory_iterator(d))
      if (e.is_regular_file() && (e.path().extension() == ".txt" || e.path().extension() == ".gll"))
        files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

// Canonical name per signal id: the smallest symbol that maps to it (aliases share an id).
template <class P>
static std::map<int, std::string> canonicalNames(const P &p)
{
  std::map<int, std::string> m;
  for (const auto &[name, id] : p.symbolToSignal)
  {
    auto it = m.find(id);
    if (it == m.end() || name < it->second)
      m[id] = name;
  }
  return m;
}

template <class P>
static std::vector<std::string> names(const P &, const std::map<int, std::string> &canon, const std::vector<int> &ids)
{
  std::vector<std::string> out;
  for (int id : ids)
    out.push_back(canon.at(id));
  return out;
}

template <class P>
static std::set<std::string> analogNames(const P &p, const std::map<int, std::string> &canon)
{
  std::set<std::string> s;
  for (int id : p.analogSignals)
    s.insert(canon.at(id));
  return s;
}

template <class P>
static std::map<std::string, uint64_t> constants(const P &p, const std::map<int, std::string> &canon)
{
  std::map<std::string, uint64_t> m;
  for (const auto &[id, v] : p.constantSignalValues)
    m[canon.at(id)] = static_cast<uint64_t>(v);
  return m;
}

static bool hasInternalPrefix(const std::string &n)
{
  return n.rfind("_not_", 0) == 0 || n.rfind("_ps_", 0) == 0 || n.rfind("_ns_", 0) == 0;
}

static bool sameStatement(const gll::Statement &a, const gll::Statement &b)
{
  if (a.kind != b.kind)
    return false;
  if (a.kind == gll::Statement::Kind::Decl)
  {
    if (a.decl != b.decl || a.items.size() != b.items.size())
      return false;
    for (size_t i = 0; i < a.items.size(); ++i)
      if (a.items[i].name != b.items[i].name || a.items[i].alias != b.items[i].alias)
        return false;
    return true;
  }
  if (a.kind == gll::Statement::Kind::Gate)
  {
    if (a.type != b.type || a.name != b.name || a.preset != b.preset || a.presetQuoted != b.presetQuoted ||
        a.args.size() != b.args.size() || a.outputs.size() != b.outputs.size())
      return false;
    for (size_t i = 0; i < a.args.size(); ++i)
      if (a.args[i].symbol != b.args[i].symbol || a.args[i].mod != b.args[i].mod || a.args[i].quoted != b.args[i].quoted)
        return false;
    for (size_t i = 0; i < a.outputs.size(); ++i)
      if (a.outputs[i].symbol != b.outputs[i].symbol)
        return false;
    return true;
  }
  return a.raw == b.raw;
}

static gll::Statement line(const std::string &text, bool expectOk = true)
{
  gll::Statement st;
  std::string err;
  bool ok = gll::parseLine(text, st, err);
  CHECK_MSG(ok == expectOk, text + " -> " + err);
  return st;
}

static std::string slice(const gll::Statement &st, gll::Span s)
{
  if (s.col0 < 0 || s.col1 < s.col0 || static_cast<size_t>(s.col1) > st.raw.size())
    return "<bad span>";
  return st.raw.substr(static_cast<size_t>(s.col0), static_cast<size_t>(s.col1 - s.col0));
}

static std::string compileText(const std::string &text, Program &prog)
{
  gll::Script script;
  gll::ParseError err;
  if (!gll::parseScript(text, script, err))
    return "parse error: " + err.message;
  ParseResult r = compile(script, prog);
  return r.ok ? "" : r.msg;
}

static bool hasToken(const Program &p, int lineNo, int col0, int col1, const std::string &sym)
{
  for (const auto &t : p.tokens)
    if (t.line == lineNo && t.col0 == col0 && t.col1 == col1 && t.symbol == sym)
      return true;
  return false;
}

// ---------------------------------------------------------------- tests

static void testEquivalence()
{
  auto files = sampleFiles();
  CHECK(files.size() >= 20);
  for (const auto &path : files)
  {
    std::string tag = path.filename().string();
    legacy::Program lp;
    Program np;
    auto lr = legacy::parseFile(path.string(), lp);
    auto nr = parseFile(path.string(), np);
    CHECK_MSG(lr.ok, tag + ": legacy " + lr.msg);
    CHECK_MSG(nr.ok, tag + ": new " + nr.msg);
    if (!lr.ok || !nr.ok)
      continue;

    CHECK_MSG(lp.inputNames == np.inputNames, tag + ": inputNames");
    CHECK_MSG(lp.outputNames == np.outputNames, tag + ": outputNames");
    CHECK_MSG(lp.analogInputNames == np.analogInputNames, tag + ": analogInputNames");
    CHECK_MSG(lp.analogOutputNames == np.analogOutputNames, tag + ": analogOutputNames");
    CHECK_MSG(lp.nodes.size() == np.nodes.size(), tag + ": node count");
    CHECK_MSG(lp.sourceLines.size() == np.sourceLines.size(), tag + ": sourceLines count");

    auto lc = canonicalNames(lp), nc = canonicalNames(np);
    CHECK_MSG(analogNames(lp, lc) == analogNames(np, nc), tag + ": analogSignals");
    CHECK_MSG(constants(lp, lc) == constants(np, nc), tag + ": constants");
    std::set<std::string> lsyms, nsyms;
    for (const auto &kv : lp.symbolToSignal)
      lsyms.insert(kv.first);
    for (const auto &kv : np.symbolToSignal)
      nsyms.insert(kv.first);
    CHECK_MSG(lsyms == nsyms, tag + ": symbol set");
    CHECK_MSG(static_cast<size_t>(np.signalCount) == nc.size(), tag + ": signalCount");
    for (const auto &kv : np.symbolToSignal)  // every id handed out is below signalCount
      CHECK(kv.second >= 0 && kv.second < np.signalCount);

    size_t n = std::min(lp.nodes.size(), np.nodes.size());
    for (size_t i = 0; i < n; ++i)
    {
      const auto &a = lp.nodes[i];
      const auto &b = np.nodes[i];
      std::string nt = tag + ": node " + std::to_string(i) + " " + a.name;
      CHECK_MSG(static_cast<int>(a.type) == static_cast<int>(b.type), nt + " type");
      CHECK_MSG(a.name == b.name, nt + " name");
      CHECK_MSG(b.internal == hasInternalPrefix(a.name), nt + " internal");
      CHECK_MSG(a.sourceLine == b.sourceLine, nt + " sourceLine");
      CHECK_MSG(a.hardcodedPresetTime == b.hardcodedPresetTime, nt + " presetTime");
      CHECK_MSG(a.hardcodedPresetValue == b.hardcodedPresetValue, nt + " presetValue");
      CHECK_MSG(names(lp, lc, a.inputs) == names(np, nc, b.inputs), nt + " inputs");
      CHECK_MSG(names(lp, lc, a.outputs) == names(np, nc, b.outputs), nt + " outputs");
      CHECK_MSG((a.cvOutputSignal < 0) == (b.cvOutputSignal < 0), nt + " cv presence");
      if (a.cvOutputSignal >= 0 && b.cvOutputSignal >= 0)
        CHECK_MSG(lc.at(a.cvOutputSignal) == nc.at(b.cvOutputSignal), nt + " cv");
    }

    // Tokens: inside the raw line and spelling the symbol they claim.
    for (const auto &t : np.tokens)
    {
      bool ok = t.line >= 0 && static_cast<size_t>(t.line) < np.sourceLines.size();
      CHECK_MSG(ok, tag + ": token line");
      if (!ok)
        continue;
      const std::string &src = np.sourceLines[static_cast<size_t>(t.line)];
      ok = t.col0 >= 0 && t.col1 > t.col0 && static_cast<size_t>(t.col1) <= src.size() &&
           src.substr(static_cast<size_t>(t.col0), static_cast<size_t>(t.col1 - t.col0)) == t.symbol;
      CHECK_MSG(ok, tag + ": token text '" + t.symbol + "'");
    }
  }
}

static void testRoundTrip()
{
  for (const auto &path : sampleFiles())
  {
    std::string tag = path.filename().string();
    std::string text = readFile(path);
    gll::Script script;
    gll::ParseError err;
    CHECK_MSG(gll::parseScript(text, script, err), tag + ": " + err.message);

    std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string expected = text;
    if (expected.size() >= eol.size() && expected.compare(expected.size() - eol.size(), eol.size(), eol) == 0)
      expected.erase(expected.size() - eol.size());
    CHECK_MSG(gll::toText(script, eol) == expected, tag + ": toText");

    for (size_t i = 0; i < script.lines.size(); ++i)
    {
      const gll::Statement &st = script.lines[i];
      if (st.kind != gll::Statement::Kind::Decl && st.kind != gll::Statement::Kind::Gate)
        continue;
      std::string where = tag + ":" + std::to_string(i + 1);
      gll::Statement again;
      std::string e;
      CHECK_MSG(gll::parseLine(gll::formatStatement(st), again, e), where + " " + e);
      CHECK_MSG(sameStatement(st, again), where + " " + st.raw + " vs " + gll::formatStatement(st));
      // Formatting is idempotent.
      CHECK_MSG(gll::formatStatement(again) == gll::formatStatement(st), where + " idempotent");
      gll::Statement r = st;
      gll::refresh(r);
      CHECK_MSG(r.raw == gll::formatStatement(st) && sameStatement(st, r), where + " refresh");
    }
  }
}

static void testTables()
{
  CHECK(gll::allTypes().size() == 16);
  for (gll::NodeType t : gll::allTypes())
  {
    const auto &info = gll::typeInfo(t);
    CHECK(info.type == t);
    CHECK(gll::typeFromKeyword(info.keyword) == t);
    CHECK(info.summary && *info.summary);
  }
  CHECK(!gll::typeFromKeyword("FOO"));
  CHECK(!gll::typeFromKeyword("and"));
  CHECK(gll::typeInfo(gll::NodeType::TON_).presetTime);
  CHECK(gll::typeInfo(gll::NodeType::CTU_).cvOutput && gll::typeInfo(gll::NodeType::CTD_).presetValue);
  CHECK(gll::typeInfo(gll::NodeType::SR_).inputs.size() == 2);
  CHECK(std::string(gll::typeInfo(gll::NodeType::CTD_).inputs[1]) == "LD");
  CHECK(gll::typeInfo(gll::NodeType::AND_).variadic && gll::typeInfo(gll::NodeType::BTN).inputs.empty());
  CHECK(std::string(gll::categoryName(gll::typeInfo(gll::NodeType::PS_).category)) == "Edge");
  CHECK(std::string(gll::categoryName(gll::typeInfo(gll::NodeType::BTN).category)) == "Input");
  CHECK(std::string(gll::modKeyword(gll::Mod::Ns)) == "NS" && std::string(gll::modKeyword(gll::Mod::None)).empty());
  CHECK(std::string(gll::declKeyword(gll::DeclKind::AOut)) == "AOUT");
}

static void testLiteralsAndIdentifiers()
{
  CHECK(gll::parseLiteral("0") == 0u);
  CHECK(gll::parseLiteral("128") == 128u);
  CHECK(gll::parseLiteral("\"0x80\"") == 128u);
  CHECK(gll::parseLiteral("0XfF") == 255u);
  CHECK(gll::parseLiteral("18446744073709551615") == UINT64_MAX);
  CHECK(!gll::parseLiteral("18446744073709551616"));
  CHECK(gll::parseLiteral("0xFFFFFFFFFFFFFFFF") == UINT64_MAX);
  CHECK(!gll::parseLiteral("0x1FFFFFFFFFFFFFFFF"));
  CHECK(!gll::parseLiteral(""));
  CHECK(!gll::parseLiteral("0x"));
  CHECK(!gll::parseLiteral("-1"));
  CHECK(!gll::parseLiteral("12a"));
  CHECK(!gll::parseLiteral("abc"));

  CHECK(gll::isValidIdentifier("a"));
  CHECK(gll::isValidIdentifier("_nc"));
  CHECK(gll::isValidIdentifier("ArmFährtX"));
  CHECK(gll::isValidIdentifier("x1"));
  CHECK(!gll::isValidIdentifier(""));
  CHECK(!gll::isValidIdentifier("a b"));
  CHECK(!gll::isValidIdentifier("a,b"));
  CHECK(!gll::isValidIdentifier("a(b"));
  CHECK(!gll::isValidIdentifier("a)"));
  CHECK(!gll::isValidIdentifier("a\"b"));
  CHECK(!gll::isValidIdentifier("a#b"));
  CHECK(!gll::isValidIdentifier("a->b"));
  CHECK(!gll::isValidIdentifier("1a"));
  CHECK(!gll::isValidIdentifier(".5"));
  CHECK(!gll::isValidIdentifier("-a"));
  CHECK(gll::isValidIdentifier("a-b"));
}

static void testParseLine()
{
  using K = gll::Statement::Kind;
  CHECK(line("").kind == K::Blank);
  CHECK(line("   \t").kind == K::Blank);
  CHECK(line("   \t").indent == "   \t");
  auto c = line("  # hi -> there");
  CHECK(c.kind == K::Comment && c.indent == "  ");
  auto u = line("  just some text");
  CHECK(u.kind == K::Unknown && u.raw == "  just some text");
  CHECK(line("IN").kind == K::Unknown);  // keyword without space is not a declaration

  auto d = line("  AIN  x , y(why)");
  CHECK(d.kind == K::Decl && d.decl == gll::DeclKind::AIn && d.items.size() == 2);
  CHECK(d.indent == "  ");
  CHECK(d.items[0].name == "x" && d.items[0].alias.empty() && slice(d, d.items[0].nameSpan) == "x");
  CHECK(d.items[1].name == "y" && d.items[1].alias == "why");
  CHECK(slice(d, d.items[1].nameSpan) == "y" && slice(d, d.items[1].aliasSpan) == "why");
  CHECK(d.items[1].display() == "why" && d.items[0].display() == "x");
  CHECK(gll::formatStatement(d) == "  AIN x, y(why)");
  CHECK(line("OUT a,,b").items.size() == 2);

  auto g = line("AND gate1( a ,NOT( b ), \"7\" )  ->  c,d");
  CHECK(g.kind == K::Gate && g.type == gll::NodeType::AND_ && g.name == "gate1");
  CHECK(g.args.size() == 3 && g.outputs.size() == 2);
  CHECK(g.args[1].mod == gll::Mod::Not && g.args[1].symbol == "b" && slice(g, g.args[1].span) == "b");
  CHECK(g.args[2].quoted && g.args[2].symbol == "7" && slice(g, g.args[2].span) == "7");
  CHECK(slice(g, g.nameSpan) == "gate1" && slice(g, g.outputs[1].span) == "d");
  CHECK(gll::formatStatement(g) == "AND gate1(a, NOT(b), \"7\") -> c, d");

  CHECK(gll::formatStatement(line("AND g() ->")) == "AND g() ->");
  CHECK(gll::formatStatement(line("BTN b() -> q")) == "BTN b() -> q");
  CHECK(gll::formatStatement(line("  OR  o ( a , b )->x")) == "  OR o(a, b) -> x");

  auto t = line("TON t1(500ms, start) -> done");
  CHECK(t.preset == "500ms" && !t.presetQuoted && t.args.size() == 1 && slice(t, t.presetSpan) == "500ms");
  CHECK(gll::formatStatement(t) == "TON t1(500ms, start) -> done");  // unquoted stays unquoted
  t.presetQuoted = true;
  CHECK(gll::formatStatement(t) == "TON t1(\"500ms\", start) -> done");
  auto t2 = line("TON t2(\".5s\",a) -> b");
  CHECK(t2.preset == ".5s" && t2.presetQuoted && slice(t2, t2.presetSpan) == ".5s");
  CHECK(!line("TON t3(start, 5) -> b").preset);  // only the first argument can be a preset
  CHECK(!line("TON t3(PS(5), a) -> b").preset);

  auto ctu = line("CTU c(\"10\", up, rst) -> done, cv");
  CHECK(ctu.preset == "10" && ctu.args.size() == 2 && ctu.outputs.size() == 2);
  CHECK(gll::formatStatement(ctu) == "CTU c(\"10\", up, rst) -> done, cv");
  CHECK(line("CTD c(-3, a, b) -> x").preset == "-3");
  CHECK(!line("CTU c(-, a) -> x").preset);
  CHECK(!line("CTU c(\"abc\", a) -> x").preset);
  CHECK(!line("CTU c(a, b) -> x").preset);

  auto lt = line("LT x(a, \"0x80\") -> y");
  CHECK(lt.args[1].quoted && gll::formatStatement(lt) == "LT x(a, \"0x80\") -> y");
  CHECK(gll::formatStatement(line("LT x(a, 5) -> y")) == "LT x(a, 5) -> y");

  // Nested / malformed modifiers
  std::string err;
  gll::Statement st;
  CHECK(!gll::parseLine("AND g(NOT(PS(a))) -> x", st, err) && err == "Nested modifiers are not supported");
  CHECK(!gll::parseLine("AND g(NOT(NOT(a)), b) -> x", st, err) && err == "Nested modifiers are not supported");
  CHECK(!gll::parseLine("AND g(NOT(a, b)) -> x", st, err));
  CHECK(!gll::parseLine("AND g(NOT()) -> x", st, err));

  // Errors
  CHECK(!gll::parseLine("AND -> x", st, err) && err == "Invalid gate syntax");
  CHECK(!gll::parseLine("AND g a, b -> x", st, err) && err == "Missing '(' in gate definition");
  CHECK(!gll::parseLine("AND g(a, b -> x", st, err) && err == "Missing ')' in gate definition");
  CHECK(!gll::parseLine("AND g(a, NOT(b) -> x", st, err) && err == "Missing ')' in gate definition");
  CHECK(!gll::parseLine("FOO g(a) -> x", st, err) && err == "Unknown gate type: FOO");
  CHECK(!gll::parseLine("IN a(b", st, err));

  // Trailing \r is not part of the line
  auto cr = line("AND g(a) -> b\r");
  CHECK(cr.raw == "AND g(a) -> b" && slice(cr, cr.outputs[0].span) == "b");
}

static void testParseScript()
{
  gll::Script s;
  gll::ParseError err;
  CHECK(gll::parseScript("a\n", s, err) && s.lines.size() == 1);
  CHECK(gll::parseScript("a\n\n", s, err) && s.lines.size() == 2 && s.lines[1].kind == gll::Statement::Kind::Blank);
  CHECK(gll::parseScript("a", s, err) && s.lines.size() == 1);
  CHECK(gll::parseScript("", s, err) && s.lines.empty());
  CHECK(gll::parseScript("\n", s, err) && s.lines.size() == 1);
  CHECK(gll::parseScript("IN a\nOUT b\n\n# c\nAND g(a) -> b\n", s, err) && s.lines.size() == 5);
  CHECK(gll::toText(s) == "IN a\nOUT b\n\n# c\nAND g(a) -> b");
  CHECK(gll::toText(s, "\r\n") == "IN a\r\nOUT b\r\n\r\n# c\r\nAND g(a) -> b");

  // Errors carry the 0-based line
  CHECK(!gll::parseScript("IN a\n\nFOO g(a) -> b\nIN c", s, err));
  CHECK(err.line == 2 && err.message == "Unknown gate type: FOO");
  CHECK(!gll::parseScript("IN a\nAND g(a -> b\n", s, err));
  CHECK(err.line == 1 && err.message == "Missing ')' in gate definition");
  CHECK(!gll::parseScript("AND g a -> b\n", s, err));
  CHECK(err.line == 0 && err.message == "Missing '(' in gate definition");

  // CRLF input
  const std::string crlf = "IN a\r\n  OUT b\r\n\r\nAND g(NOT(a)) -> b\r\n";
  CHECK(gll::parseScript(crlf, s, err) && s.lines.size() == 4);
  CHECK(s.lines[1].raw == "  OUT b" && s.lines[1].indent == "  ");
  CHECK(s.lines[2].kind == gll::Statement::Kind::Blank && s.lines[2].raw.empty());
  CHECK(gll::toText(s, "\r\n") + "\r\n" == crlf);
  Program p;
  CHECK(compileText(crlf, p).empty());
  CHECK(p.sourceLines.size() == 4 && p.sourceLines[0] == "IN a" && p.nodes.size() == 2);
  CHECK(hasToken(p, 3, 10, 11, "a"));
  CHECK(hasToken(p, 3, 17, 18, "b"));
}

static void testCompile()
{
  Program p;
  CHECK(compileText("# nothing\n\nsome unknown line\n", p).empty());
  CHECK(p.nodes.empty() && p.sourceLines.size() == 3 && p.tokens.empty());

  // Alias: shares the id, display name is the alias, both tokens exist
  CHECK(compileText("IN INPUT_0(Sensor), b\nOUT q\nAND g(Sensor, INPUT_0) -> q\n", p).empty());
  CHECK(p.inputNames == (std::vector<std::string>{"Sensor", "b"}));
  CHECK(p.symbolToSignal.at("Sensor") == p.symbolToSignal.at("INPUT_0"));
  CHECK(p.signalCount == 3);  // INPUT_0/Sensor, b, q
  CHECK(p.symbolToSignal.at("q") == 2);
  CHECK(p.nodes.size() == 1 && p.nodes[0].inputs[0] == p.nodes[0].inputs[1]);
  CHECK(hasToken(p, 0, 3, 10, "INPUT_0"));
  CHECK(hasToken(p, 0, 11, 17, "Sensor"));
  CHECK(hasToken(p, 0, 20, 21, "b"));

  // signalCount counts ids, not names
  CHECK(compileText("IN a(x), b(y)\n", p).empty());
  CHECK(p.signalCount == 2 && p.symbolToSignal.size() == 4);
  CHECK(p.symbolToSignal.at("a") != p.symbolToSignal.at("b"));
  CHECK(p.symbolToSignal.at("x") == p.symbolToSignal.at("a") && p.symbolToSignal.at("y") == p.symbolToSignal.at("b"));

  // Indented line: columns refer to the raw, untrimmed line
  CHECK(compileText("IN a, b\nOUT x\n    AND g1(a,  NOT(b)) -> x\n", p).empty());
  CHECK(hasToken(p, 2, 11, 12, "a"));
  CHECK(hasToken(p, 2, 19, 20, "b"));
  CHECK(hasToken(p, 2, 26, 27, "x"));
  CHECK(!hasToken(p, 2, 4, 7, "AND"));
  for (const auto &t : p.tokens)  // no tokens for gate / internal names
    CHECK(t.symbol.rfind("_not_", 0) != 0 && t.symbol != "g1");

  // Internal nodes: order, naming, NOT/PS/NS grouping
  CHECK(compileText("IN a, b, c, d\nOUT x\nAND g(PS(a), NOT(b), c, NS(d), NOT(c)) -> x\n", p).empty());
  CHECK(p.nodes.size() == 5);
  CHECK(p.nodes[0].name == "_not_0" && p.nodes[0].type == Program::Node::NOT_ && p.nodes[0].internal);
  CHECK(p.nodes[1].name == "_not_1" && p.nodes[1].internal);
  CHECK(p.nodes[2].name == "_ps_2" && p.nodes[2].type == Program::Node::PS_ && p.nodes[2].internal);
  CHECK(p.nodes[3].name == "_ns_3" && p.nodes[3].type == Program::Node::NS_ && p.nodes[3].internal);
  CHECK(p.nodes[4].name == "g" && !p.nodes[4].internal && p.nodes[4].inputs.size() == 5);
  CHECK(p.symbolToSignal.count("_not_0_out") && p.symbolToSignal.count("_ps_2_out"));
  CHECK(p.nodes[4].inputs[0] == p.symbolToSignal.at("_ps_2_out"));
  CHECK(p.nodes[4].inputs[1] == p.symbolToSignal.at("_not_0_out") || p.nodes[4].inputs[1] == p.symbolToSignal.at("_not_1_out"));
  CHECK(p.nodes[4].inputs[3] == p.symbolToSignal.at("_ns_3_out"));
  CHECK(p.nodes[0].sourceLine == 2 && p.nodes[4].sourceLine == 2);
  CHECK(p.nodes[0].inputs[0] == p.symbolToSignal.at("b"));  // NOT(b) comes first in arg order

  // Presets
  CHECK(compileText("IN a\nOUT x, y, z\nTON t(\"500ms\", a) -> x\nCTU c(\"10\", a, a) -> y, z\n", p).empty());
  CHECK(p.nodes[0].hardcodedPresetTime == 0.5f && p.nodes[0].inputs.size() == 1);
  CHECK(p.nodes[1].hardcodedPresetValue == 10 && p.nodes[1].inputs.size() == 2);
  CHECK(p.nodes[1].outputs.size() == 1 && p.nodes[1].cvOutputSignal == p.symbolToSignal.at("z"));
  CHECK(p.nodes[0].hardcodedPresetValue == -1 && p.nodes[1].hardcodedPresetTime == -1.0f);

  // Literals: full uint64 range, quoted or not, hex or decimal
  CHECK(compileText("AIN a\nOUT y\nLT l1(a, 1000) -> y\nLT l2(a, \"0xFFFF\") -> y\nGT l3(a, 18446744073709551615) -> y\nEQ l4(a, \"0x80\") -> y\nLT l5(a, 128) -> y\n", p).empty());
  CHECK(p.symbolToSignal.count("_const_1000") && p.symbolToSignal.count("_const_65535"));
  CHECK(p.constantSignalValues.at(p.symbolToSignal.at("_const_1000")) == 1000u);
  CHECK(p.constantSignalValues.at(p.symbolToSignal.at("_const_65535")) == 0xFFFFu);
  CHECK(p.constantSignalValues.at(p.symbolToSignal.at("_const_18446744073709551615")) == UINT64_MAX);
  CHECK(p.analogSignals.count(p.symbolToSignal.at("_const_1000")));
  CHECK(p.nodes[0].inputs[1] == p.symbolToSignal.at("_const_1000"));
  CHECK(p.nodes[3].inputs[1] == p.nodes[4].inputs[1]);  // "0x80" and 128 share one constant
  CHECK(p.symbolToSignal.count("0xFFFF") == 0 && p.symbolToSignal.count("1000") == 0);
  for (const auto &t : p.tokens)
    CHECK(t.symbol.rfind("_const_", 0) != 0 && t.symbol != "1000" && t.symbol != "0xFFFF" && t.symbol != "128");
  // Literals only count for comparators
  CHECK(compileText("AND g(a, 5) -> y\n", p).empty());
  CHECK(p.symbolToSignal.count("5") == 1 && p.constantSignalValues.empty());

  // Analog declarations
  CHECK(compileText("AIN s(sensor)\nAOUT m\n", p).empty());
  CHECK(p.analogInputNames == (std::vector<std::string>{"sensor"}) && p.analogOutputNames == (std::vector<std::string>{"m"}));
  CHECK(p.analogSignals.size() == 2 && p.analogSignals.count(p.symbolToSignal.at("sensor")));

  // Errors through parseFile text path
  Program q;
  std::string msg = compileText("IN a\nFOO g(a) -> b\n", q);
  CHECK(msg.find("Unknown gate type: FOO") != std::string::npos);
  CHECK(compileText("AND g(NOT(PS(a))) -> b\n", q).find("Nested modifiers are not supported") != std::string::npos);
  CHECK(compileText("IN a\n\nAND g -> b\n", q).find("Missing '('") != std::string::npos);

  // parseFile error format
  fs::path tmp = fs::temp_directory_path() / "gll_core_test_bad.gll";
  {
    std::ofstream f(tmp, std::ios::binary);
    f << "IN a\r\nAND g(a, b -> c\r\n";
  }
  Program bad;
  ParseResult r = parseFile(tmp.string(), bad);
  CHECK(!r.ok && r.line == 1 && r.msg == "Line 2: Missing ')' in gate definition");
  fs::remove(tmp);
  CHECK(!parseFile((fs::temp_directory_path() / "gll_does_not_exist.gll").string(), bad).ok);
}

int main()
{
  testTables();
  testLiteralsAndIdentifiers();
  testParseLine();
  testParseScript();
  testCompile();
  testEquivalence();
  testRoundTrip();
  std::printf("%d checks, %d failures, %zu sample files\n", g_checks, g_failures, sampleFiles().size());
  int editFailures = runEditTests();
  return g_failures + editFailures == 0 ? 0 : 1;
}
