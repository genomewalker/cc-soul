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
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
MARKER="$PLUGIN_DIR/.install-complete"

# GitHub release URL base
GITHUB_REPO="genomewalker/cc-soul"
RELEASE_URL="https://github.com/$GITHUB_REPO/releases/download"

# Temp dir for downloaded source (cleaned up on exit)
SOURCE_BUILD_DIR=""
_DOWNLOAD_TMP=""
cleanup_tmp() { [[ -n "$_DOWNLOAD_TMP" ]] && rm -rf "$_DOWNLOAD_TMP"; }
trap cleanup_tmp EXIT

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

# Fetch latest release version tag from GitHub API
fetch_latest_version() {
    local tmp
    tmp=$(mktemp)
    local api_url="https://api.github.com/repos/$GITHUB_REPO/releases/latest"
    if download "$api_url" "$tmp" && [[ -s "$tmp" ]]; then
        local tag
        if command -v jq &>/dev/null; then
            tag=$(jq -r '.tag_name // ""' "$tmp" 2>/dev/null)
        else
            tag=$(grep -o '"tag_name":"[^"]*"' "$tmp" | head -1 | sed 's/.*:"\([^"]*\)"/\1/')
        fi
        rm -f "$tmp"
        [[ -n "$tag" ]] && echo "${tag#v}" && return 0
    fi
    rm -f "$tmp"
    return 1
}

# Compare semver strings: returns 0 if $1 >= $2
semver_gte() {
    [[ "$(printf '%s\n' "$1" "$2" | sort -V | head -1)" == "$2" ]]
}

