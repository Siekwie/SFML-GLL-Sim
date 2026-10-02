# Changelog

## All notable changes to **Gates** (the SFML **GLL** logic simulator) will be documented in this file.

## V2 (0.1.0) - 2026-10-02

The simulator becomes a node editor. Same language, same simulation semantics, same Modbus integration.

- New node canvas: every gate is a node, every declared IN/OUT/AIN/AOUT a terminal, wires coloured by live value; pan, zoom, box select, auto layout (wraps long step sequences into bands), zoomed-out overview
- Visual editing written back to the `.gll` file: add nodes from the palette or the right-click menu, connect / unplug wires, input modifiers (NOT/PS/NS), rename, presets, delete; dropping a wire on empty canvas creates a connected node
- Inspector panel with structured fields per node and live state; circuit notes (undriven outputs, signals nothing drives, unconnected inputs)
- Code panel kept as a toggleable view with live highlighting, linked to the selection
- Undo / redo for every edit and move; node positions in a `<file>.gll.layout` sidecar
- Hot reload keeps the simulation state (signals, timers, counters, latches) instead of restarting it
- Parse errors are shown in the app (line + message) instead of only on the console; the graph stays on the last valid version
- Toolbar, status bar, Modbus dialog and shortcut overlay (F1) redesigned; window is resizable
- Language core split into a lossless syntax layer (`Gll.hpp`), compiler (`Parser.hpp`) and editing operations (`Edits.hpp`), with a headless test suite (`-DGLL_BUILD_TESTS=ON`) that checks the new parser against the V1 parser on every sample
- Fixes: comparator literals accept the full 32-bit range (were limited to 0-255); comparators compare full 64-bit values; token highlighting on indented lines; aliased declarations are highlighted; UTF-8 names render correctly; nested modifiers like `NOT(PS(a))` are reported as an error instead of producing a bogus signal
- Opening a file that does not exist creates it

## 0.0.7 - 2026-01-06

- Added configurable register mode in settings: 16-bit (0-65535) or 32-bit (0-4,294,967,295)
- 32-bit mode uses 2 consecutive Modbus registers per signal (big-endian)
- Added settings for analog input/output counts in Modbus settings dialog
- Analog widgets display both hex and decimal values
- Fixed analog input parsing to support full 32-bit range
- Removed CV output clamping to 255

## 0.0.6 - 2026-01-06

- Added AIN (Analog Input) and AOUT (Analog Output) signal declarations
- Added hex literal support in comparators ("0xFF", "0x80") and decimal literals ("128")
- Counter CV outputs, analog signals, and literals are all compatible for comparisons
- Added analog input widgets with editable values and HEX/DEC toggle switch
- Added analog output widgets (read-only display)
- Added Modbus TCP support for analog I/O (AINPUT_N/AOUTPUT_N mapped to registers)
- Removed ESC key closing the application - ESC now exits text input and reverts changes
- Updated [GLL_Documentation.md](GLL_Documentation.md#Syntax) with AIN/AOUT syntax

## 0.0.5 - 2025-12-29

- add support for not full number time values to be delared with .5s, .25s, etc
- Implemented PS (Positive Signal/Rising Edge) node for detecting rising edges
- Added inline PS() syntax support (similar to NOT()) for use in gate arguments
- Implemented NS (Negative Signal/Falling Edge) node for detecting falling edges
- Added inline NS() syntax support (similar to NOT() and PS()) for use in gate arguments
- Added CV (Counter Value) output extraction for CTU/CTD counters via second output variable
- Implemented LT (Less Than), GT (Greater Than), and EQ (Equal) comparator gates
- Comparators work with CV outputs to compare counter values

## 0.0.4 - 2025-12-20

- Dynamic IO bit counts for Modbus configarable in settings, and auto-saved to disk
- Added support for TON/TOF nodes with hardcoded preset times in Gll
- Uniformised time parsing in UI and Gll
- Added millisecond support
- Implemented CTU (Counter Up) and CTD (Counter Down) nodes
- Added support for hardcoded PV/CV arguments in CTU/CTD
- Added UI widgets for viewing and editing counter PV/CV values
- Fixed castSignalToBool\_ returning true for all valid signal indices regardless of value
- Updated GLL_Documentation.md with CTU/CTD details
- Added Scrolling behavior to the code part

## 0.0.3 - 2025-12-20

- Reversed to having a basic PLC single-pass scan from top to bottom without any fancy ready checks nor sorting.
- Added forward reference detection
- Added a Sample for a switch with a SR that has one input for set and reset, containing such forward pass.
- Added input buffering till the end of a cycle (currently only counts for basic signal inputs)
- Added support for higher frequencies (UNTESTED)
- Added ModBus Client Support
- Added FactoryIO A-To-B GLL example
- Added alias support for inputs and outputs see [Aliasing](GLL_Documentation.md#aliasing)
- Hardened font loading and added fallback
- Capped max sim speed at 2000 Hz for now
- Added XOR gate support see [XOR-Gate](GLL_Documentation.md#xor)

## 0.0.2 - 2025-12-19

Added **TON** and **TOF** timer gates with interactive preset time (PT) configuration:

- **TON** (Timer On Delay): Output goes HIGH after input has been HIGH for the preset time duration
- **TOF** (Timer Off Delay): Output stays HIGH for the preset time duration after input goes LOW
- Timer widgets appear in the sidebar for each TON/TOF gate
- Click timer widgets to edit preset time (PT) - supports formats like `500ms`, `10s`, `2m`, `1h` (milliseconds, seconds, minutes, hours)
- Timer widgets highlight yellowish when active (timer is ticking), grayish when inactive
- Preset time can be configured at runtime through the UI sidebar

- **hot reloading**
- **run loop toggle button**

## 0.0.1 - 2025-12-18

Baseline version matching the currenty released program behavior, with the following feature differences:

- No **hot reloading**: editing the loaded `.txt/.gll` file did not automatically re-parse and refresh the simulator/UI.
- No **run loop toggle button**: there was no `REPEAT/ONCE` control; when running, the simulation always looped continuously (it could not auto-stop after a single run cycle).
