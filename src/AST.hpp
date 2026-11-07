#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Compiled, simulation-ready form of a GLL script (see Gll.hpp for the
// editable syntax layer this is compiled from).
struct Program
{
  std::vector<std::string> inputNames, outputNames;
  std::vector<std::string> analogInputNames, analogOutputNames;  // Analog I/O signals
  std::unordered_map<std::string, int> symbolToSignal;
  int signalCount = 0;  // number of distinct signal ids (aliases share an id)
  std::unordered_set<int> analogSignals;  // Set of signal IDs that are analog (not boolean)
  std::unordered_map<int, uint64_t> constantSignalValues;  // Signal ID -> constant value (for literals)

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
    int sourceLine = -1;
    bool internal = false;  // auto-generated for inline NOT()/PS()/NS()
    float hardcodedPresetTime = -1.0f;
    int hardcodedPresetValue = -1;
    int cvOutputSignal = -1;  // For counters: optional second output to expose CV value
  };
  std::vector<Node> nodes;
  std::vector<std::string> sourceLines;
};