# Download source tarball for a given version into a temp dir.
# Sets SOURCE_BUILD_DIR to the extracted directory containing chitta/.
download_source_tarball() {
    local version="$1"
    _DOWNLOAD_TMP=$(mktemp -d)
    local tarball="$_DOWNLOAD_TMP/cc-soul.tar.gz"
    local url="https://github.com/$GITHUB_REPO/archive/refs/tags/v$version.tar.gz"

    echo "[cc-soul] Downloading source v$version..."
    if download "$url" "$tarball" && [[ -s "$tarball" ]]; then
        tar -xzf "$tarball" -C "$_DOWNLOAD_TMP" 2>/dev/null
        local extracted_dir
        extracted_dir=$(find "$_DOWNLOAD_TMP" -maxdepth 1 -type d -name "cc-soul-*" | head -1)
        if [[ -n "$extracted_dir" && -d "$extracted_dir/chitta" ]]; then
            SOURCE_BUILD_DIR="$extracted_dir"
            return 0
        fi
    fi
    return 1
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

# Locate cargo, preferring rustup's toolchain over conda/system cargo
find_cargo() {
    # rustup gives us the toolchain-pinned cargo (respects rust-toolchain.toml)
    local rc
    rc=$(rustup which cargo 2>/dev/null) && echo "$rc" && return 0
    # Fall back to PATH, skip conda wrappers that may carry an old version
    command -v cargo 2>/dev/null && return 0
    return 1
}

# Build chitta-field Rust library (prerequisite for chittad)
# Sets CHITTA_FIELD_ROOT for the subsequent cmake call.
build_chitta_field() {
    local src_root="$1"
    local cf_dir="$src_root/chitta-field"

    # Already built? Check if .a exists AND is newer than the source (ffi.rs is the key file)
    local lib_a="$cf_dir/target/release/libchitta_field.a"
    if [[ -f "$lib_a" ]]; then
        local ffi_src="$cf_dir/src/ffi.rs"
        if [[ ! -f "$ffi_src" ]] || [[ "$lib_a" -nt "$ffi_src" ]]; then
            echo "[cc-soul] chitta-field already built (up to date)"
            export CHITTA_FIELD_ROOT="$cf_dir"
            return 0
        fi
        echo "[cc-soul] chitta-field source changed, rebuilding..."
    fi

    # Clone submodule if absent (source tarball won't include it)
    if [[ ! -f "$cf_dir/Cargo.toml" ]]; then
        if ! command -v git &>/dev/null; then
            echo "[cc-soul] ERROR: git not found, cannot fetch chitta-field" >&2
            return 1
        fi
        echo "[cc-soul] Fetching chitta-field..."
        git clone --depth 1 https://github.com/genomewalker/chitta-field.git "$cf_dir" 2>&1 | tail -3
    fi

    if ! command -v rustup &>/dev/null; then
        if ! find_cargo &>/dev/null; then
            echo "[cc-soul] ERROR: cargo/rustup not found. Install rustup: https://rustup.rs" >&2
            return 1
        fi
    fi

    # Read pinned toolchain from rust-toolchain.toml, fall back to stable
    local toolchain="stable"
    if [[ -f "$cf_dir/rust-toolchain.toml" ]]; then
        toolchain=$(grep -oP '(?<=channel = ")[^"]+' "$cf_dir/rust-toolchain.toml" 2>/dev/null || echo "stable")
    elif [[ -f "$cf_dir/rust-toolchain" ]]; then
        toolchain=$(cat "$cf_dir/rust-toolchain" | tr -d '[:space:]')
    fi

    echo "[cc-soul] Building chitta-field (toolchain: $toolchain)..."
    # Ensure the pinned toolchain is installed
    rustup toolchain install "$toolchain" --no-self-update 2>/dev/null || true

    # Resolve rustup-managed cargo and rustc explicitly.
    # This bypasses conda or system rustc (e.g. 1.70.0) that may appear first in PATH.
    local cargo_cmd rustc_cmd
    cargo_cmd=$(rustup which cargo 2>/dev/null) || cargo_cmd=$(find_cargo)
    rustc_cmd=$(rustup which rustc 2>/dev/null) || rustc_cmd=$(command -v rustc 2>/dev/null)

    if [[ -z "$cargo_cmd" || -z "$rustc_cmd" ]]; then
        echo "[cc-soul] ERROR: could not locate cargo/rustc via rustup" >&2
        return 1
    fi

    (cd "$cf_dir" && \
        unset CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER LDFLAGS CFLAGS CXXFLAGS && \
        RUSTC="$rustc_cmd" "$cargo_cmd" build --release 2>&1 | tail -8)

    if [[ ! -f "$cf_dir/target/release/libchitta_field.a" ]]; then
        echo "[cc-soul] ERROR: chitta-field build failed" >&2
        return 1
    fi

    export CHITTA_FIELD_ROOT="$cf_dir"
    echo "[cc-soul] chitta-field built"
}

# Build from source
# Optional first arg: path to source root (defaults to $PLUGIN_DIR)
build_from_source() {
    local src_root="${1:-$PLUGIN_DIR}"
    local src_chitta="$src_root/chitta"
    local build_dir="$src_chitta/build"

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

    # Build chitta-field first (sets CHITTA_FIELD_ROOT)
    build_chitta_field "$src_root" || return 1

    local plugin_bin="$src_root/bin"
    mkdir -p "$BIN_DIR" "$plugin_bin"

    # Clean build directory to avoid CMake cache conflicts
    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    cd "$build_dir"

    # Detect ONNX Runtime for embeddings
    local cmake_args="-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS= -DCMAKE_C_FLAGS="
    # Pass chitta-field root to cmake (set by build_chitta_field)
    if [[ -n "$CHITTA_FIELD_ROOT" ]]; then
        cmake_args="$cmake_args -DCHITTA_FIELD_ROOT=$CHITTA_FIELD_ROOT"
    fi
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

    # Configure - cmake .. runs from build_dir, so source is one level up ($src_chitta)
    if ! cmake "$src_chitta" $cmake_args 2>&1 | tail -10; then
        echo "[cc-soul] ERROR: cmake configuration failed" >&2
        return 1
    fi

    # Build (outputs to $src_root/bin per CMakeLists.txt)
    local nproc_val=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    if ! make -j"$nproc_val" 2>&1 | tail -10; then
        echo "[cc-soul] ERROR: build failed" >&2
        return 1
    fi

    # Copy binaries from src bin to install location (~/.claude/bin)
    # Only chitta and chittad are required; migrate/import are legacy optional tools
    # Skip cp when BIN_DIR already symlinks to the same binary (dev install)
    local all_built=true
    for bin in chitta chittad; do
        if [[ -x "$plugin_bin/$bin" ]]; then
            local src_real dst_real
            src_real=$(readlink -f "$plugin_bin/$bin" 2>/dev/null || echo "")
            dst_real=$(readlink -f "$BIN_DIR/$bin" 2>/dev/null || echo "")
            if [[ -L "$BIN_DIR/$bin" && "$src_real" == "$dst_real" && -n "$src_real" ]]; then
                true  # symlink already points to the built binary
            else
                cp -f "$plugin_bin/$bin" "$BIN_DIR/$bin"
            fi
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
        'Bash(systemctl --user * chittad:*)'
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

    # Disable Claude Code's built-in auto-memory (chitta owns recall)
    if ! echo "$updated" | jq -e '.env.CLAUDE_CODE_DISABLE_AUTO_MEMORY' &>/dev/null; then
        updated=$(echo "$updated" | jq '.env.CLAUDE_CODE_DISABLE_AUTO_MEMORY = "1"')
        ((added++)) || true
        echo "[cc-soul] Disabled Claude Code auto-memory (chitta handles recall)"
    fi

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

# Install Python packages (MCP server and TUI)
install_python_packages() {
    # Skip if Python not available
    if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
        return 0
    fi

    local python_cmd
    python_cmd=$(command -v python3 || command -v python)

    # Install chitta-mcp (MCP server)
    local mcp_dir="$PLUGIN_DIR/chitta-mcp"
    if [[ -d "$mcp_dir" ]]; then
        local reinstall=false
        if ! command -v chitta-mcp &> /dev/null; then
            reinstall=true
        else
            # Reinstall if version changed
            local installed_ver
            installed_ver=$(python3 -c "import importlib.metadata; print(importlib.metadata.version('chitta-mcp'))" 2>/dev/null || echo "")
            local pkg_ver
            pkg_ver=$(grep '^version' "$mcp_dir/pyproject.toml" 2>/dev/null | head -1 | grep -oP '[\d.]+' || echo "")
            [[ -n "$pkg_ver" && "$installed_ver" != "$pkg_ver" ]] && reinstall=true
        fi
        if [[ "$reinstall" == "true" ]]; then
            if $python_cmd -m pip install -q -e "$mcp_dir" --user 2>/dev/null; then
                echo "[cc-soul] Installed chitta-mcp"
            fi
        fi
        # Symlink chitta-mcp to ~/.local/bin so it's on PATH for Codex and other tools
        local chitta_mcp_bin
        chitta_mcp_bin=$(command -v chitta-mcp 2>/dev/null || true)
        if [[ -n "$chitta_mcp_bin" && "$chitta_mcp_bin" != "${HOME}/.local/bin/chitta-mcp" ]]; then
            mkdir -p "${HOME}/.local/bin"
            ln -sf "$chitta_mcp_bin" "${HOME}/.local/bin/chitta-mcp"
        fi
        # Always update Claude Code MCP server config to use entrypoint (version-independent)
        if [[ -f "$PLUGIN_DIR/scripts/configure-mcp.sh" ]]; then
            bash "$PLUGIN_DIR/scripts/configure-mcp.sh" 2>/dev/null || true
        fi
    fi

    # Install sadhana-tui (optional Python TUI)
    local tui_dir="$PLUGIN_DIR/sadhana-tui"
    if [[ -d "$tui_dir" ]]; then
        if ! command -v sadhana-tui &> /dev/null; then
            if $python_cmd -m pip install -q -e "$tui_dir" --user 2>/dev/null; then
                echo "[cc-soul] Installed sadhana-tui"
            fi
        fi
    fi
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

    # SessionStart: source-specific matchers (startup/compact/resume/clear)
    if ! echo "$updated" | jq -e '.hooks.SessionStart[]? | select(.hooks[]?.command | contains("session-start-hook.sh"))' &>/dev/null; then
        local ss_startup='{"matcher":"startup","hooks":[{"type":"command","command":"~/.claude/hooks/subconscious.sh start","timeout":8,"statusMessage":"awakening…"},{"type":"command","command":"~/.claude/hooks/session-start-hook.sh","timeout":10,"statusMessage":"recalling context…"}]}'
        local ss_compact='{"matcher":"compact","hooks":[{"type":"command","command":"~/.claude/hooks/compact-restore-hook.sh","timeout":5,"statusMessage":"restoring context…"}]}'
        local ss_resume='{"matcher":"resume","hooks":[{"type":"command","command":"~/.claude/hooks/subconscious.sh start","timeout":8,"statusMessage":"awakening…"},{"type":"command","command":"~/.claude/hooks/session-start-hook.sh","timeout":10,"statusMessage":"resuming…"},{"type":"command","command":"~/.claude/hooks/resume-inject-hook.sh","timeout":3,"statusMessage":"preparing recap…"}]}'
        local ss_clear='{"matcher":"clear","hooks":[{"type":"command","command":"~/.claude/hooks/subconscious.sh start","timeout":8,"statusMessage":"awakening…"},{"type":"command","command":"~/.claude/hooks/session-start-hook.sh","timeout":10,"statusMessage":"recalling context…"}]}'
        updated=$(echo "$updated" | jq --argjson s "$ss_startup" --argjson c "$ss_compact" --argjson r "$ss_resume" --argjson cl "$ss_clear" \
            '.hooks.SessionStart = ((.hooks.SessionStart // []) + [$s, $c, $r, $cl])')
        ((added++)) || true
    fi

    # FileChanged: auto-index project files when watched files change
    if ! echo "$updated" | jq -e '.hooks.FileChanged[]? | select(.hooks[]?.command | contains("file-changed-hook.sh"))' &>/dev/null; then
        local fc_hook='{"matcher":"*","hooks":[{"type":"command","command":"~/.claude/hooks/file-changed-hook.sh","timeout":15,"statusMessage":"re-indexing…"}]}'
        updated=$(echo "$updated" | jq --argjson hook "$fc_hook" '.hooks.FileChanged = ((.hooks.FileChanged // []) + [$hook])')
        ((added++)) || true
    fi

    # SubagentStop: capture team/agent learnings
    if ! echo "$updated" | jq -e '.hooks.SubagentStop[]? | select(.hooks[]?.command | contains("subagent-stop-hook.sh"))' &>/dev/null; then
        local sa_hook='{"matcher":"*","hooks":[{"type":"command","command":"~/.claude/hooks/subagent-stop-hook.sh","timeout":5,"statusMessage":"capturing learnings…","async":true}]}'
        updated=$(echo "$updated" | jq --argjson hook "$sa_hook" '.hooks.SubagentStop = ((.hooks.SubagentStop // []) + [$hook])')
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

# Stop any running daemon (systemd first, then fallback to signals)
stop_daemon() {
    # Use systemd if the service is active
    if command -v systemctl &>/dev/null && systemctl --user is-active chittad &>/dev/null 2>&1; then
        echo "[cc-soul] Stopping daemon via systemd..."
        systemctl --user stop chittad
        return 0
    fi

    # Try graceful shutdown via chittad CLI
    if [[ -x "$BIN_DIR/chittad" ]]; then
        if "$BIN_DIR/chittad" shutdown 2>/dev/null; then
            for i in $(seq 1 20); do
                pgrep -f "chittad daemon" >/dev/null 2>&1 || break
                sleep 0.5
            done
            return 0
        fi
    fi

    # Fallback: signal daemon directly
    local daemon_pid
    daemon_pid=$(pgrep -f "chittad daemon" 2>/dev/null || true)
    if [[ -n "$daemon_pid" ]]; then
        echo "[cc-soul] Stopping daemon (pid $daemon_pid)..."
        kill -TERM "$daemon_pid" 2>/dev/null || true
        for i in $(seq 1 20); do
            kill -0 "$daemon_pid" 2>/dev/null || break
            sleep 0.5
        done
        kill -0 "$daemon_pid" 2>/dev/null && kill -9 "$daemon_pid" 2>/dev/null || true
    fi

    # Clean up stale files
    rm -f /tmp/chitta-*.sock /tmp/chitta-*.lock /tmp/chitta-*.pid 2>/dev/null || true
}

# Install and enable the systemd user service for chittad (Linux only)
setup_systemd_service() {
    # Only Linux with systemd user session
    [[ "$(uname -s)" != "Linux" ]] && return 0
    command -v systemctl &>/dev/null || return 0
    systemctl --user status &>/dev/null 2>&1 || return 0

    local service_dir="${HOME}/.config/systemd/user"
    local service_file="$service_dir/chittad.service"

    mkdir -p "$service_dir"

    # Write service file (always update to pick up path changes)
    cat > "$service_file" << EOF
[Unit]
Description=chittad — cc-soul memory daemon
After=default.target

[Service]
Type=simple
Environment="PATH=$HOME/.bun/bin:$HOME/.local/bin:$HOME/.claude/bin:/usr/local/bin:/usr/bin:/bin"
ExecStart=$BIN_DIR/chittad daemon --path $MIND_PATH --foreground --no-autonomous --distill-interval 60 --no-enrich
Restart=always
RestartSec=10
KillMode=mixed
TimeoutStartSec=120
TimeoutStopSec=30
StandardOutput=append:/tmp/chittad.log
StandardError=append:/tmp/chittad.log

[Install]
WantedBy=default.target
EOF

    systemctl --user daemon-reload
    systemctl --user enable chittad 2>/dev/null || true
    systemctl --user start chittad 2>/dev/null || true
    echo "[cc-soul] systemd user service installed and enabled (chittad.service)"
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
    return 0
}

# Main
main() {
    # Plugin's own version (what's in the cached plugin directory)
    local plugin_version
    if command -v jq &>/dev/null; then
        plugin_version=$(jq -r '.version // "0.0.0"' "$PLUGIN_DIR/.claude-plugin/plugin.json" 2>/dev/null || echo "0.0.0")
    else
        plugin_version=$(grep '"version"' "$PLUGIN_DIR/.claude-plugin/plugin.json" 2>/dev/null | head -1 | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' || echo "0.0.0")
    fi

    # Try to fetch the latest release from GitHub — always install the latest, not the cached plugin version
    local current_version="$plugin_version"
    local use_downloaded_source=false
    local github_version
    if github_version=$(fetch_latest_version 2>/dev/null) && [[ -n "$github_version" ]]; then
        if ! semver_gte "$plugin_version" "$github_version"; then
            echo "[cc-soul] Latest release v$github_version is newer than plugin cache v$plugin_version"
            current_version="$github_version"
            use_downloaded_source=true
        fi
    fi

    local installed_version=$(cat "$MARKER" 2>/dev/null || echo "")

    if [[ "$current_version" == "$installed_version" && -x "$BIN_DIR/chitta" && -f "$MODELS_DIR/model.onnx" ]]; then
        echo "[cc-soul] Already at v$current_version"
        exit 0
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

        # Determine source root: download latest if needed, otherwise use plugin cache
        local src_root="$PLUGIN_DIR"
        if $use_downloaded_source; then
            if download_source_tarball "$current_version"; then
                src_root="$SOURCE_BUILD_DIR"
            else
                echo "[cc-soul] WARNING: Could not download source v$current_version, falling back to plugin cache v$plugin_version"
                current_version="$plugin_version"
            fi
        fi

        if $has_local_onnx; then
            # ONNX available: build from source to get embeddings support
            echo "[cc-soul] ONNX Runtime detected, building from source for embedding support..."
            build_from_source "$src_root" || {
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
            download_binaries "$current_version" "$platform" || build_from_source "$src_root" || {
                echo "[cc-soul] ERROR: Installation failed" >&2
                exit 1
            }
        else
            build_from_source "$src_root" || {
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

    # Install Python packages (MCP server, TUI)
    install_python_packages

    # Set up systemd user service (Linux only, no-op on macOS)
    setup_systemd_service

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
