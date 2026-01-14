#!/bin/bash
# Smart Install Script for cc-soul
#
# Tries to download pre-built binaries, falls back to building from source.
# Runs as first hook on SessionStart.

set -e

# Ignore signals that might come from daemon shutdown
trap '' USR1 USR2

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"
CHITTA_DIR="$PLUGIN_DIR/chitta"
BUILD_DIR="$CHITTA_DIR/build"
BIN_DIR="${HOME}/.claude/bin"
MODELS_DIR="${HOME}/.claude/models"
MARKER="$PLUGIN_DIR/.install-complete"

# GitHub release URL base
GITHUB_REPO="genomewalker/cc-soul"
RELEASE_URL="https://github.com/$GITHUB_REPO/releases/download"

# ONNX model checksums (SHA256) - empty hash = skip verification
# TODO: compute actual checksums when models are pinned
MODEL_CHECKSUM=""
VOCAB_CHECKSUM=""

# Detect platform
detect_platform() {
    local os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local arch=$(uname -m)

    case "$os" in
        linux)
            case "$arch" in
                x86_64) echo "linux-x64" ;;
                aarch64) echo "linux-arm64" ;;
                *) echo "unknown" ;;
            esac
            ;;
        darwin)
            case "$arch" in
                x86_64) echo "macos-x64" ;;
                arm64) echo "macos-arm64" ;;
                *) echo "unknown" ;;
            esac
            ;;
        *) echo "unknown" ;;
    esac
}

# Download file with curl or wget
download() {
    local url="$1"
    local output="$2"

    if command -v curl &> /dev/null; then
        curl -fsSL -o "$output" "$url" 2>/dev/null
    elif command -v wget &> /dev/null; then
        wget -q -O "$output" "$url" 2>/dev/null
    else
        return 1
    fi
}

# Verify checksum (empty expected = skip verification)
verify_checksum() {
    local file="$1"
    local expected="$2"

    [[ -z "$expected" ]] && return 0  # Skip if no checksum provided

    local actual
    if command -v sha256sum &>/dev/null; then
        actual=$(sha256sum "$file" | cut -d' ' -f1)
    elif command -v shasum &>/dev/null; then
        actual=$(shasum -a 256 "$file" | cut -d' ' -f1)
    else
        return 0  # Can't verify, assume OK
    fi

    [[ "$actual" == "$expected" ]]
}

# Try to download pre-built binaries
download_binaries() {
    local version="$1"
    local platform="$2"
    local url="$RELEASE_URL/v$version/chitta-$platform.tar.gz"

    echo "[cc-soul] Downloading pre-built binaries ($platform)..."
    local tmp_file=$(mktemp)

    if download "$url" "$tmp_file"; then
        mkdir -p "$BIN_DIR"
        # Clean old chitta files before extracting
        rm -f "$BIN_DIR"/chitta* "$BIN_DIR"/lib*.so* 2>/dev/null
        if tar -xzf "$tmp_file" -C "$BIN_DIR" 2>/dev/null; then
            rm -f "$tmp_file"
            # Verify binaries can actually run (check for missing shared libs)
            # The bundled libs should be found via RPATH=$ORIGIN
            if "$BIN_DIR/chittad" --help >/dev/null 2>&1 && \
               "$BIN_DIR/chitta" --help >/dev/null 2>&1; then
                echo "[cc-soul] Pre-built binaries installed"
                return 0
            else
                echo "[cc-soul] Pre-built binaries incompatible, will build from source"
                rm -f "$BIN_DIR"/chitta_* "$BIN_DIR"/lib*.so*
                return 1
            fi
        fi
        rm -f "$tmp_file"
    fi

    return 1
}

