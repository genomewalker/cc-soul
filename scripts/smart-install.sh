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

# Detect ONNX Runtime location
detect_onnx_runtime() {
    local include_dir=""
    local lib_dir=""

    # 1. Check CONDA_PREFIX (active conda environment)
    if [[ -n "$CONDA_PREFIX" ]]; then
        if [[ -f "$CONDA_PREFIX/lib/libonnxruntime.so" || -f "$CONDA_PREFIX/lib/libonnxruntime.dylib" ]]; then
            lib_dir="$CONDA_PREFIX/lib"
            # Check for nested include structure
            if [[ -d "$CONDA_PREFIX/include/onnxruntime/core/session" ]]; then
                include_dir="$CONDA_PREFIX/include/onnxruntime/core/session"
            elif [[ -f "$CONDA_PREFIX/include/onnxruntime_cxx_api.h" ]]; then
                include_dir="$CONDA_PREFIX/include"
            fi
        fi
    fi

    # 2. Check ~/.claude/deps/onnxruntime (dedicated install)
    if [[ -z "$lib_dir" && -d "$HOME/.claude/deps/onnxruntime" ]]; then
        local ort_dir="$HOME/.claude/deps/onnxruntime"
        if [[ -f "$ort_dir/lib/libonnxruntime.so" || -f "$ort_dir/lib/libonnxruntime.dylib" ]]; then
            lib_dir="$ort_dir/lib"
            if [[ -d "$ort_dir/include/onnxruntime/core/session" ]]; then
                include_dir="$ort_dir/include/onnxruntime/core/session"
            elif [[ -f "$ort_dir/include/onnxruntime_cxx_api.h" ]]; then
                include_dir="$ort_dir/include"
            fi
        fi
    fi

    # 3. Check pip install location
    if [[ -z "$lib_dir" ]]; then
        local pip_ort=$(python3 -c "import onnxruntime; print(onnxruntime.__file__)" 2>/dev/null | xargs dirname 2>/dev/null || true)
        if [[ -n "$pip_ort" && -d "$pip_ort/capi" ]]; then
            if [[ -f "$pip_ort/capi/libonnxruntime.so" || -f "$pip_ort/capi/libonnxruntime.dylib" ]]; then
                lib_dir="$pip_ort/capi"
                include_dir="$pip_ort/capi/include"
            fi
        fi
    fi

    # 4. macOS: Check Homebrew
    if [[ -z "$lib_dir" && "$(uname -s)" == "Darwin" ]]; then
        local brew_prefix=$(brew --prefix onnxruntime 2>/dev/null || true)
        if [[ -n "$brew_prefix" && -d "$brew_prefix/lib" ]]; then
            lib_dir="$brew_prefix/lib"
            if [[ -d "$brew_prefix/include/onnxruntime/core/session" ]]; then
                include_dir="$brew_prefix/include/onnxruntime/core/session"
            elif [[ -f "$brew_prefix/include/onnxruntime_cxx_api.h" ]]; then
                include_dir="$brew_prefix/include"
            fi
        fi
    fi

    # Return results
    if [[ -n "$lib_dir" && -n "$include_dir" ]]; then
        echo "$include_dir|$lib_dir"
        return 0
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
    mkdir -p "$BIN_DIR" "$plugin_bin"

    # Clean build directory to avoid CMake cache conflicts
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Detect ONNX Runtime for embeddings
    local cmake_args="-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS= -DCMAKE_C_FLAGS="
    local onnx_info
    if onnx_info=$(detect_onnx_runtime); then
        local onnx_include="${onnx_info%%|*}"
        local onnx_lib="${onnx_info##*|}"
        echo "[cc-soul] Found ONNX Runtime: $onnx_lib"
        cmake_args="$cmake_args -DONNXRUNTIME_INCLUDE_DIR=$onnx_include -DONNXRUNTIME_LIB_DIR=$onnx_lib"
        # Set RPATH so binary finds libs at runtime without LD_LIBRARY_PATH
        cmake_args="$cmake_args -DCMAKE_INSTALL_RPATH=$onnx_lib"
        cmake_args="$cmake_args -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"
    else
        echo "[cc-soul] WARNING: ONNX Runtime not found, embeddings will be disabled"
        echo "[cc-soul]   Install via: conda install onnxruntime-cpp, pip install onnxruntime, or brew install onnxruntime"
    fi

    # Configure - show errors now for debugging
    if ! cmake .. $cmake_args 2>&1 | tail -10; then
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
    # Only chitta and chittad are required; migrate/import are legacy optional tools
    local all_built=true
    for bin in chitta chittad; do
        if [[ -x "$plugin_bin/$bin" ]]; then
            cp -f "$plugin_bin/$bin" "$BIN_DIR/$bin"
        else
            echo "[cc-soul] ERROR: $bin not built" >&2
            all_built=false
        fi
    done

    # Copy shared libraries if present
    for lib in libonnxruntime.so libonnxruntime.so.1.16.3; do
        if [[ -f "$plugin_bin/$lib" ]]; then
            cp -f "$plugin_bin/$lib" "$BIN_DIR/$lib"
        fi
    done

    $all_built && echo "[cc-soul] Build complete"
}

