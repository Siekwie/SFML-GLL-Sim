#pragma once
// V1 (pre node-editor) GLL parser, used only by the tests as reference semantics.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace legacy
{

struct Program
{
  std::vector<std::string> inputNames, outputNames;
  std::vector<std::string> analogInputNames, analogOutputNames;  // Analog I/O signals
  std::unordered_map<std::string, int> symbolToSignal;
  std::unordered_set<int> analogSignals;  // Set of signal IDs that are analog (not boolean)
  std::unordered_map<int, int> constantSignalValues;  // Signal ID -> constant value (for hex literals)

  struct TokenSpan
  {
    int line, col0, col1;
    std::string symbol;
  };
  std::vector<TokenSpan> tokens;

  struct Node
  {
    enum Type
    {
      AND_,
      OR_,
      XOR_,
      NOT_,
      PS_,
      NS_,
      SR_,
      RS_,
      TON_,
      TOF_,
      CTU_,
      CTD_,
      LT_,
      GT_,
      EQ_,
      BTN
    } type;
    std::string name;
    std::vector<int> inputs;
    std::vector<int> outputs;
    int sourceLine;
    float hardcodedPresetTime = -1.0f;
    int hardcodedPresetValue = -1;
    int cvOutputSignal = -1;  // For counters: optional second output to expose CV value
  };
  std::vector<Node> nodes;
  std::vector<std::string> sourceLines;
};

struct ParseResult
{
  bool ok;
  std::string msg;
};

ParseResult parseFile(const std::string &path, Program &out);

}  // namespace legacy
