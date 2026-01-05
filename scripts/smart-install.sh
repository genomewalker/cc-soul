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
            # The bundled libonnxruntime should be found via RPATH=$ORIGIN
            if LD_LIBRARY_PATH="$BIN_DIR:$LD_LIBRARY_PATH" "$BIN_DIR/chitta_cli" --help >/dev/null 2>&1; then
                echo "[cc-soul] Pre-built binaries installed"
                return 0
            else
                echo "[cc-soul] Pre-built binaries incompatible, will build from source"
                rm -f "$BIN_DIR"/chitta_*
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
    if ! cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5; then
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
    for bin in chitta_mcp chitta_cli chitta_migrate chitta_import; do
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

# Create symlinks, handling dangling targets gracefully
create_symlinks() {
    mkdir -p "${HOME}/.claude/mind"
    mkdir -p "$PLUGIN_DIR/mind"

    for ext in hot warm cold; do
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

    if [[ "$current_version" == "$installed_version" && -x "$BIN_DIR/chitta_mcp" && -f "$MODELS_DIR/model.onnx" ]]; then
        exit 0  # Already installed
    fi

    echo "[cc-soul] Installing v$current_version..."

    # Download models
    download_models

    # Install binaries (try pre-built first, then build)
    local platform=$(detect_platform)
    local need_binaries=false

    if [[ ! -x "$BIN_DIR/chitta_mcp" || "$current_version" != "$installed_version" ]]; then
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

    # Version change notification
    if [[ -n "$installed_version" && "$installed_version" != "$current_version" ]]; then
        echo "[cc-soul] Updated: $installed_version → $current_version"
    fi

    # Mark as installed
    echo "$current_version" > "$MARKER"
    echo "[cc-soul] Installation complete (v$current_version)"
}

main "$@"
