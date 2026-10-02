# GLL (`Gate logic Language`) Reference document

> Disclaimer: This is a work of art, and hobbyism. I'm not following any norms, unless they are actually making sense. This is not ment for any kind of production use. Be ready to see behavior that may not be what you expect.

## Overview

`GLL` is a simple language for describing logic circuits. It is designed to be easy to read and write, and to be used for prototyping and educational purposes.

## Syntax

### **Comments**

Comments start with a `#` and continue to the end of the line. Multi-line comments are yet to be implemented.

```
# This is a comment
```

### **Input and output signals**

Input and output signals are declared using the `IN` and `OUT` keywords, respectively. Each signal is declared with a symbol-name, in a comma-separated list of input and output names. Read more about multiple outputs [here](#multiple-outputs).
Every Input or Output signal must be declared here accept if its used temporarily once in the circuit.

```
IN a, b, c
OUT x, y, z
```

### **Analog Input and Output signals**

Analog inputs and outputs are declared using the `AIN` and `AOUT` keywords. Unlike digital signals (which are boolean 0/1), analog signals hold integer values. The range depends on the configured Modbus register mode: 16-bit mode supports 0-65535, while 32-bit mode supports 0-4,294,967,295.

```
AIN sensorValue, temperature
AOUT motorSpeed, valvePosition
```

**All integer values are compatible:** Counter CV outputs, analog signals (AIN/AOUT), and literal values are all stored internally as 64-bit integers. This means you can freely compare them against each other using the comparators (LT, GT, EQ). The actual range depends on the Modbus register mode (16-bit or 32-bit).

```
AIN sensorValue
IN countPulse, reset
OUT sensorAboveCount, sensorBelowThreshold, sensorMatchesCount

# Counter with CV output
CTU pulseCounter("100", countPulse, reset) -> counterDone, counterCV

# Compare analog input against counter CV value
GT sensorVsCounter(sensorValue, counterCV) -> sensorAboveCount

# Compare analog input against hex literal
LT belowThreshold(sensorValue, "0x80") -> sensorBelowThreshold

# Compare analog input against counter value for equality
EQ matchesCount(sensorValue, counterCV) -> sensorMatchesCount
```

**UI Interaction:**

- Analog input widgets display values in both hex and decimal (e.g., `7F (127)`)
- Click on an AIN widget to edit the value manually (type raw hex digits or decimal)
- HEX/DEC toggle button to switch display and input mode

**Literal Syntax for Comparators:**
You can use hex or decimal literals in comparator arguments (the full 32-bit range since V2; V1 only accepted 0-255):

- `"00000010"` - any hex value
- `"128"` - decimal format also supported

### **Nodes**

Nodes are used to descibe the logic operations in the circut. Each gate is declared with a type, a name, a parenthesized list of inputs, and a comma-separated list of outputs, coming after the `->`.

The currently supported nodes are:

#### `AND`

The `AND` gate is used to compute the logical `AND` of its inputs. The output is `true` if and only if all inputs are `true`.

```
AND gate1(a, b) -> c
```

**Truth table**:
| A | B | C |
| --- | --- | --- |
| 0 | 0 | 0 |
| 0 | 1 | 0 |
| 1 | 0 | 0 |
| 1 | 1 | 1 |

#### `OR`

The `OR` gate is used to compute the logical `OR` of its inputs. The output is `true` if at least one input is `true`. If no inputs are `true`, the output is `false`.

```
OR gate2(a, b) -> c
```

**Truth table**:
| A | B | C |
| --- | --- | --- |
| 0 | 0 | 0 |
| 0 | 1 | 1 |
| 1 | 0 | 1 |
| 1 | 1 | 1 |

#### `XOR`

The `XOR` gate is used to compute the logical `XOR` of its inputs. The output is `true` if the inputs are different, and `false` if the inputs are the same.

```
XOR gate2(a, b) -> c
```

**Truth table**:
| A | B | C |
| --- | --- | --- |
| 0 | 0 | 0 |
| 0 | 1 | 1 |
| 1 | 0 | 1 |
| 1 | 1 | 0 |

#### `NOT`

The `NOT` gate is used to compute the logical `NOT` of its input. The output is `true` if the input is `false`, and `false` if the input is `true`. Basically, it inverts the input signal. A `NOT` gate should be defined inline as in the example below.

```
AND gate3(a, NOT(b)) -> c
```

This makes the circuit "code" more dense and easier/natural to read.

**Nesting**: inline modifiers can be nested, and they apply from the inside out:

```
# HIGH except during the one scan in which `a` rises
OR notRising(NOT(PS(a))) -> x
```

`NOT(PS(a))` first detects the rising edge of `a`, then inverts it. Any combination and depth works (`NOT(NS(a))`, `PS(NOT(a))`, ...) and behaves exactly like the same chain built from standalone `NOT` / `PS` / `NS` nodes. (V1 only got this right when the inner modifier happened to be processed first; `NOT(PS(a))` silently turned into a constant HIGH there.)

Inline modifiers are separate internal nodes evaluated on every scan, before the gate that uses them. An edge detector therefore always tracks its input, even while the gate's other inputs make the result irrelevant: in `AND y(b, PS(a))`, a rise of `a` that happens while `b` is LOW is consumed in that scan and does not fire later when `b` goes HIGH.

**Truth table**:
| B | X |
| --- | --- |
| 0 | 1 |
| 1 | 0 |

#### `PS`

The `PS` (Positive Signal) gate is a **rising edge detector**. It outputs `true` only when its input transitions from `false` to `true`. If the input stays `true`, the output is `false` on subsequent evaluations. This is commonly used in PLC programming to detect button presses or trigger one-shot events.

Like `NOT`, a `PS` gate can be used inline:

```
AND gate4(PS(a), b) -> c
```

Or as a standalone node:

```
PS myEdge(input_signal) -> edge_detected
```

**Behavior**:

- Input goes from LOW to HIGH -> Output is HIGH (for one scan cycle)
- Input stays HIGH -> Output is LOW
- Input goes from HIGH to LOW -> Output is LOW
- Input stays LOW -> Output is LOW

**Truth table** (with previous state):
| Previous | Current | Output |
| --- | --- | --- |
| 0 | 0 | 0 |
| 0 | 1 | 1 |
| 1 | 0 | 0 |
| 1 | 1 | 0 |

#### `NS`

The `NS` (Negative Signal) gate is a **falling edge detector**. It outputs `true` only when its input transitions from `true` to `false`. If the input stays `false`, the output is `false` on subsequent evaluations. This is the inverse of `PS` and is commonly used to detect release events or falling transitions.

Like `NOT` and `PS`, a `NS` gate can be used inline:

```
AND gate5(NS(a), b) -> c
```

Or as a standalone node:

```
NS myFallingEdge(input_signal) -> edge_detected
```

**Behavior**:

- Input goes from HIGH to LOW -> Output is HIGH (for one scan cycle)
- Input stays LOW -> Output is LOW
- Input goes from LOW to HIGH -> Output is LOW
- Input stays HIGH -> Output is LOW

**Truth table** (with previous state):
| Previous | Current | Output |
| --- | --- | --- |
| 0 | 0 | 0 |
| 0 | 1 | 0 |
| 1 | 0 | 1 |
| 1 | 1 | 0 |

#### `SR`

The `SR` gate is used to compute a `set/reset` position of its inputs. The reset priority in the `SR` variant in `GLL` differs from most known Norms, where for what ever reason the reset priority is contridictant to the naming, in `GLL` that is not the case. The input on `S` overrides the reset on `R`. Meaning if both inputs are `true`, the output is `true`.

- Inputs: `S` (set), `R` (reset)

- Outputs: `Q`

example:

```
SR setBlock(a, b) -> c
```

**Truth table**:
| S | R | Q |
| --- | --- | --- |
| 0 | 0 | Ø |
| 0 | 1 | 0 |
| 1 | 0 | 1 |
| 1 | 1 | 1 |

#### `RS`

The `RS` gate is used to compute a `set/reset` position of its inputs. The reset priority in the `RS` variant in `GLL` differs from most known Norms, where for what ever reason the reset priority is in contradiction to the naming, in `GLL` that is not the case. The input on `S` is overriden by the reset on `R`. Meaning if both inputs are `true`, the output is `false`, due to the dominant `R` input resetting.

- Inputs: `S` (set), `R` (reset)

- Outputs: `Q`

example:

```
RS setBlock(a, b) -> c
```

**Truth table**:
| S | R | Q |
| --- | --- | --- |
| 0 | 0 | Ø |
| 0 | 1 | 0 |
| 1 | 0 | 1 |
| 1 | 1 | 0 |

#### `TON`

The `TON` gate is a timer on delay. When the input goes HIGH, the timer starts counting. The output goes HIGH only after the input has been HIGH for the preset time (PT) duration. If the input goes LOW before the preset time elapses, the timer resets and the output remains LOW.

- Inputs: `IN` (input signal)

- Outputs: `Q`

- Preset Time (PT): Configured in the UI sidebar for each TON gate, or **hardcoded** in the GLL file as the first argument. Click the timer widget to edit. Supports formats like `500ms` (milliseconds), `10s` (seconds), `2m` (minutes), `1h` (hours).

**Hardcoded Example**:

```
# The first argument is a string or number with unit (fixed time)
TON delay_on("400ms", start) -> delayed_output
```

If a hardcoded time is provided, it overrides defaults and cannot be changed in the UI.

example:

```
TON delay_on(start) -> delayed_output
```

**Behavior**:

- Input goes HIGH → Timer starts counting
- Input stays HIGH for PT duration → Output goes HIGH
- Input goes LOW before PT duration → Timer resets, output stays LOW
- Input goes LOW after output is HIGH → Output goes LOW immediately

#### `TOF`

The `TOF` gate is a timer off delay. When the input goes LOW, the timer starts counting. The output stays HIGH for the preset time (PT) duration after the input goes LOW, then goes LOW. If the input goes HIGH again before the timer expires, the timer resets and the output stays HIGH.

- Inputs: `IN` (input signal)

- Outputs: `Q`

- Preset Time (PT): Configured in the UI sidebar for each TOF gate, or **hardcoded** in the GLL file as the first argument. Click the timer widget to edit. Supports formats like `500ms` (milliseconds), `10s` (seconds), `2m` (minutes), `1h` (hours).

**Hardcoded Example**:

```
# Fixed 2 second off-delay
TOF delay_off("2s", stop) -> delayed_output
```

If a hardcoded time is provided, it overrides defaults and cannot be changed in the UI.

example:

```
TOF delay_off(stop) -> delayed_output
```

**Behavior**:

- Input goes HIGH → Output goes HIGH immediately
- Input goes LOW → Timer starts counting, output stays HIGH
- Timer expires (PT duration) → Output goes LOW
- Input goes HIGH again before timer expires → Timer resets, output stays HIGH

#### `CTU`

The `CTU` gate is a count-up counter. It increases its current value (CV) on every positive edge of the count-up (CU) input signal. When CV is greater than or equal to the preset value (PV), the output (Q) goes HIGH.

- Inputs: `CU` (count up), `R` (reset)
- Outputs: `Q` (done), optionally `CV` (counter value)
- Configurable Variables: `PV` (Preset Value) which can be edited in the UI sidebar by clicking the counter widget, or **hardcoded** in the GLL file as the first argument. `CV` (Current Value) is internal and updated automatically.

**Hardcoded Example**:

```
# Fixed PV of 10
CTU myCounter("10", input, reset) -> done
```

**CV Output Example**:

You can extract the counter value (CV) as a second output. This allows you to use the counter value with comparators (LT, GT, EQ) or pass it to other nodes.

```
# done goes HIGH when CV >= 10, counterValue holds the actual count
CTU myCounter("10", input, reset) -> done, counterValue

# Compare counter value with another signal
GT isAboveFive(counterValue, threshold) -> aboveThreshold
```

**Behavior**:

- Positive edge on `CU` → `CV` increases by 1 (max 32767)
- `R` is HIGH → `CV` resets to 0
- `CV >= PV` → `Q` is HIGH, otherwise LOW

#### `CTD`

The `CTD` gate is a count-down counter. It decreases its current value (CV) on every positive edge of the count-down (CD) input signal. When CV is less than or equal to 0, the output (Q) goes HIGH.

- Inputs: `CD` (count down), `LD` (load)
- Outputs: `Q` (done), optionally `CV` (counter value)
- Configurable Variables: `PV` (Preset Value) which can be edited in the UI sidebar by clicking the counter widget, or **hardcoded** in the GLL file as the first argument. `CV` (Current Value) is internal and updated automatically.

**Hardcoded Example**:

```
# Fixed PV of 5
CTD myCounter("5", input, load) -> done
```

**CV Output Example**:

You can extract the counter value (CV) as a second output. This allows you to use the counter value with comparators (LT, GT, EQ) or pass it to other nodes.

```
# done goes HIGH when CV <= 0, counterValue holds the actual count
CTD myCounter("10", input, load) -> done, counterValue
```

**Behavior**:

- Positive edge on `CD` → `CV` decreases by 1 (stops at 0)
- `LD` is HIGH → `CV` is set to `PV`
- `CV <= 0` → `Q` is HIGH, otherwise LOW

#### `LT`

The `LT` (Less Than) gate is a **comparator**. It compares two integer values and outputs `true` if the first value is less than the second. This is primarily designed to work with CV (Counter Value) outputs from CTU/CTD counters.

- Inputs: `A`, `B` (integer values, typically from CV outputs)
- Outputs: `Q` (true if A < B)

```
LT compare(valueA, valueB) -> isLess
```

**Behavior**:

- `A < B` → `Q` is HIGH
- `A >= B` → `Q` is LOW

#### `GT`

The `GT` (Greater Than) gate is a **comparator**. It compares two integer values and outputs `true` if the first value is greater than the second.

- Inputs: `A`, `B` (integer values, typically from CV outputs)
- Outputs: `Q` (true if A > B)

```
GT compare(valueA, valueB) -> isGreater
```

**Behavior**:

- `A > B` → `Q` is HIGH
- `A <= B` → `Q` is LOW

#### `EQ`

The `EQ` (Equal) gate is a **comparator**. It compares two integer values and outputs `true` if they are equal.

- Inputs: `A`, `B` (integer values, typically from CV outputs)
- Outputs: `Q` (true if A == B)

```
EQ compare(valueA, valueB) -> isEqual
```

**Behavior**:

- `A == B` → `Q` is HIGH
- `A != B` → `Q` is LOW

**Comparator Example with Counters**:

```
# Two counters with CV outputs
CTU counterA("10", inputA, resetA) -> doneA, cvA
CTU counterB("10", inputB, resetB) -> doneB, cvB

# Compare the counter values
LT aLessThanB(cvA, cvB) -> isLess
GT aGreaterThanB(cvA, cvB) -> isGreater
EQ aEqualsB(cvA, cvB) -> isEqual
```

#### `BTN` (Work In Progress)

`BTN` nodes represent physical buttons in the simulation. They can be momentary or latched.

- Outputs: `Q` (high when pressed/latched)

example:

```
BTN myButton() -> is_pressed
```

### Integration

#### **Modbus TCP**

GLL supports Modbus TCP as a client. It can synchronize its internal signals with a remote Modbus server (like from Factory I/O).

**Digital I/O (Boolean signals):**

- **Inputs**: Signals named `INPUT_0`, `INPUT_1`, ..., `INPUT_N` are automatically mapped to Modbus Discrete Inputs (bits).
- **Outputs**: Signals named `OUTPUT_0`, `OUTPUT_1`, ..., `OUTPUT_N` are automatically mapped to Modbus Coils (bits).

**Analog I/O (Register values):**

GLL supports two analog register modes for compatibility with different PLCs:

**16-bit Mode** (Default)

- Range: 0 to 65,535
- 1 Modbus register per analog signal
- Compatible with most PLCs and legacy systems

**32-bit Mode** (High Precision)

- Range: 0 to 4,294,967,295
- 2 Modbus registers per analog signal (big-endian format)
- For advanced applications requiring extended range

**Signal Mapping:**

- **Analog Inputs**: Signals named `AINPUT_0`, `AINPUT_1`, ..., `AINPUT_N` are mapped to Modbus Input Registers.
- **Analog Outputs**: Signals named `AOUTPUT_0`, `AOUTPUT_1`, ..., `AOUTPUT_N` are mapped to Modbus Holding Registers.

In 32-bit mode, each analog signal uses 2 consecutive registers:

- `AINPUT_0` uses Input Registers 0-1
- `AINPUT_1` uses Input Registers 2-3
- `AOUTPUT_0` uses Holding Registers 0-1
- etc.

**Example with Modbus analog I/O:**

```
# Declare analog inputs/outputs with Modbus-compatible names
AIN AINPUT_0(sensorValue), AINPUT_1(temperature)
AOUT AOUTPUT_0(motorSpeed), AOUTPUT_1(valvePos)

# Use comparators with the analog signals
GT tempHigh(temperature, "0x80") -> overheated
LT tempLow(temperature, "0x10") -> tooCold
```

Mapping configuration (IP, Port, Slave ID, Bit Counts, Analog Register Counts, and Register Mode) can be adjusted in the **Settings** menu. These settings are saved to `modbus_config.txt`.

### **Node Editor (V2)**

Since V2 the simulator is a visual node editor. The `.gll` file stays the single source of truth: every change made on the canvas or in the inspector is written straight back into the file as ordinary GLL, and every change made to the file in another editor shows up on the canvas immediately. You can work in either, or both side by side.

- **Canvas**: every gate statement is a node; every declared `IN`, `OUT`, `AIN` and `AOUT` signal is a terminal. Wires run from the node that writes a signal to every node that reads it. Wires and port dots are green when HIGH, grey/red when LOW and orange for analog values.
- **Dashed wires** are read *before* they are written in the scan (a forward reference, see [Execution Model](#execution-model)); the value arrives one scan later. This is normal for latches and step sequences.
- **Palette** (left): drag a node onto the canvas, or click it to drop it in the middle of the view. Right-clicking the canvas offers the same list.
- **Connecting**: drag from an output port to an input port (or the other way round). Dropping onto a node body uses its first free input. Dragging a connected input away unplugs it; dropping a wire on empty canvas offers to create a new node that is connected right away.
- **Inputs of AND/OR/XOR** grow: drop a wire on the `+` port to add one.
- **Input modifiers**: right-click an input port (or use the chips in the inspector) to make it inverted `NOT(x)`, rising edge `PS(x)`, falling edge `NS(x)`, or an inverted edge `NOT(PS(x))` / `NOT(NS(x))`. The port shows a bubble for NOT and a triangle for each edge, innermost next to the port. Other chains written in the code (e.g. `PS(NOT(x))`) are shown as text in the inspector.
- **Inspector** (right): name, presets (`PT`, `PV`), every input as a text field (type a signal name or, for comparators, a constant such as `0x80`), output signal names and the live state. Renaming an output renames the signal everywhere, so wires stay attached.
- **Code panel** (right, below the inspector): the file with live signal colours and the line being evaluated. Clicking a line selects its node and vice versa.
- **Notes**: with nothing selected the inspector lists things worth a look — undriven outputs, signals nothing drives, unconnected inputs.
- **Undo / redo** cover every edit and node move (`Ctrl+Z` / `Ctrl+Y`).
- **Layout**: node positions are stored next to the circuit in `<file>.gll.layout`, so the `.gll` itself stays plain GLL that V1 can still run. Nodes without a stored position are placed automatically; **Auto layout** (`L`) re-arranges everything by signal flow.
- If the file contains a syntax error, the code panel shows it and the graph keeps showing the last valid version (read-only) until the error is fixed.

When the editor creates nodes it writes `_nc` ("not connected") into inputs that have no wire yet, for example `SR sr1(_nc, _nc) -> sr1_Q`. `_nc` is an ordinary signal nobody drives, so it always reads LOW. New nodes get an output signal named `<node>_Q`.

### **Simulation Features**

#### **Hot Reloading**

The editor monitors the loaded `.gll` or `.txt` file. When you save the file in your external editor, Gates re-parses it and updates the graph. The running simulation keeps its state (signal values, timers, counters, latched buttons) for everything that still exists, so you can change a circuit while it runs.

#### **Execution Modes**

- **Run / Pause**: **Space** or the Run button in the toolbar.
- **Step Once**: **Period (.)** or the Step button evaluates the next node.
- **Repeat / Once**: toggle between continuous scanning and stopping after one complete scan.

#### **Speed Control**

Use the `+` and `-` keys or the speed slider in the toolbar to adjust the evaluation frequency (nodes per second). Below about 40 Hz the node currently being evaluated is outlined on the canvas and its line is highlighted in the code panel.

### **Controls**

Press **F1** (or the `?` button) in the app for the full list.

- **Space** - Run / pause
- **Period (.)** - Evaluate one node
- **+/-** - Faster / slower
- **Ctrl+Z / Ctrl+Y** - Undo / redo
- **Del / Backspace** - Delete the selection
- **F** - Fit the circuit in view, **L** - Auto layout
- **Tab** - Show / hide the side panel, **P** - Show / hide the palette
- **Drag empty canvas** (or right / middle drag) - Pan, **Wheel** - Zoom, **Shift+drag** - Box select
- **Click** the switch of an `IN` terminal to toggle it
- **Click** a `BTN` node for a momentary press, **Ctrl+Click** to latch it
- **Click** the value of an `AIN` terminal to edit it in the inspector (HEX/DEC switch next to the field)
- **Timer / counter presets** are edited in the inspector and written into the file as the first argument (`TON t("500ms", x)`)

### **Execution Model**

Gates follows a basic PLC-style single-pass scan:

1.  **Read Inputs**: External signals (Modbus, UI toggles) are captured.
2.  **Sequential Evaluation**: Nodes are evaluated one by one in the order they appear in the file.
3.  **Forward References**: If a node uses an input from a node defined _later_ in the file, it will use the signal value from the _previous_ scan cycle. If it uses a signal from an _earlier_ node, it uses the fresh value from the current cycle.
4.  **Write Outputs**: Results are committed to output signals and Modbus coils.

This simple model ensures predictable behavior even with complex logic cycles.

### **Syntax-Sugar**

#### multiple outputs

If a gate has multiple outputs, they can be declared in a comma-separated list after the `->`.

example:

```
AND gate1(a, b) -> c, d
```

#### Aliasing

Aliases can be used to give a signal a more readable name. This is useful for readability and for debugging.

example:

```
IN INPUT_0(atEntry), INPUT_1(atLoad), INPUT_2(atLeft), INPUT_3(atRight)
OUT OUTPUT_0(entryConv), OUTPUT_1(loadConv), OUTPUT_2(forksL), OUTPUT_3(forksR)

AND Step1(atEntry, atLoad) -> entryConv
OR Step2(atLeft, atRight) -> forksL, forksR, loadConv
```
