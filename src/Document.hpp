#pragma once
// The open .gll file.
//
// The text file is the single source of truth: the node editor applies
// structural edits (Edits.hpp) to the script, writes the file and recompiles.
// External edits are picked up by polling the file (hot reload), so the file
// can still be edited in any text editor next to the graph.
#include "Gll.hpp"
#include "Parser.hpp"
#include <SFML/System/Vector2.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Document
{
public:
  // Node positions on the canvas, keyed by GraphView node key. Persisted in
  // a sidecar file "<file>.layout" so the .gll stays plain GLL.
  using Positions = std::map<std::string, sf::Vector2f>;

  bool open(const std::filesystem::path &path, std::string &error);

  // Reload if the file changed on disk. Returns true when anything visible
  // changed (new program or new error state).
  bool poll();

  const std::filesystem::path &path() const { return path_; }
  std::string fileName() const { return path_.filename().string(); }

  // Last successfully compiled state. While the file on disk has an error
  // these keep showing the last good version.
  const gll::Script &script() const { return script_; }
  std::shared_ptr<const Program> program() const { return program_; }
  const std::vector<std::string> &diskLines() const { return diskLines_; }

  // Error of the current file contents (nullopt when it compiles).
  const std::optional<ParseResult> &error() const { return error_; }
  bool editable() const { return !error_.has_value(); }

  // Increments whenever program() is replaced.
  uint64_t revision() const { return revision_; }

  // ---- editing ----------------------------------------------------------
  // Snapshot for undo, run `fn` on a copy of the script, write + recompile.
  // `fn` returns false to cancel. Returns false if cancelled or invalid.
  bool edit(const std::string &label, const std::function<bool(gll::Script &)> &fn);

  // Record an undo step for a layout-only change (node moves) that is
  // about to happen.
  void beginLayoutChange(const std::string &label);

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  bool undo();
  bool redo();
  const std::string &statusMessage() const { return status_; }
  void setStatus(const std::string &msg) { status_ = msg; }

  // ---- layout -------------------------------------------------------------
  Positions &positions() { return positions_; }
  void saveLayout();

private:
  struct Snapshot
  {
    std::string text;
    Positions positions;
    std::string label;
  };

  bool load(const std::string &text, bool fromDisk);
  bool writeFile(const std::string &text);
  std::string currentText() const;
  void restore(const Snapshot &snap);
  void loadLayout();

  std::filesystem::path path_;
  std::filesystem::file_time_type mtime_{};
  std::string eol_ = "\n";
  bool trailingNewline_ = true;

  gll::Script script_;
  std::shared_ptr<const Program> program_;
  std::vector<std::string> diskLines_;
  std::optional<ParseResult> error_;
  uint64_t revision_ = 0;

  Positions positions_;
  std::vector<Snapshot> undo_, redo_;
  std::string status_;
};
