#pragma once
// Structural editing operations on a gll::Script.
//
// The node editor never edits text directly: every canvas / inspector action
// is one of these functions. Each one mutates only the statements it has to
// (re-formatting just those lines), so comments, blank lines and the layout
// of untouched lines in the user's file are preserved.
#include "Gll.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace gll::edits
{

struct DeclRef
{
  int line = -1;
  int item = -1;
};

// Line index of the gate statement called `name`, or -1.
int findGate(const Script &s, std::string_view name);

// Declaration item whose name or alias equals `symbol`.
std::optional<DeclRef> findDecl(const Script &s, std::string_view symbol);

// True if `symbol` is used anywhere (decl name/alias, arg, output) or is a gate name.
bool isNameTaken(const Script &s, std::string_view symbol);

// `base` followed by the lowest number >= 1 that makes it unused (and1, and2 ...).
std::string uniqueName(const Script &s, std::string_view base);

// ---- gates ---------------------------------------------------------------

// Append a new gate statement and return its line. Fixed-arity ports and the
// first two inputs of AND/OR/XOR start as kNotConnected; the gate gets one
// output signal "<name>_Q".
int addGate(Script &s, NodeType type, const std::string &name);

// Remove a gate. References to signals only this gate produced are
// disconnected (unless the signal is a declared IN/OUT/AIN/AOUT).
void deleteGate(Script &s, int line);

void renameGate(Script &s, int line, const std::string &newName);

// TON/TOF time ("500ms") or CTU/CTD preset value ("10"); nullopt removes it.
void setPreset(Script &s, int line, const std::optional<std::string> &preset);

// ---- gate inputs -----------------------------------------------------------

// Connect input `arg` of the gate on `line` to `symbol`. For variadic gates
// arg == args.size() appends a new input. The port's modifier is kept.
void connectInput(Script &s, int line, int arg, const std::string &symbol);

// Disconnect an input. Variadic gates with more than two inputs drop the
// port; otherwise it becomes a plain kNotConnected (modifiers removed, so it
// really reads LOW).
void disconnectInput(Script &s, int line, int arg);

// Replace the modifier chain of an input (outermost first; empty = plain).
void setInputMods(Script &s, int line, int arg, const std::vector<Mod> &mods);
// Single modifier shorthand; Mod::None clears the chain.
void setInputMod(Script &s, int line, int arg, Mod mod);

// Comparator literal ("0x80", "128"), written quoted.
void setInputLiteral(Script &s, int line, int arg, const std::string &literal);

// ---- gate outputs ----------------------------------------------------------

// Output ports: 0 = Q, 1 = CV (counters only).
// Signal name carried by `port`, creating "<name>_Q" / "<name>_CV" if needed.
std::string ensureOutput(Script &s, int line, int port);

// Replace the whole output list (as typed in the inspector).
void setOutputs(Script &s, int line, const std::vector<std::string> &outputs);

// Make `port` of the gate on `line` drive the declared OUT/AOUT `symbol`.
// Any other gate driving `symbol` stops doing so.
void driveTerminal(Script &s, int line, int port, const std::string &symbol);

// Stop the gate on `line` from writing `symbol`.
void undrive(Script &s, int line, const std::string &symbol);

// ---- declarations ------------------------------------------------------------

// Add a declared signal. Appended to the last declaration line of that kind,
// or a new declaration line is inserted after the existing declarations.
DeclRef addDecl(Script &s, DeclKind kind, const std::string &name);

// Remove a declared signal and disconnect everything that used it.
void removeDecl(Script &s, DeclRef ref);

void setDeclAlias(Script &s, DeclRef ref, const std::string &alias);

// ---- signals -----------------------------------------------------------------

// Rename a signal everywhere: declarations, gate inputs and outputs.
void renameSymbol(Script &s, const std::string &from, const std::string &to);

// Disconnect every gate input reading `symbol` and remove it from every
// gate's outputs.
void detachSymbol(Script &s, const std::string &symbol);

}  // namespace gll::edits
