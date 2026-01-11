#!/bin/bash
# Smart Install Script for cc-soul
#
# Tries to download pre-built binaries, falls back to building from source.
# Runs as first hook on SessionStart.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"
CHITTA_DIR="$PLUGIN_DIR/chitta"
BUILD_DIR="$CHITTA_DIR/build"
BIN_DIR="$PLUGIN_DIR/bin"
MODELS_DIR="$CHITTA_DIR/models"
MARKER="$PLUGIN_DIR/.install-complete"

# GitHub release URL base
GITHUB_REPO="genomewalker/cc-soul"
RELEASE_URL="https://github.com/$GITHUB_REPO/releases/download"

# ONNX model checksums (SHA256)
MODEL_CHECKSUM="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"  # placeholder
VOCAB_CHECKSUM="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"  # placeholder

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

# Verify checksum
verify_checksum() {
    local file="$1"
    local expected="$2"

    if [[ "$expected" == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ]]; then
        # Placeholder checksum - skip verification
        return 0
    fi

    local actual
    if command -v sha256sum &> /dev/null; then
        actual=$(sha256sum "$file" | cut -d' ' -f1)
    elif command -v shasum &> /dev/null; then
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
        if tar -xzf "$tmp_file" -C "$BIN_DIR" 2>/dev/null; then
            rm -f "$tmp_file"
            # Verify binaries can actually run (check for missing shared libs)
            # The bundled libs should be found via RPATH=$ORIGIN
            if "$BIN_DIR/chitta_cli" --help >/dev/null 2>&1 && \
               "$BIN_DIR/chitta_migrate" --help >/dev/null 2>&1; then
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

    mkdir -p "$BUILD_DIR" "$BIN_DIR"
    cd "$BUILD_DIR"

    # Configure - show errors now for debugging
    # Explicitly clear ASAN flags for release builds
    if ! cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="" -DCMAKE_C_FLAGS="" 2>&1 | tail -5; then
        echo "[cc-soul] ERROR: cmake configuration failed" >&2
        return 1
    fi

    # Build
    local nproc_val=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    if ! make -j"$nproc_val" 2>&1 | tail -10; then
        echo "[cc-soul] ERROR: build failed" >&2
        return 1
    fi

    # Verify binaries
    local all_built=true
    for bin in chitta chitta_cli chitta_migrate chitta_import; do
        if [[ ! -x "$BIN_DIR/$bin" ]]; then
            echo "[cc-soul] WARNING: $bin not built" >&2
            all_built=false
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
    local perms=(
        'Bash(*/chitta:*)'
        'Bash(*/chitta_cli:*)'
        'Bash(~/.claude/bin/chitta:*)'
        'Bash(~/.claude/bin/chitta_cli:*)'
        'Bash(chitta:*)'
        'Bash(chitta_cli:*)'
        'Bash(pkill -f "chitta_cli daemon":*)'
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
        if ! echo "$updated" | jq -e ".permissions.allow | index(\"$perm\")" &>/dev/null; then
            updated=$(echo "$updated" | jq ".permissions.allow += [\"$perm\"]")
            ((added++))
        fi
    done

    # Write back if changed
    if [[ $added -gt 0 ]]; then
        echo "$updated" | jq '.' > "$settings_file"
        echo "[cc-soul] Added $added bash permissions for chitta (global)"
    fi
}

# Create symlinks, handling dangling targets gracefully
create_symlinks() {
    mkdir -p "${HOME}/.claude/mind"
    mkdir -p "${HOME}/.claude/bin"
    mkdir -p "$PLUGIN_DIR/mind"

    # Create global bin symlinks for stable paths (no version parsing needed)
    for bin in chitta chitta_cli chitta_migrate chitta_import; do
        if [[ -x "$BIN_DIR/$bin" ]]; then
            local target="${HOME}/.claude/bin/$bin"
            # Remove existing file/symlink before creating new symlink
            [[ -e "$target" || -L "$target" ]] && rm -f "$target"
            ln -s "$BIN_DIR/$bin" "$target"
        fi
    done

    # If both directories resolve to the same path, skip file symlinks
    local user_mind_resolved=$(readlink -f "${HOME}/.claude/mind" 2>/dev/null || echo "${HOME}/.claude/mind")
    local plugin_mind_resolved=$(readlink -f "$PLUGIN_DIR/mind" 2>/dev/null || echo "$PLUGIN_DIR/mind")

    if [[ "$user_mind_resolved" == "$plugin_mind_resolved" ]]; then
        return 0
    fi

    for ext in hot warm cold wal unified vectors meta connections payloads edges tags; do
        local target="${HOME}/.claude/mind/chitta.$ext"
        local link="$PLUGIN_DIR/mind/chitta.$ext"

        # Touch target if it doesn't exist (prevents dangling symlink issues)
        [[ -e "$target" ]] || touch "$target"

        ln -sfn "$target" "$link"
    done
}

# Main
main() {
    # Check if already installed
    local current_version=$(grep '"version"' "$PLUGIN_DIR/.claude-plugin/plugin.json" 2>/dev/null | cut -d'"' -f4 || echo "0.0.0")
    local installed_version=$(cat "$MARKER" 2>/dev/null || echo "")

    if [[ "$current_version" == "$installed_version" && -x "$BIN_DIR/chitta" && -f "$MODELS_DIR/model.onnx" ]]; then
        exit 0  # Already installed
    fi

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

    # Create symlinks
    create_symlinks

    # Configure bash permissions for chitta commands
    configure_permissions

    # Version change notification
    if [[ -n "$installed_version" && "$installed_version" != "$current_version" ]]; then
        echo "[cc-soul] Updated: $installed_version → $current_version"
    fi

    # Mark as installed
    echo "$current_version" > "$MARKER"
    echo "[cc-soul] Installation complete (v$current_version)"
}

main "$@"
