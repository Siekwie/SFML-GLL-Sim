#include "Gll.hpp"
#include <cctype>
#include <cstddef>

namespace gll
{
// ---------------------------------------------------------------- tables

namespace
{
using T = NodeType;

// Indexed by NodeType, so the order must match the enum in AST.hpp.
const std::vector<NodeTypeInfo> &table()
{
  static const std::vector<NodeTypeInfo> t = {
    {T::AND_, "AND", Category::Logic, "AND — output is high when all inputs are high", {}, true},
    {T::OR_, "OR", Category::Logic, "OR — output is high when any input is high", {}, true},
    {T::XOR_, "XOR", Category::Logic, "XOR — output is high when an odd number of inputs are high", {}, true},
    {T::NOT_, "NOT", Category::Logic, "NOT — inverts its input", {"IN"}},
    {T::PS_, "PS", Category::Edge, "PS — one-scan pulse on the rising edge of IN", {"IN"}},
    {T::NS_, "NS", Category::Edge, "NS — one-scan pulse on the falling edge of IN", {"IN"}},
    {T::SR_, "SR", Category::Memory, "SR — set-dominant latch: S sets Q, R resets it", {"S", "R"}},
    {T::RS_, "RS", Category::Memory, "RS — reset-dominant latch: S sets Q, R resets it", {"S", "R"}},
    {T::TON_, "TON", Category::Timer, "TON — on-delay timer: Q goes high after IN has been high for PT", {"IN"}, false, true},
    {T::TOF_, "TOF", Category::Timer, "TOF — off-delay timer: Q stays high for PT after IN goes low", {"IN"}, false, true},
    {T::CTU_, "CTU", Category::Counter, "CTU — up counter: counts CU pulses, R resets, Q when CV >= PV", {"CU", "R"}, false, false, true, true},
    {T::CTD_, "CTD", Category::Counter, "CTD — down counter: counts CD pulses, LD reloads, Q when CV <= 0", {"CD", "LD"}, false, false, true, true},
    {T::LT_, "LT", Category::Compare, "LT — high when A < B", {"A", "B"}, false, false, false, false, true},
    {T::GT_, "GT", Category::Compare, "GT — high when A > B", {"A", "B"}, false, false, false, false, true},
    {T::EQ_, "EQ", Category::Compare, "EQ — high when A = B", {"A", "B"}, false, false, false, false, true},
    {T::BTN, "BTN", Category::Input, "BTN — push button input", {}},
  };
  return t;
}

const char *const kCategoryNames[] = {"Logic", "Edge", "Memory", "Timer", "Counter", "Compare", "Input"};
const char *const kModKeywords[] = {"", "NOT", "PS", "NS"};
const char *const kDeclKeywords[] = {"IN", "OUT", "AIN", "AOUT"};
}  // namespace

const NodeTypeInfo &typeInfo(NodeType t)
{
  return table()[static_cast<size_t>(t)];
}

const std::vector<NodeType> &allTypes()
{
  static const std::vector<NodeType> all = [] {
    std::vector<NodeType> v;
    for (const auto &i : table())
      v.push_back(i.type);
    return v;
  }();
  return all;
}

std::optional<NodeType> typeFromKeyword(std::string_view keyword)
{
  for (const auto &i : table())
    if (keyword == i.keyword)
      return i.type;
  return std::nullopt;
}

const char *categoryName(Category c)
{
  return kCategoryNames[static_cast<size_t>(c)];
}

const char *modKeyword(Mod m)
{
  return kModKeywords[static_cast<size_t>(m)];
}

const char *declKeyword(DeclKind k)
{
  return kDeclKeywords[static_cast<size_t>(k)];
}

// ---------------------------------------------------------------- parsing helpers

namespace
{
bool isSpace(char c)
{
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

// Piece of the raw line with absolute columns [col0, col1).
struct Piece
{
  std::string text;
  int col0 = -1, col1 = -1;
  Span span() const { return {col0, col1}; }
};

// raw[from, to) with surrounding whitespace removed (empty text if nothing is left).
Piece trimmed(const std::string &raw, size_t from, size_t to)
{
  while (from < to && isSpace(raw[from]))
    ++from;
  while (to > from && isSpace(raw[to - 1]))
    --to;
  return {raw.substr(from, to - from), static_cast<int>(from), static_cast<int>(to)};
}

// Split raw[from, to) on commas outside parentheses. Pieces are trimmed and empty ones dropped.
std::vector<Piece> splitList(const std::string &raw, size_t from, size_t to)
{
  std::vector<Piece> out;
  int depth = 0;
  size_t start = from;
  auto flush = [&](size_t end) {
    Piece p = trimmed(raw, start, end);
    if (!p.text.empty())
      out.push_back(std::move(p));
  };
  for (size_t i = from; i < to; ++i)
  {
    if (raw[i] == '(')
      ++depth;
    else if (raw[i] == ')' && depth > 0)
      --depth;
    else if (raw[i] == ',' && depth == 0)
    {
      flush(i);
      start = i + 1;
    }
  }
  flush(to);
  return out;
}

bool isQuoted(const std::string &s)
{
  return s.size() >= 2 && s.front() == '"' && s.back() == '"';
}

bool startsWith(const std::string &s, const char *prefix)
{
  return s.rfind(prefix, 0) == 0;
}

// One declaration item: `name` or `name(alias)`.
bool parseDeclItem(const Piece &p, DeclItem &item, std::string &error)
{
  size_t open = p.text.find('(');
  size_t close = p.text.find(')');
  if (open == std::string::npos && close == std::string::npos)
  {
    item.name = p.text;
    item.nameSpan = p.span();
    return true;
  }
  if (open == std::string::npos || close == std::string::npos || close < open || close + 1 != p.text.size())
  {
    error = "Malformed alias in declaration: " + p.text;
    return false;
  }
  const std::string &raw = p.text;
  Piece name = trimmed(raw, 0, open);
  Piece alias = trimmed(raw, open + 1, close);
  if (name.text.empty())
  {
    error = "Missing signal name in declaration";
    return false;
  }
  item.name = name.text;
  item.nameSpan = {p.col0 + name.col0, p.col0 + name.col1};
  if (!alias.text.empty())
  {
    item.alias = alias.text;
    item.aliasSpan = {p.col0 + alias.col0, p.col0 + alias.col1};
  }
  return true;
}

bool parseDecl(const std::string &raw, size_t from, size_t to, Statement &st, std::string &error)
{
  for (const Piece &p : splitList(raw, from, to))
  {
    DeclItem item;
    if (!parseDeclItem(p, item, error))
      return false;
    st.items.push_back(std::move(item));
  }
  return true;
}

// One gate argument: `x`, `"lit"` or `MOD(x)`.
bool parseArg(const Piece &p, Arg &arg, std::string &error)
{
  Piece inner = p;
  for (Mod m : {Mod::Not, Mod::Ps, Mod::Ns})
  {
    std::string kw = modKeyword(m);
    if (!startsWith(p.text, (kw + "(").c_str()))
      continue;
    // The modifier's parenthesis must close at the very end of the argument.
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = kw.size(); i < p.text.size() && close == std::string::npos; ++i)
    {
      if (p.text[i] == '(')
        ++depth;
      else if (p.text[i] == ')' && --depth == 0)
        close = i;
    }
    if (close != p.text.size() - 1)
    {
      error = "Unexpected text after modifier " + kw + "(...)";
      return false;
    }
    Piece body = trimmed(p.text, kw.size() + 1, close);
    if (body.text.find_first_of("()") != std::string::npos)
    {
      error = "Nested modifiers are not supported";
      return false;
    }
    if (body.text.find(',') != std::string::npos)
    {
      error = kw + "() takes exactly one argument";
      return false;
    }
    if (body.text.empty())
    {
      error = kw + "() needs an argument";
      return false;
    }
    arg.mod = m;
    inner = {body.text, p.col0 + body.col0, p.col0 + body.col1};
    break;
  }
  if (isQuoted(inner.text))
  {
    arg.quoted = true;
    inner = {inner.text.substr(1, inner.text.size() - 2), inner.col0 + 1, inner.col1 - 1};
  }
  else if (inner.text.find_first_of("()") != std::string::npos)
  {
    error = "Unexpected parenthesis in argument: " + inner.text;
    return false;
  }
  arg.symbol = inner.text;
  arg.span = inner.span();
  return true;
}

// True if the first argument of a TON/TOF/CTU/CTD is its preset rather than a signal.
bool isPresetArg(const NodeTypeInfo &info, const Arg &a)
{
  if (a.mod != Mod::None)
    return false;
  if (info.presetTime)
    return a.quoted || (!a.symbol.empty() && (std::isdigit(static_cast<unsigned char>(a.symbol[0])) || a.symbol[0] == '.'));
  if (info.presetValue)
  {
    bool looksNumeric = a.quoted || (!a.symbol.empty() && std::isdigit(static_cast<unsigned char>(a.symbol[0]))) ||
                        (a.symbol.size() > 1 && a.symbol[0] == '-');
    if (!looksNumeric)
      return false;
    try
    {
      (void)std::stoi(a.symbol);
      return true;
    }
    catch (...)
    {
      return false;  // not a number after all: it is an ordinary signal
    }
  }
  return false;
}

// `<TYPE> <name>(<args>) -> <outputs>` inside raw[from, to).
bool parseGate(const std::string &raw, size_t from, size_t to, size_t arrow, Statement &st, std::string &error)
{
  Piece head = trimmed(raw, from, arrow);
  size_t space = head.text.find(' ');
  if (space == std::string::npos)
  {
    error = "Invalid gate syntax";
    return false;
  }
  std::string keyword = head.text.substr(0, space);
  size_t rest = static_cast<size_t>(head.col0) + space + 1;
  size_t open = raw.find('(', rest);
  if (open == std::string::npos || open >= static_cast<size_t>(head.col1))
  {
    error = "Missing '(' in gate definition";
    return false;
  }
  int depth = 0;
  size_t close = std::string::npos;
  for (size_t i = open; i < static_cast<size_t>(head.col1) && close == std::string::npos; ++i)
  {
    if (raw[i] == '(')
      ++depth;
    else if (raw[i] == ')' && --depth == 0)
      close = i;
  }
  if (close == std::string::npos)
  {
    error = "Missing ')' in gate definition";
    return false;
  }
  auto type = typeFromKeyword(keyword);
  if (!type)
  {
    error = "Unknown gate type: " + keyword;
    return false;
  }
  st.type = *type;
  Piece name = trimmed(raw, rest, open);
  st.name = name.text;
  st.nameSpan = name.span();

  for (const Piece &p : splitList(raw, open + 1, close))
  {
    Arg a;
    if (!parseArg(p, a, error))
      return false;
    st.args.push_back(std::move(a));
  }
  const NodeTypeInfo &info = typeInfo(st.type);
  if (!st.args.empty() && isPresetArg(info, st.args.front()))
  {
    st.preset = st.args.front().symbol;
    st.presetQuoted = st.args.front().quoted;
    st.presetSpan = st.args.front().span;
    st.args.erase(st.args.begin());
  }
  for (const Piece &p : splitList(raw, arrow + 2, to))
    st.outputs.push_back({p.text, p.span()});
  return true;
}
}  // namespace

// ---------------------------------------------------------------- public API

bool parseLine(const std::string &line, Statement &out, std::string &error)
{
  out = Statement{};
  out.raw = line;
  if (!out.raw.empty() && out.raw.back() == '\r')
    out.raw.pop_back();
  const std::string &raw = out.raw;

  Piece body = trimmed(raw, 0, raw.size());
  out.indent = raw.substr(0, body.text.empty() ? raw.size() : static_cast<size_t>(body.col0));
  if (body.text.empty())
  {
    out.kind = Statement::Kind::Blank;
    return true;
  }
  if (body.text[0] == '#')
  {
    out.kind = Statement::Kind::Comment;
    return true;
  }
  size_t from = static_cast<size_t>(body.col0), to = static_cast<size_t>(body.col1);

  for (DeclKind k : {DeclKind::In, DeclKind::Out, DeclKind::AIn, DeclKind::AOut})
  {
    std::string kw = std::string(declKeyword(k)) + " ";
    if (!startsWith(body.text, kw.c_str()))
      continue;
    out.kind = Statement::Kind::Decl;
    out.decl = k;
    return parseDecl(raw, from + kw.size(), to, out, error);
  }

  size_t arrow = body.text.find("->");
  if (arrow == std::string::npos)
  {
    out.kind = Statement::Kind::Unknown;
    return true;
  }
  out.kind = Statement::Kind::Gate;
  return parseGate(raw, from, to, from + arrow, out, error);
}

bool parseScript(const std::string &text, Script &out, ParseError &error)
{
  out.lines.clear();
  error = {};
  size_t pos = 0;
  while (pos < text.size())  // same line splitting as std::getline
  {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos)
      end = text.size();
    Statement st;
    std::string msg;
    if (!parseLine(text.substr(pos, end - pos), st, msg))
    {
      error = {static_cast<int>(out.lines.size()), msg};
      return false;
    }
    out.lines.push_back(std::move(st));
    pos = end + 1;
  }
  return true;
}

