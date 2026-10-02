#include "LegacyParser.hpp"
#include "TimeUtils.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// V1 parser, kept verbatim as the reference for the equivalence tests.
namespace legacy
{

static void trim(std::string& s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
    return !std::isspace(ch);
  }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
    return !std::isspace(ch);
  }).base(), s.end());
}

static std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    trim(item);
    if (!item.empty()) {
      result.push_back(item);
    }
  }
  return result;
}

static int getOrCreateSignal(Program& prog, const std::string& symbol) {
  auto it = prog.symbolToSignal.find(symbol);
  if (it != prog.symbolToSignal.end()) {
    return it->second;
  }
  int id = static_cast<int>(prog.symbolToSignal.size());
  prog.symbolToSignal[symbol] = id;
  return id;
}

static void addTokenSpan(Program& prog, int line, int col0, int col1, const std::string& symbol) {
  Program::TokenSpan span;
  span.line = line;
  span.col0 = col0;
  span.col1 = col1;
  span.symbol = symbol;
  prog.tokens.push_back(span);
}

// Parse hex value (e.g., "0xFF", "0x10", "255", "0") to integer
// Returns -1 if not a valid number
static int parseHexOrDecimal(const std::string& str) {
  if (str.empty()) return -1;
  
  std::string s = str;
  // Remove quotes if present
  if (s.front() == '"' && s.back() == '"' && s.length() >= 2) {
    s = s.substr(1, s.length() - 2);
  }
  
  try {
    if (s.length() > 2 && (s.substr(0, 2) == "0x" || s.substr(0, 2) == "0X")) {
      // Hex format
      return std::stoi(s.substr(2), nullptr, 16);
    } else {
      // Decimal format
      return std::stoi(s);
    }
  } catch (...) {
    return -1;
  }
}