# Build from source
build_from_source() {
    echo "[cc-soul] Building from source..."

    # Check dependencies
    if ! command -v cmake &> /dev/null; then
        echo "[cc-soul] ERROR: cmake not found. Please install cmake." >&2
        return 1
    fi

    if ! command -v make &> /dev/null; then
        echo "[cc-soul] ERROR: make not found. Please install make." >&2
        return 1
    fi

    local plugin_bin="$PLUGIN_DIR/bin"
    mkdir -p "$BUILD_DIR" "$BIN_DIR" "$plugin_bin"
    cd "$BUILD_DIR"

    # Configure - show errors now for debugging
    # Explicitly clear ASAN flags for release builds
    if ! cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="" -DCMAKE_C_FLAGS="" 2>&1 | tail -5; then
        echo "[cc-soul] ERROR: cmake configuration failed" >&2
        return 1
    fi

    # Build (outputs to $PLUGIN_DIR/bin per CMakeLists.txt)
    local nproc_val=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    if ! make -j"$nproc_val" 2>&1 | tail -10; then
        echo "[cc-soul] ERROR: build failed" >&2
        return 1
    fi

    # Copy binaries from plugin bin to install location (~/.claude/bin)
    local all_built=true
    for bin in chitta chittad chitta_migrate chitta_import; do
        if [[ -x "$plugin_bin/$bin" ]]; then
            cp -f "$plugin_bin/$bin" "$BIN_DIR/$bin"
        else
            echo "[cc-soul] WARNING: $bin not built" >&2
            all_built=false
        fi
    done

    # Copy shared libraries if present
    for lib in libonnxruntime.so libonnxruntime.so.1.16.3 libsqlite3.so; do
        if [[ -f "$plugin_bin/$lib" ]]; then
            cp -f "$plugin_bin/$lib" "$BIN_DIR/$lib"
        fi
    done

    $all_built && echo "[cc-soul] Build complete"
}

# Download ONNX models with checksum verification
download_models() {
    if [[ -f "$MODELS_DIR/model.onnx" && -f "$MODELS_DIR/vocab.txt" ]]; then
        # Verify existing models
        if verify_checksum "$MODELS_DIR/model.onnx" "$MODEL_CHECKSUM" && \
           verify_checksum "$MODELS_DIR/vocab.txt" "$VOCAB_CHECKSUM"; then
            return 0
        fi
        echo "[cc-soul] Model checksum mismatch, re-downloading..."
    fi

    echo "[cc-soul] Downloading embedding model..."
    mkdir -p "$MODELS_DIR"

    local model_url="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx"
    local vocab_url="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/vocab.txt"

    if ! download "$model_url" "$MODELS_DIR/model.onnx"; then
        echo "[cc-soul] WARNING: Could not download model.onnx" >&2
        return 1
    fi

    if ! download "$vocab_url" "$MODELS_DIR/vocab.txt"; then
        echo "[cc-soul] WARNING: Could not download vocab.txt" >&2
        return 1
    fi

    # Verify downloads
    if ! verify_checksum "$MODELS_DIR/model.onnx" "$MODEL_CHECKSUM"; then
        echo "[cc-soul] WARNING: model.onnx checksum mismatch" >&2
    fi

    echo "[cc-soul] Models downloaded"
}

# Configure bash permissions for chitta commands (global settings)
configure_permissions() {
    local settings_file="${HOME}/.claude/settings.json"

    # Permissions to add (global - applies to all projects)
    # Binaries install to ~/.claude/bin, so we only need those paths
    local perms=(
        'Bash(~/.claude/bin/chitta:*)'
        'Bash(~/.claude/bin/chittad:*)'
        'Bash(chitta:*)'
        'Bash(chittad:*)'
        'Bash(pkill -f "chittad daemon":*)'
    )

    # Always use global settings.json
    mkdir -p "${HOME}/.claude"

    # If no settings file exists, create minimal one
    if [[ ! -f "$settings_file" ]]; then
        echo '{}' > "$settings_file"
    fi

    # Check if jq is available
    if ! command -v jq &> /dev/null; then
        echo "[cc-soul] jq not found, skipping permission config" >&2
        return 0
    fi

    # Read current settings
    local current
    current=$(cat "$settings_file")

    # Ensure permissions.allow exists
    if ! echo "$current" | jq -e '.permissions.allow' &>/dev/null; then
        current=$(echo "$current" | jq '.permissions = {"allow": []}')
    fi

    # Add each permission if not already present
    local updated="$current"
    local added=0
    for perm in "${perms[@]}"; do
        # Use --arg to safely escape permission strings (handles embedded quotes)
        if ! echo "$updated" | jq -e --arg p "$perm" '.permissions.allow | index($p)' &>/dev/null; then
            updated=$(echo "$updated" | jq --arg p "$perm" '.permissions.allow += [$p]')
            ((added++)) || true
        fi
    done

    # Write back if changed
    if [[ $added -gt 0 ]]; then
        echo "$updated" | jq '.' > "$settings_file"
        echo "[cc-soul] Added $added bash permissions for chitta (global)"
    fi
}

# Create directories (symlinks no longer needed - mind is at ~/.claude/mind)
create_directories() {
    mkdir -p "${HOME}/.claude/mind"
    mkdir -p "${HOME}/.claude/bin"
}

