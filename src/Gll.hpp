#pragma once
// Syntax layer of GLL.
//
// A Script is a lossless, line-by-line model of a .gll file: every source line
// becomes exactly one Statement. Comments, blank lines and unknown lines are
// kept verbatim, so the node editor can rewrite single statements without
// disturbing the rest of the file. The compiler (Parser.hpp) turns a Script
// into a Program for the simulator.
#include "AST.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gll
{
using NodeType = Program::Node::Type;

// Placeholder written into fixed-arity ports that have nothing connected.
// It is an ordinary (never driven) signal, so it always reads LOW.
inline constexpr const char *kNotConnected = "_nc";

enum class Category
{
  Logic,
  Edge,
  Memory,
  Timer,
  Counter,
  Compare,
  Input
};

struct NodeTypeInfo
{
  NodeType type;
  const char *keyword;     // "AND"
  Category category;
  const char *summary;     // one-line description for palette / inspector
  std::vector<const char *> inputs;  // fixed input port labels (empty for variadic / BTN)
  bool variadic = false;   // AND / OR / XOR take any number of inputs
  bool presetTime = false; // TON / TOF: optional leading time literal ("500ms")
  bool presetValue = false;// CTU / CTD: optional leading PV literal ("10")
  bool cvOutput = false;   // CTU / CTD: second output is the counter value
  bool literals = false;   // LT / GT / EQ: args may be numeric literals
};

const NodeTypeInfo &typeInfo(NodeType t);
const std::vector<NodeType> &allTypes();
std::optional<NodeType> typeFromKeyword(std::string_view keyword);
const char *categoryName(Category c);

// Inline argument modifier: NOT(x), PS(x), NS(x)
enum class Mod
{
  None,
  Not,
  Ps,
  Ns
};
const char *modKeyword(Mod m);  // "", "NOT", "PS", "NS"

// Column range [col0, col1) inside Statement::raw. -1 when unknown.
struct Span
{
  int col0 = -1, col1 = -1;
};

struct Arg
{
  std::string symbol;  // signal name, or literal text without quotes
  Mod mod = Mod::None;
  bool quoted = false; // literal was written as "..."
  Span span;           // span of `symbol` in the raw line
};

struct Output
{
  std::string symbol;
  Span span;
};

enum class DeclKind
{
  In,
  Out,
  AIn,
  AOut
};
const char *declKeyword(DeclKind k);  // "IN", "OUT", "AIN", "AOUT"

struct DeclItem
{
  std::string name;   // e.g. INPUT_0
  std::string alias;  // e.g. Sensor (may be empty)
  Span nameSpan, aliasSpan;
  const std::string &display() const { return alias.empty() ? name : alias; }
};

struct Statement
{
  enum class Kind
  {
    Blank,
    Comment,
    Decl,
    Gate,
    Unknown  // non-empty line that is neither: ignored by the compiler (V1 behaviour)
  } kind = Kind::Blank;

  std::string raw;     // the line exactly as it is in the file (no trailing \r / \n)
  std::string indent;  // leading whitespace of raw, preserved when re-formatting

  // Kind::Decl
  DeclKind decl = DeclKind::In;
  std::vector<DeclItem> items;

  // Kind::Gate
  NodeType type = NodeType::AND_;
  std::string name;
  Span nameSpan;
  std::optional<std::string> preset;  // TON/TOF time or CTU/CTD PV literal, without quotes
  bool presetQuoted = true;
  Span presetSpan;
  std::vector<Arg> args;              // signal arguments (preset excluded)
  std::vector<Output> outputs;
};

struct ParseError
{
  int line = -1;  // 0-based line index, -1 if not line specific
  std::string message;
};

struct Script
{
  std::vector<Statement> lines;  // exactly one entry per source line
};

// Parse one line. On failure returns false and fills `error` (message only).
bool parseLine(const std::string &raw, Statement &out, std::string &error);

// Parse a whole text (\n or \r\n separated). A trailing newline does not
// produce an extra empty statement. On failure `error.line` is set.
bool parseScript(const std::string &text, Script &out, ParseError &error);

// Canonical text for a statement, keeping its indent. Blank / Comment /
// Unknown statements return `raw` unchanged.
//   IN a, INPUT_0(sensor)
//   AND gate1(a, NOT(b)) -> c, d
//   TON t1("500ms", start) -> done
std::string formatStatement(const Statement &st);

// Re-format `st` and re-parse it so `raw` and all spans are consistent again.
// Call this after mutating the structured fields of a Decl / Gate statement.
void refresh(Statement &st);

// Join all statement `raw` texts with `eol`. No trailing newline is added.
std::string toText(const Script &s, const std::string &eol = "\n");

// Numeric literal as accepted by comparators: decimal ("128") or hex
// ("0x80"), optionally quoted. Returns nullopt for anything else.
std::optional<uint64_t> parseLiteral(std::string_view text);

// True if `s` can be used as a node or signal name in GLL source.
bool isValidIdentifier(std::string_view s);

}  // namespace gll