namespace
{
std::string formatArg(const Arg &a)
{
  std::string s = a.quoted ? "\"" + a.symbol + "\"" : a.symbol;
  return a.mod == Mod::None ? s : std::string(modKeyword(a.mod)) + "(" + s + ")";
}
}  // namespace

std::string formatStatement(const Statement &st)
{
  std::string s = st.indent;
  if (st.kind == Statement::Kind::Decl)
  {
    s += std::string(declKeyword(st.decl)) + " ";
    for (size_t i = 0; i < st.items.size(); ++i)
    {
      const DeclItem &it = st.items[i];
      s += (i ? ", " : "") + it.name + (it.alias.empty() ? "" : "(" + it.alias + ")");
    }
    return s;
  }
  if (st.kind != Statement::Kind::Gate)
    return st.raw;

  s += std::string(typeInfo(st.type).keyword) + " " + st.name + "(";
  bool first = true;
  auto sep = [&] {
    if (!first)
      s += ", ";
    first = false;
  };
  if (st.preset)
  {
    sep();
    s += st.presetQuoted ? "\"" + *st.preset + "\"" : *st.preset;
  }
  for (const Arg &a : st.args)
  {
    sep();
    s += formatArg(a);
  }
  s += ") ->";
  for (size_t i = 0; i < st.outputs.size(); ++i)
    s += (i ? ", " : " ") + st.outputs[i].symbol;
  return s;
}

