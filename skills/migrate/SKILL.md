---
name: migrate
description: Incrementally import soul data from SQLite or shared chitta files. Adds to existing data.
execution: task
---

# Soul Migration (Incremental)

Import soul data incrementally - new nodes are added to existing data.

## Sources

1. **SQLite soul.db** - Legacy cc-soul format (chitta_migrate)
2. **Shared chitta files** - From another user (chitta_import)

## Execute

```
Task(
  subagent_type="general-purpose",
  description="Import soul data incrementally",
  prompt="""
Import soul data incrementally. Ask user which source to import from.

## 1. Determine Source

Ask user: "Import from (1) SQLite soul.db or (2) shared chitta files?"

## 2a. SQLite soul.db Import

Find binaries:
```bash
PLUGIN_DIR=$(ls -d ~/.claude/plugins/cache/genomewalker-cc-soul/soul/*/chitta 2>/dev/null | tail -1)
MIGRATE_BIN="$PLUGIN_DIR/build/chitta_migrate"
MODELS_PATH="$PLUGIN_DIR/models"

# Build if missing
if [ ! -f "$MIGRATE_BIN" ]; then
  cd "$PLUGIN_DIR/build" && make chitta_migrate
fi
```

Dry run:
```bash
$MIGRATE_BIN --dry-run --verbose \
  --soul-db ~/.claude/mind/soul.db \
  --output ~/.claude/mind/chitta \
  --model "$MODELS_PATH/model.onnx" \
  --vocab "$MODELS_PATH/vocab.txt"
```

If approved, run import:
```bash
$MIGRATE_BIN --verbose \
  --soul-db ~/.claude/mind/soul.db \
  --output ~/.claude/mind/chitta \
  --model "$MODELS_PATH/model.onnx" \
  --vocab "$MODELS_PATH/vocab.txt"
```

## 2b. Chitta Files Import

Ask user for source path (base path without .hot/.cold suffix).

Find binaries:
```bash
PLUGIN_DIR=$(ls -d ~/.claude/plugins/cache/genomewalker-cc-soul/soul/*/chitta 2>/dev/null | tail -1)
IMPORT_BIN="$PLUGIN_DIR/build/chitta_import"
MODELS_PATH="$PLUGIN_DIR/models"

# Build if missing
if [ ! -f "$IMPORT_BIN" ]; then
  cd "$PLUGIN_DIR/build" && make chitta_import
fi
```

Dry run:
```bash
$IMPORT_BIN --dry-run --verbose \
  --source /path/to/shared/chitta \
  --target ~/.claude/mind/chitta \
  --model "$MODELS_PATH/model.onnx" \
  --vocab "$MODELS_PATH/vocab.txt"
```

If approved, run import:
```bash
$IMPORT_BIN --verbose \
  --source /path/to/shared/chitta \
  --target ~/.claude/mind/chitta \
  --model "$MODELS_PATH/model.onnx" \
  --vocab "$MODELS_PATH/vocab.txt"
```

## 3. Verify

After import:
- chitta soul_context format="json" - Check node counts
- chitta recall query="wisdom", limit=3 - Verify search

## 4. Report

- Source type and path
- Nodes imported by type
- Current soul state
"""
)
```