# Stop any running daemon (gracefully via chittad shutdown, fallback to signals)
stop_daemon() {
    # Try graceful shutdown via chittad
    if [[ -x "$BIN_DIR/chittad" ]]; then
        "$BIN_DIR/chittad" shutdown 2>/dev/null && sleep 1 && return 0
    fi

    # Fallback: signal daemon directly
    local daemon_pid=$(pgrep -f "chittad daemon" 2>/dev/null || true)
    if [[ -n "$daemon_pid" ]]; then
        echo "[cc-soul] Stopping daemon (pid $daemon_pid)..."
        kill -TERM "$daemon_pid" 2>/dev/null || true
        sleep 1
        kill -0 "$daemon_pid" 2>/dev/null && kill -9 "$daemon_pid" 2>/dev/null || true
    fi

    # Clean up stale files
    rm -f /tmp/chitta-*.sock /tmp/chitta-*.lock /tmp/chitta-*.pid 2>/dev/null || true
}

validate_binaries() {
    if [[ ! -x "$BIN_DIR/chittad" ]]; then
        echo "[cc-soul] ERROR: chittad not found after install" >&2
        return 1
    fi
    if [[ ! -x "$BIN_DIR/chitta" ]]; then
        echo "[cc-soul] ERROR: chitta not found after install" >&2
        return 1
    fi

    local daemon_help
    daemon_help=$("$BIN_DIR/chittad" daemon --help 2>&1 || true)
    if [[ -z "$daemon_help" ]]; then
        echo "[cc-soul] ERROR: Unable to query chittad daemon help" >&2
        return 1
    fi
    if ! echo "$daemon_help" | grep -q -- "--socket"; then
        echo "[cc-soul] ERROR: chittad daemon lacks --socket support" >&2
        return 1
    fi

    local cli_help
    cli_help=$("$BIN_DIR/chitta" --help 2>&1 || true)
    if [[ -z "$cli_help" ]]; then
        echo "[cc-soul] ERROR: Unable to query chitta help" >&2
        return 1
    fi
    if ! echo "$cli_help" | grep -q -- "--socket-path"; then
        echo "[cc-soul] WARNING: chitta missing --socket-path support" >&2
    fi
}

# Main
main() {
    # Check if already installed
    local current_version
    if command -v jq &>/dev/null; then
        current_version=$(jq -r '.version // "0.0.0"' "$PLUGIN_DIR/.claude-plugin/plugin.json" 2>/dev/null || echo "0.0.0")
    else
        current_version=$(grep '"version"' "$PLUGIN_DIR/.claude-plugin/plugin.json" 2>/dev/null | head -1 | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' || echo "0.0.0")
    fi
    local installed_version=$(cat "$MARKER" 2>/dev/null || echo "")

    if [[ "$current_version" == "$installed_version" && -x "$BIN_DIR/chitta" && -f "$MODELS_DIR/model.onnx" ]]; then
        exit 0  # Already installed
    fi

    # Stop daemon before updating binaries (version mismatch can cause issues)
    stop_daemon

    echo "[cc-soul] Installing v$current_version..."

    # Download models
    download_models

    # Install binaries (try pre-built first, then build)
    local platform=$(detect_platform)
    local need_binaries=false

    if [[ ! -x "$BIN_DIR/chitta" || "$current_version" != "$installed_version" ]]; then
        need_binaries=true
    fi

    if $need_binaries; then
        if [[ "$platform" != "unknown" ]]; then
            download_binaries "$current_version" "$platform" || build_from_source || {
                echo "[cc-soul] ERROR: Installation failed" >&2
                exit 1
            }
        else
            build_from_source || {
                echo "[cc-soul] ERROR: Build failed and no pre-built binaries for this platform" >&2
                exit 1
            }
        fi
    fi

    # Create directories
    create_directories

    # Configure bash permissions for chitta commands
    configure_permissions

    if ! validate_binaries; then
        echo "[cc-soul] ERROR: Installation incomplete (invalid binaries)" >&2
        exit 1
    fi

    # Version change notification
    if [[ -n "$installed_version" && "$installed_version" != "$current_version" ]]; then
        echo "[cc-soul] Updated: $installed_version → $current_version"
    fi

    # Mark as installed
    echo "$current_version" > "$MARKER"
    echo "[cc-soul] Installation complete (v$current_version)"
}

main "$@"