# Download ONNX models with checksum verification
download_models() {
    if [[ -f "$MODELS_DIR/model.onnx" && -f "$MODELS_DIR/vocab.txt" ]]; then
        # Check if model dimension changed (force re-download for model upgrade)
        # Must check BEFORE checksum verification to catch old MiniLM→BGE upgrade
        local model_size=$(stat -f%z "$MODELS_DIR/model.onnx" 2>/dev/null || stat --printf="%s" "$MODELS_DIR/model.onnx" 2>/dev/null || echo "0")
        if [[ "$model_size" -lt 200000000 ]]; then
            echo "[cc-soul] Model upgrade detected (384→768 dim), re-downloading..."
            rm -f "$MODELS_DIR/model.onnx" "$MODELS_DIR/vocab.txt"
        else
            # Verify checksums on correct-sized model
            if verify_checksum "$MODELS_DIR/model.onnx" "$MODEL_CHECKSUM" && \
               verify_checksum "$MODELS_DIR/vocab.txt" "$VOCAB_CHECKSUM"; then
                return 0
            fi
            echo "[cc-soul] Model checksum mismatch, re-downloading..."
        fi
    fi

    echo "[cc-soul] Downloading embedding model (bge-base-en-v1.5, ~436MB)..."
    mkdir -p "$MODELS_DIR"

    local model_url="https://huggingface.co/BAAI/bge-base-en-v1.5/resolve/main/onnx/model.onnx"
    local vocab_url="https://huggingface.co/BAAI/bge-base-en-v1.5/resolve/main/vocab.txt"

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
# Note: MCP server config is separate - use /cc-soul-mcp command
configure_permissions() {
    local settings_file="${HOME}/.claude/settings.json"

    # Permissions to add (global - applies to all projects)
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
    mkdir -p "${HOME}/.claude/hooks"
}

# Install hooks
install_hooks() {
    local hooks_src="$PLUGIN_DIR/hooks"
    local hooks_dst="${HOME}/.claude/hooks"

    mkdir -p "$hooks_dst"

    # Individual hook scripts
    local hooks=(
        subconscious.sh
        session-start-hook.sh
        prompt-hook.sh
        stop-hook.sh
        pre-compact-hook.sh
        pre-tool-hook.sh
        post-bash-hook.sh
        log-bash-history.sh
    )

    for script in "${hooks[@]}"; do
        if [[ -f "$hooks_src/$script" ]]; then
            if [[ ! -f "$hooks_dst/$script" ]] || \
               ! cmp -s "$hooks_src/$script" "$hooks_dst/$script"; then
                cp "$hooks_src/$script" "$hooks_dst/"
                chmod +x "$hooks_dst/$script"
                echo "[cc-soul] Installed $script"
            fi
        fi
    done
}

# Install CLAUDE.md as user-level rule
install_claude_rules() {
    local rules_dir="${HOME}/.claude/rules"
    local source="$PLUGIN_DIR/CLAUDE.md"
    local target="$rules_dir/cc-soul.md"

    [[ ! -f "$source" ]] && return 0

    mkdir -p "$rules_dir"
    ln -sf "$source" "$target"
    echo "[cc-soul] Linked CLAUDE.md → ~/.claude/rules/cc-soul.md"
}

# Configure hooks in settings.json
configure_hooks() {
    local settings_file="${HOME}/.claude/settings.json"

    # Needs jq for JSON manipulation
    if ! command -v jq &> /dev/null; then
        echo "[cc-soul] jq not found, skipping hook config" >&2
        return 0
    fi

    # Skip if settings file doesn't exist
    [[ ! -f "$settings_file" ]] && return 0

    # Skip if cc-soul plugin is enabled (plugin manages its own hooks via hooks.json)
    if jq -e '.enabledPlugins["cc-soul@genomewalker-cc-soul"] == true' "$settings_file" &>/dev/null; then
        echo "[cc-soul] Plugin enabled, skipping hook config (plugin manages hooks)"
        return 0
    fi

    local current
    current=$(cat "$settings_file")

    # Ensure hooks object exists
    if ! echo "$current" | jq -e '.hooks' &>/dev/null; then
        current=$(echo "$current" | jq '.hooks = {}')
    fi

    local updated="$current"
    local added=0
    local hooks_dir="${HOME}/.claude/hooks"

    # ============================================================
    # cc-soul hooks - memory injection and learning
    # Uses ~/.claude/hooks/ for portability (independent of plugin location)
    # ============================================================

    # SessionStart: awakening + context injection
    if ! echo "$updated" | jq -e '.hooks.SessionStart[]? | select(.hooks[]?.command | contains("session-start-hook.sh"))' &>/dev/null; then
        local session_start_hook='{"matcher":"*","hooks":[{"type":"command","command":"~/.claude/hooks/subconscious.sh start","statusMessage":"awakening…"},{"type":"command","command":"~/.claude/hooks/session-start-hook.sh","statusMessage":"recalling context…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$session_start_hook" '.hooks.SessionStart = ((.hooks.SessionStart // []) + [$hook])')
        ((added++)) || true
    fi

    # UserPromptSubmit: memory resonance on each message
    if ! echo "$updated" | jq -e '.hooks.UserPromptSubmit[]? | select(.hooks[]?.command | contains("prompt-hook.sh"))' &>/dev/null; then
        local prompt_hook='{"matcher":"*","hooks":[{"type":"command","command":"~/.claude/hooks/prompt-hook.sh","statusMessage":"resonating…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$prompt_hook" '.hooks.UserPromptSubmit = ((.hooks.UserPromptSubmit // []) + [$hook])')
        ((added++)) || true
    fi

    # Stop: preserve state and auto-learn
    if ! echo "$updated" | jq -e '.hooks.Stop[]? | select(.hooks[]?.command | contains("stop-hook.sh"))' &>/dev/null; then
        local stop_hook='{"matcher":"*","hooks":[{"type":"command","command":"~/.claude/hooks/stop-hook.sh","statusMessage":"preserving state…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$stop_hook" '.hooks.Stop = ((.hooks.Stop // []) + [$hook])')
        ((added++)) || true
    fi

    # PreCompact: checkpoint before context loss
    if ! echo "$updated" | jq -e '.hooks.PreCompact[]? | select(.hooks[]?.command | contains("pre-compact-hook.sh"))' &>/dev/null; then
        local precompact_hook='{"matcher":"*","hooks":[{"type":"command","command":"~/.claude/hooks/pre-compact-hook.sh","statusMessage":"consolidating memory…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$precompact_hook" '.hooks.PreCompact = ((.hooks.PreCompact // []) + [$hook])')
        ((added++)) || true
    fi

    # PreToolUse (Read|Edit|Write): inject file context
    if ! echo "$updated" | jq -e '.hooks.PreToolUse[]? | select(.hooks[]?.command | contains("pre-tool-hook.sh"))' &>/dev/null; then
        local pretool_hook='{"matcher":"Read|Edit|Write","hooks":[{"type":"command","command":"~/.claude/hooks/pre-tool-hook.sh","timeout":5,"statusMessage":"gathering context…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$pretool_hook" '.hooks.PreToolUse = ((.hooks.PreToolUse // []) + [$hook])')
        ((added++)) || true
    fi

    # ============================================================
    # Bash history hook (existing functionality)
    # ============================================================
    if ! echo "$updated" | jq -e '.hooks.PostToolUse[]? | select(.matcher == "Bash") | .hooks[]? | select(.command | contains("log-bash-history"))' &>/dev/null; then
        local bash_hook='{"matcher":"Bash","hooks":[{"type":"command","command":"~/.claude/hooks/log-bash-history.sh \"$CLAUDE_TOOL_INPUT_command\" >/dev/null 2>&1","timeout":5,"statusMessage":"logging command…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$bash_hook" '.hooks.PostToolUse = ((.hooks.PostToolUse // []) + [$hook])')
        ((added++)) || true
    fi

    # Write back if changed
    if [[ $added -gt 0 ]]; then
        echo "$updated" | jq '.' > "$settings_file"
        echo "[cc-soul] Configured $added hooks in settings.json"
    fi
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
        # Check if ONNX Runtime is available locally
        # Pre-built binaries DON'T include ONNX, so build from source when possible
        local has_local_onnx=false
        if detect_onnx_runtime >/dev/null 2>&1; then
            has_local_onnx=true
        fi

        if $has_local_onnx; then
            # ONNX available: build from source to get embeddings support
            echo "[cc-soul] ONNX Runtime detected, building from source for embedding support..."
            build_from_source || {
                echo "[cc-soul] WARNING: Source build failed, trying pre-built (no embeddings)"
                if [[ "$platform" != "unknown" ]]; then
                    download_binaries "$current_version" "$platform" || {
                        echo "[cc-soul] ERROR: Installation failed" >&2
                        exit 1
                    }
                else
                    echo "[cc-soul] ERROR: No pre-built binaries for this platform" >&2
                    exit 1
                fi
            }
        elif [[ "$platform" != "unknown" ]]; then
            # No local ONNX: try pre-built first (faster), then source
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

    # Install hooks
    install_hooks

    # Link CLAUDE.md to user rules
    install_claude_rules

    # Configure bash permissions for chitta commands
    configure_permissions

    # Configure hooks in settings.json
    configure_hooks

    if ! validate_binaries; then
        echo "[cc-soul] ERROR: Installation incomplete (invalid binaries)" >&2
        exit 1
    fi

    # Run database migrations if needed
    if [[ -f "${HOME}/.claude/mind/chitta.duckdb" && -x "$BIN_DIR/chittad" ]]; then
        "$BIN_DIR/chittad" upgrade --path "${HOME}/.claude/mind" 2>/dev/null | grep -E "^\[migrations\]|Already at" || true
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
