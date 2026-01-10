---
name: memory-location
description: Configure chitta memory storage location for optimal I/O performance. Use when memory is slow, setting up fast storage, or moving the soul database.
allowed-tools: Bash, Read, AskUserQuestion
---

# Memory Location Setup

Move chitta memory to faster storage by symlinking `~/.claude/mind`.

## How It Works

cc-soul stores all data in `~/.claude/mind/`. By replacing this directory with a symlink to faster storage, everything works transparently across plugin versions.

## Instructions

### Step 1: Ask for Fast Storage Path

Use AskUserQuestion to get the user's preferred fast storage location.

### Step 2: Benchmark (Optional)

Test I/O speed of the provided path vs current:

```bash
# Test current
dd if=/dev/zero of=~/.claude/mind/io_test bs=1M count=10 2>&1 | grep -oP '\d+\.?\d* [MGK]B/s'
rm -f ~/.claude/mind/io_test

# Test new path
dd if=/dev/zero of="$FAST_PATH/io_test" bs=1M count=10 2>&1 | grep -oP '\d+\.?\d* [MGK]B/s'
rm -f "$FAST_PATH/io_test"
```

### Step 3: Migrate

```bash
MIND_PATH="$HOME/.claude/mind"
FAST_PATH="<user-provided-path>"

# Kill chitta first
pkill -9 chitta 2>/dev/null
sleep 1

# Create fast storage directory
mkdir -p "$FAST_PATH"

# Move existing data
if [ -d "$MIND_PATH" ] && [ ! -L "$MIND_PATH" ]; then
    cp -r "$MIND_PATH"/* "$FAST_PATH/" 2>/dev/null
    mv "$MIND_PATH" "${MIND_PATH}.backup"
fi

# Remove if symlink exists
[ -L "$MIND_PATH" ] && rm "$MIND_PATH"

# Create symlink
ln -sf "$FAST_PATH" "$MIND_PATH"
```

### Step 4: Verify

```bash
ls -la ~/.claude/mind
ls -la ~/.claude/mind/
```

## Rollback

```bash
rm ~/.claude/mind
mv ~/.claude/mind.backup ~/.claude/mind
```

## Notes

- Works across all cc-soul versions (path is version-independent)
- Warn if user chooses `/tmp` or `/scratch` - data may not persist
- Backup files remain in `~/.claude/mind.backup` until user deletes them
