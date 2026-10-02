#pragma once
#include "AST.hpp"
#include <memory>
#include <vector>
#include <unordered_map>

struct Simulator {
  explicit Simulator(std::shared_ptr<const Program> p);
  // Carry runtime state (signal values, timers, counters, buttons, edge
  // memories) over from a simulator of a previous version of the program,
  // matched by signal and node name. Used after every edit / hot reload so
  // changing the circuit does not reset a running simulation.
  void transferStateFrom(const Simulator& old);
  void update(float dt, float simHz, bool running, bool stepOnce);

  // BTN control hooks
  void setMomentary(const std::string& btnName, bool down);
  void toggleLatch(const std::string& btnName);
  // Query BTN state
  bool isButtonPressed(const std::string& btnName) const;
  bool isButtonLatched(const std::string& btnName) const;
  bool getSignalValue(const std::string& signalName) const;
  // Present time control hook, getter and status
  void setPresetTime(const std::string& gateName, float seconds);
  float getPresetTime(const std::string& gateName);
  bool getTGateStatus(const std::string& gateName);
  float getTimerElapsed(const std::string& gateName) const;
  void setTGateStatus(const std::string& gateName, bool status);

  // Counter control hooks
  void setPresetCounterValue(const std::string& gateName, int value);
  int getPresetCounterValue(const std::string& gateName);
  void setCurrentCounterValue(const std::string& gateName, int value);
  int getCurrentCounterValue(const std::string& gateName);

  // Direct signal control (for IN signals without BTN nodes)
  void toggleSignal(const std::string& signalName);
  void setSignal(const std::string& signalName, bool value);
  
  // Analog signal control (for AIN/AOUT signals)
  void setAnalogSignal(const std::string& signalName, uint64_t value);
  uint64_t getAnalogSignalValue(const std::string& signalName) const;
  bool isAnalogSignal(const std::string& signalName) const;
  
  const std::vector<uint64_t>& signals() const { return cur_; }
  int currentEvaluatingLine() const { return curLine_; }
  int currentEvaluatingNode() const { return curNodeIdx_; }
  bool isValidTopology() const { return topo_.size() == prog_.nodes.size(); }
  bool isSteppingThrough() const { return stepping_; }
  uint64_t scanCount() const { return scans_; }

private:
  std::shared_ptr<const Program> progOwner_;
  const Program& prog_;
  uint64_t scans_ = 0;
  std::vector<int> topo_;
  std::vector<uint64_t> cur_, next_;
  float acc_ = 0.f;
  int curLine_ = -1;
  int curNodeIdx_ = -1;       // Current node being evaluated (for visualization)
  int lastVisibleLine_ = -1;  // Last non-internal node line for highlighting
  int lastVisibleNodeIdx_ = -1;
  size_t stepIdx_ = 0;        // Which node in topo_ we're at during slow-step
  bool stepping_ = false;     // Are we in the middle of a slow-step cycle?
  bool hasCycles_ = false;   // Whether the circuit has cycles (unused for execution now)
  std::vector<uint64_t> prevStateAtCycleStart_; // State at start of cycle for UI feedback
  std::unordered_map<int, bool> latch_, momentary_; // by node index
  std::unordered_map<int, bool> pendingLatch_, pendingMomentary_; // buffered inputs
  std::unordered_map<int, uint64_t> pendingSignals_; // buffered signal changes
  std::unordered_map<std::string, float> presentTimeSeconds;
  std::unordered_map<std::string, bool> nodeStatus;
  std::unordered_map<std::string, float> timerElapsedTime; // Elapsed time for each timer (in seconds)
  
  std::unordered_map<std::string, int> presetCounterValue;
  std::unordered_map<std::string, int> currentCounterValue;
  std::unordered_map<std::string, bool> counterPrevInput;
  std::unordered_map<std::string, bool> psPrevInput; // Previous input state for PS (rising edge) nodes
  std::unordered_map<std::string, bool> nsPrevInput; // Previous input state for NS (falling edge) nodes

  void stepOnce_();           // Full step (all nodes at once)
  void stepOneNode_();        // Step single node (for visualization)
  void finishStep_();         // Finish the current step cycle
  void commitPendingInputs_(); // Apply buffered inputs at start of cycle
  bool castSignalToBool_(int sigIdx){
    if (sigIdx < 0 || sigIdx >= static_cast<int>(next_.size())) {
        return false;
      } else {
        return (next_[sigIdx] != 0);
      }
  };
  bool evaluateNode_(int nodeIdx);  // Evaluate a single node
  int findBtnIndex(const std::string& btnName) const;
};

