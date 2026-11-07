#pragma once
// GLL compiler: turns the syntax layer (gll::Script) into a Program that the
// Simulator can execute. Semantics are identical to the V1 single-pass parser.
#include "AST.hpp"
#include "Gll.hpp"
#include <string>

struct ParseResult
{
  bool ok;
  std::string msg;
  int line = -1;  // 0-based line of the error, -1 if not line specific
};

// Compile an already parsed script. `out` is reset first.
ParseResult compile(const gll::Script &script, Program &out);

// Convenience: read + parseScript + compile.
ParseResult parseFile(const std::string &path, Program &out);