void refresh(Statement &st)
{
  std::string text = formatStatement(st);
  Statement parsed;
  std::string error;
  if (parseLine(text, parsed, error))
    st = std::move(parsed);
  else
    st.raw = text;  // structured fields are unusable as text; keep what we can
}

std::string toText(const Script &s, const std::string &eol)
{
  std::string out;
  for (size_t i = 0; i < s.lines.size(); ++i)
  {
    if (i)
      out += eol;
    out += s.lines[i].raw;
  }
  return out;
}

std::optional<uint64_t> parseLiteral(std::string_view text)
{
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    text = text.substr(1, text.size() - 2);
  unsigned base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
  {
    base = 16;
    text.remove_prefix(2);
  }
  if (text.empty())
    return std::nullopt;
  uint64_t v = 0;
  for (char c : text)
  {
    unsigned d;
    if (c >= '0' && c <= '9')
      d = static_cast<unsigned>(c - '0');
    else if (base == 16 && c >= 'a' && c <= 'f')
      d = static_cast<unsigned>(c - 'a') + 10;
    else if (base == 16 && c >= 'A' && c <= 'F')
      d = static_cast<unsigned>(c - 'A') + 10;
    else
      return std::nullopt;
    if (v > (UINT64_MAX - d) / base)
      return std::nullopt;  // overflow
    v = v * base + d;
  }
  return v;
}

bool isValidIdentifier(std::string_view s)
{
  if (s.empty() || s.find("->") != std::string_view::npos)
    return false;
  if (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '.' || s[0] == '-')
    return false;
  for (char c : s)
    if (isSpace(c) || std::string_view(",()\"#").find(c) != std::string_view::npos)
      return false;
  return true;
}

}  // namespace gll