// Create a constant signal with a fixed value
static int getOrCreateConstantSignal(Program& prog, int value) {
  // Create a unique name for the constant
  std::string constName = "_const_" + std::to_string(value);
  auto it = prog.symbolToSignal.find(constName);
  if (it != prog.symbolToSignal.end()) {
    return it->second;
  }
  int id = static_cast<int>(prog.symbolToSignal.size());
  prog.symbolToSignal[constName] = id;
  prog.analogSignals.insert(id);  // Constants are analog signals
  return id;
}
ParseResult parseFile(const std::string& path, Program& out) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return {false, "Could not open file: " + path};
  }

  out.inputNames.clear();
  out.outputNames.clear();
  out.analogInputNames.clear();
  out.analogOutputNames.clear();
  out.symbolToSignal.clear();
  out.analogSignals.clear();
  out.constantSignalValues.clear();
  out.nodes.clear();
  out.sourceLines.clear();
  out.tokens.clear();

  std::string line;
  int lineNum = 0;

  while (std::getline(file, line)) {
    out.sourceLines.push_back(line);
    trim(line);
    
    if (line.empty() || line[0] == '#') {
      lineNum++;
      continue;
    }

    // Parse IN declaration
    if (line.substr(0, 3) == "IN ") {
      std::string rest = line.substr(3);
      auto items = split(rest, ',');
      size_t searchStart = 3; // Start after "IN "
      for (const auto& item : items) {
        // Check for alias: name(alias)
        size_t parenOpen = item.find('(');
        size_t parenClose = item.find(')');
        
        std::string name = item;
        std::string alias = "";
        
        if (parenOpen != std::string::npos && parenClose != std::string::npos && parenClose > parenOpen) {
          name = item.substr(0, parenOpen);
          alias = item.substr(parenOpen + 1, parenClose - parenOpen - 1);
          trim(name);
          trim(alias);
        }

        int sigId = getOrCreateSignal(out, name);
        if (!alias.empty()) {
          out.symbolToSignal[alias] = sigId;
          out.inputNames.push_back(alias);
        } else {
          out.inputNames.push_back(name);
        }

        // Find with word boundary check
        size_t pos = line.find(item, searchStart);
        if (pos != std::string::npos) {
          addTokenSpan(out, lineNum, static_cast<int>(pos), 
                       static_cast<int>(pos + item.length()), item);
          searchStart = pos + item.length();
        }
      }
      lineNum++;
      continue;
    }

    // Parse OUT declaration
    if (line.substr(0, 4) == "OUT ") {
      std::string rest = line.substr(4);
      auto items = split(rest, ',');
      size_t searchStart = 4; // Start after "OUT "
      for (const auto& item : items) {
        // Check for alias: name(alias)
        size_t parenOpen = item.find('(');
        size_t parenClose = item.find(')');
        
        std::string name = item;
        std::string alias = "";
        
        if (parenOpen != std::string::npos && parenClose != std::string::npos && parenClose > parenOpen) {
          name = item.substr(0, parenOpen);
          alias = item.substr(parenOpen + 1, parenClose - parenOpen - 1);
          trim(name);
          trim(alias);
        }

        int sigId = getOrCreateSignal(out, name);
        if (!alias.empty()) {
          out.symbolToSignal[alias] = sigId;
          out.outputNames.push_back(alias);
        } else {
          out.outputNames.push_back(name);
        }

        // Find with word boundary check
        size_t pos = line.find(item, searchStart);
        if (pos != std::string::npos) {
          addTokenSpan(out, lineNum, static_cast<int>(pos), 
                       static_cast<int>(pos + item.length()), item);
          searchStart = pos + item.length();
        }
      }
      lineNum++;
      continue;
    }

    // Parse AIN (Analog Input) declaration
    if (line.substr(0, 4) == "AIN ") {
      std::string rest = line.substr(4);
      auto items = split(rest, ',');
      size_t searchStart = 4; // Start after "AIN "
      for (const auto& item : items) {
        // Check for alias: name(alias)
        size_t parenOpen = item.find('(');
        size_t parenClose = item.find(')');
        
        std::string name = item;
        std::string alias = "";
        
        if (parenOpen != std::string::npos && parenClose != std::string::npos && parenClose > parenOpen) {
          name = item.substr(0, parenOpen);
          alias = item.substr(parenOpen + 1, parenClose - parenOpen - 1);
          trim(name);
          trim(alias);
        }

        int sigId = getOrCreateSignal(out, name);
        out.analogSignals.insert(sigId);  // Mark as analog signal
        if (!alias.empty()) {
          out.symbolToSignal[alias] = sigId;
          out.analogInputNames.push_back(alias);
        } else {
          out.analogInputNames.push_back(name);
        }

        // Find with word boundary check
        size_t pos = line.find(item, searchStart);
        if (pos != std::string::npos) {
          addTokenSpan(out, lineNum, static_cast<int>(pos), 
                       static_cast<int>(pos + item.length()), item);
          searchStart = pos + item.length();
        }
      }
      lineNum++;
      continue;
    }

    // Parse AOUT (Analog Output) declaration
    if (line.substr(0, 5) == "AOUT ") {
      std::string rest = line.substr(5);
      auto items = split(rest, ',');
      size_t searchStart = 5; // Start after "AOUT "
      for (const auto& item : items) {
        // Check for alias: name(alias)
        size_t parenOpen = item.find('(');
        size_t parenClose = item.find(')');
        
        std::string name = item;
        std::string alias = "";
        
        if (parenOpen != std::string::npos && parenClose != std::string::npos && parenClose > parenOpen) {
          name = item.substr(0, parenOpen);
          alias = item.substr(parenOpen + 1, parenClose - parenOpen - 1);
          trim(name);
          trim(alias);
        }

        int sigId = getOrCreateSignal(out, name);
        out.analogSignals.insert(sigId);  // Mark as analog signal
        if (!alias.empty()) {
          out.symbolToSignal[alias] = sigId;
          out.analogOutputNames.push_back(alias);
        } else {
          out.analogOutputNames.push_back(name);
        }

        // Find with word boundary check
        size_t pos = line.find(item, searchStart);
        if (pos != std::string::npos) {
          addTokenSpan(out, lineNum, static_cast<int>(pos), 
                       static_cast<int>(pos + item.length()), item);
          searchStart = pos + item.length();
        }
      }
      lineNum++;
      continue;
    }

    // Parse gate: <GATETYPE> <name>(args...) -> output
    size_t arrowPos = line.find("->");
    if (arrowPos == std::string::npos) {
      lineNum++;
      continue;
    }

    std::string beforeArrow = line.substr(0, arrowPos);
    std::string afterArrow = line.substr(arrowPos + 2);
    trim(beforeArrow);
    trim(afterArrow);

    // Extract gate type and name
    size_t spacePos = beforeArrow.find(' ');
    if (spacePos == std::string::npos) {
      return {false, "Line " + std::to_string(lineNum + 1) + ": Invalid gate syntax"};
    }

    std::string gateType = beforeArrow.substr(0, spacePos);
    std::string gateName = beforeArrow.substr(spacePos + 1);
    
    size_t parenPos = gateName.find('(');
    if (parenPos == std::string::npos) {
      return {false, "Line " + std::to_string(lineNum + 1) + ": Missing '(' in gate definition"};
    }

    std::string name = gateName.substr(0, parenPos);
    std::string argsStr = gateName.substr(parenPos + 1);
    
    // Find matching closing paren (handle nested parens from NOT())
    int depth = 1;
    size_t closeParen = 0;
    while (closeParen < argsStr.size() && depth > 0) {
      if (argsStr[closeParen] == '(') depth++;
      else if (argsStr[closeParen] == ')') depth--;
      if (depth > 0) closeParen++;
    }
    if (depth != 0) {
      return {false, "Line " + std::to_string(lineNum + 1) + ": Missing ')' in gate definition"};
    }
    argsStr = argsStr.substr(0, closeParen);

    // Parse gate type
    Program::Node::Type type;
    if (gateType == "AND") {
      type = Program::Node::AND_;
    } else if (gateType == "OR") {
      type = Program::Node::OR_;
    } else if (gateType == "XOR") {
      type = Program::Node::XOR_;
    } else if (gateType == "NOT") {
      type = Program::Node::NOT_;
    } else if (gateType == "PS") {
      type = Program::Node::PS_;
    } else if (gateType == "NS") {
      type = Program::Node::NS_;
    } else if (gateType == "SR") {
      type = Program::Node::SR_;
    } else if (gateType == "RS") {
      type = Program::Node::RS_;
    } else if (gateType == "TON") {
      type = Program::Node::TON_;
    } else if (gateType == "TOF") {
      type = Program::Node::TOF_;
    } else if (gateType == "CTU") {
      type = Program::Node::CTU_;
    } else if (gateType == "CTD") {
      type = Program::Node::CTD_;
    } else if (gateType == "LT") {
      type = Program::Node::LT_;
    } else if (gateType == "GT") {
      type = Program::Node::GT_;
    } else if (gateType == "EQ") {
      type = Program::Node::EQ_;
    } else if (gateType == "BTN") {
      type = Program::Node::BTN;
    } else {
      return {false, "Line " + std::to_string(lineNum + 1) + ": Unknown gate type: " + gateType};
    }

    // Create node
    Program::Node node;

    // Parse inputs (handle NOT(...) syntax)
    std::vector<int> inputs;
    std::vector<std::string> inputSymbols;
    
    // First, find original token positions in the source line BEFORE processing
    // We need to find the arguments section in the original line
    size_t argsStartInLine = line.find('(') + 1;
    
    if (!argsStr.empty()) {
      // Handle NOT(...) in arguments - find matching parenthesis properly
      std::string processed = argsStr;
      size_t notPos = processed.find("NOT(");
      while (notPos != std::string::npos) {
        // Find the matching closing paren by counting depth
        int depth = 1;
        size_t notEnd = notPos + 4; // Start after "NOT("
        while (notEnd < processed.size() && depth > 0) {
          if (processed[notEnd] == '(') depth++;
          else if (processed[notEnd] == ')') depth--;
          if (depth > 0) notEnd++;
        }
        
        if (depth != 0) {
          return {false, "Line " + std::to_string(lineNum + 1) + ": Unmatched NOT("};
        }
        
        std::string notArg = processed.substr(notPos + 4, notEnd - notPos - 4);
        trim(notArg);
        
        // Add token span for the argument inside NOT() - find it in original line
        size_t notInOriginal = argsStr.find("NOT(");
        if (notInOriginal != std::string::npos) {
          size_t argInOriginal = argsStr.find(notArg, notInOriginal + 4);
          if (argInOriginal != std::string::npos) {
            int col0 = static_cast<int>(argsStartInLine + argInOriginal);
            addTokenSpan(out, lineNum, col0, col0 + static_cast<int>(notArg.length()), notArg);
          }
        }
        
        // Create a NOT node for this
        Program::Node notNode;
        notNode.type = Program::Node::NOT_;
        notNode.name = "_not_" + std::to_string(out.nodes.size());
        int notInputSig = getOrCreateSignal(out, notArg);
        notNode.inputs.push_back(notInputSig);
        std::string notOutputName = "_not_" + std::to_string(out.nodes.size()) + "_out";
        int notOutputSig = getOrCreateSignal(out, notOutputName);
        notNode.outputs.push_back(notOutputSig);
        notNode.sourceLine = lineNum;
        out.nodes.push_back(notNode);
        
        // Replace NOT(...) with the output signal name in processed string
        processed.replace(notPos, notEnd - notPos + 1, notOutputName);
        notPos = processed.find("NOT(");
      }

      // Handle PS(...) in arguments - Positive Signal (Rising Edge) inline
      size_t psPos = processed.find("PS(");
      while (psPos != std::string::npos) {
        // Find the matching closing paren by counting depth
        int depth = 1;
        size_t psEnd = psPos + 3; // Start after "PS("
        while (psEnd < processed.size() && depth > 0) {
          if (processed[psEnd] == '(') depth++;
          else if (processed[psEnd] == ')') depth--;
          if (depth > 0) psEnd++;
        }
        
        if (depth != 0) {
          return {false, "Line " + std::to_string(lineNum + 1) + ": Unmatched PS("};
        }
        
        std::string psArg = processed.substr(psPos + 3, psEnd - psPos - 3);
        trim(psArg);
        
        // Add token span for the argument inside PS() - find it in original line
        size_t psInOriginal = argsStr.find("PS(");
        if (psInOriginal != std::string::npos) {
          size_t argInOriginal = argsStr.find(psArg, psInOriginal + 3);
          if (argInOriginal != std::string::npos) {
            int col0 = static_cast<int>(argsStartInLine + argInOriginal);
            addTokenSpan(out, lineNum, col0, col0 + static_cast<int>(psArg.length()), psArg);
          }
        }
        
        // Create a PS node for this (rising edge detector)
        Program::Node psNode;
        psNode.type = Program::Node::PS_;
        psNode.name = "_ps_" + std::to_string(out.nodes.size());
        int psInputSig = getOrCreateSignal(out, psArg);
        psNode.inputs.push_back(psInputSig);
        std::string psOutputName = "_ps_" + std::to_string(out.nodes.size()) + "_out";
        int psOutputSig = getOrCreateSignal(out, psOutputName);
        psNode.outputs.push_back(psOutputSig);
        psNode.sourceLine = lineNum;
        out.nodes.push_back(psNode);
        
        // Replace PS(...) with the output signal name in processed string
        processed.replace(psPos, psEnd - psPos + 1, psOutputName);
        psPos = processed.find("PS(");
      }

      // Handle NS(...) in arguments - Negative Signal (Falling Edge) inline
      size_t nsPos = processed.find("NS(");
      while (nsPos != std::string::npos) {
        // Find the matching closing paren by counting depth
        int depth = 1;
        size_t nsEnd = nsPos + 3; // Start after "NS("
        while (nsEnd < processed.size() && depth > 0) {
          if (processed[nsEnd] == '(') depth++;
          else if (processed[nsEnd] == ')') depth--;
          if (depth > 0) nsEnd++;
        }
        
        if (depth != 0) {
          return {false, "Line " + std::to_string(lineNum + 1) + ": Unmatched NS("};
        }
        
        std::string nsArg = processed.substr(nsPos + 3, nsEnd - nsPos - 3);
        trim(nsArg);
        
        // Add token span for the argument inside NS() - find it in original line
        size_t nsInOriginal = argsStr.find("NS(");
        if (nsInOriginal != std::string::npos) {
          size_t argInOriginal = argsStr.find(nsArg, nsInOriginal + 3);
          if (argInOriginal != std::string::npos) {
            int col0 = static_cast<int>(argsStartInLine + argInOriginal);
            addTokenSpan(out, lineNum, col0, col0 + static_cast<int>(nsArg.length()), nsArg);
          }
        }
        
        // Create a NS node for this (falling edge detector)
        Program::Node nsNode;
        nsNode.type = Program::Node::NS_;
        nsNode.name = "_ns_" + std::to_string(out.nodes.size());
        int nsInputSig = getOrCreateSignal(out, nsArg);
        nsNode.inputs.push_back(nsInputSig);
        std::string nsOutputName = "_ns_" + std::to_string(out.nodes.size()) + "_out";
        int nsOutputSig = getOrCreateSignal(out, nsOutputName);
        nsNode.outputs.push_back(nsOutputSig);
        nsNode.sourceLine = lineNum;
        out.nodes.push_back(nsNode);
        
        // Replace NS(...) with the output signal name in processed string
        processed.replace(nsPos, nsEnd - nsPos + 1, nsOutputName);
        nsPos = processed.find("NS(");
      }
      
      // Now split by comma and add non-internal signals
      auto argList = split(processed, ',');
      int argIdx = 0;
      for (const auto& arg : argList) {
        // Special handling for TON/TOF first argument as hardcoded time
        if (argIdx == 0 && (type == Program::Node::TON_ || type == Program::Node::TOF_)) {
          // Check if it's a quoted string or looks like a time (starts with digit or dot)
          if (!arg.empty() && ((arg.front() == '"' && arg.back() == '"') || (std::isdigit(arg.front())) || (arg.front() == '.'))) {
            std::string timeStr = arg;
            if (timeStr.front() == '"') {
                timeStr = timeStr.substr(1, timeStr.length() - 2);
            }
            node.hardcodedPresetTime = parseTimeStringToFloat(timeStr);
            argIdx++;
            continue; // Skip adding this as a signal input
          }
        }

        // Special handling for CTU/CTD arguments (PV)
        if (argIdx == 0 && (type == Program::Node::CTU_ || type == Program::Node::CTD_)) {
          if (!arg.empty() && ((arg.front() == '"' && arg.back() == '"') || (std::isdigit(arg.front())) || (arg.size() > 1 && arg.front() == '-'))) {
            std::string valStr = arg;
            if (valStr.front() == '"') valStr = valStr.substr(1, valStr.length() - 2);
            try {
              // Try to parse as int
              int val = std::stoi(valStr);
              node.hardcodedPresetValue = val;
              argIdx++;
              continue;
            } catch (...) {
              // If not a number, fall through to signal handling
            }
          }
        }

        argIdx++;
        
        // For comparators (LT, GT, EQ), check if this argument is a hex/decimal literal
        bool isLiteral = false;
        if (type == Program::Node::LT_ || type == Program::Node::GT_ || type == Program::Node::EQ_) {
          int constVal = parseHexOrDecimal(arg);
          if (constVal >= 0 && constVal <= 255) {
            isLiteral = true;
            int sigId = getOrCreateConstantSignal(out, constVal);
            out.constantSignalValues[sigId] = constVal;
            inputs.push_back(sigId);
            // Don't add to inputSymbols for literals
          }
        }
        
        if (!isLiteral) {
          inputSymbols.push_back(arg);
          int sigId = getOrCreateSignal(out, arg);
          inputs.push_back(sigId);
        }
        
        // Only add token span for non-internal signals (not _not_X_out, _ps_X_out, _ns_X_out, or _const_X)
        if (arg.find("_not_") != 0 && arg.find("_ps_") != 0 && arg.find("_ns_") != 0 && arg.find("_const_") != 0 && !isLiteral) {
          // Find this exact token in original argsStr using word boundaries
          size_t pos = 0;
          while (pos < argsStr.size()) {
            size_t found = argsStr.find(arg, pos);
            if (found == std::string::npos) break;
            
            // Check word boundaries
            bool startOk = (found == 0 || !std::isalnum(argsStr[found - 1]));
            bool endOk = (found + arg.size() >= argsStr.size() || 
                         !std::isalnum(argsStr[found + arg.size()]));
            
            if (startOk && endOk) {
              int col0 = static_cast<int>(argsStartInLine + found);
              addTokenSpan(out, lineNum, col0, col0 + static_cast<int>(arg.length()), arg);
              break;
            }
            pos = found + 1;
          }
        }
      }
    }

    // Parse outputs (comma-separated) - find with word boundaries
    std::vector<int> outputSigs;
    auto outputNames = split(afterArrow, ',');
    size_t searchStart = arrowPos + 2; // Start after "->"
    int outputIdx = 0;
    for (const auto& outputName : outputNames) {
      int outputSig = getOrCreateSignal(out, outputName);
      
      // For CTU/CTD: second output is CV (counter value) output
      if ((type == Program::Node::CTU_ || type == Program::Node::CTD_) && outputIdx == 1) {
        node.cvOutputSignal = outputSig;
      } else {
        outputSigs.push_back(outputSig);
      }
      
      // Find this output name in the original line
      size_t outPos = line.find(outputName, searchStart);
      if (outPos != std::string::npos) {
        addTokenSpan(out, lineNum, static_cast<int>(outPos), 
                     static_cast<int>(outPos + outputName.length()), outputName);
        searchStart = outPos + outputName.length();
      }
      outputIdx++;
    }

    // Create node
    node.type = type;
    node.name = name;
    node.inputs = inputs;
    node.outputs = outputSigs;
    node.sourceLine = lineNum;
    out.nodes.push_back(node);

    lineNum++;
  }

  return {true, ""};
}

}  // namespace legacy
