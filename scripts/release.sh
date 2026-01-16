#!/bin/bash
# Release automation script for cc-soul
#
# Usage:
#   ./scripts/release.sh patch   # Bug fixes (2.56.0 → 2.56.1)
#   ./scripts/release.sh minor   # New features (2.56.0 → 2.57.0)
#   ./scripts/release.sh major   # Breaking changes (2.56.0 → 3.0.0)
#   ./scripts/release.sh 2.57.0  # Explicit version
#
# SemVer Guidelines:
#   MAJOR: Breaking changes (protocol change, removed tool, renamed param)
#   MINOR: New features, backward compatible (new tool, new param, new skill)
#   PATCH: Bug fixes, no new features (fix crash, fix logic error)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
cd "$REPO_DIR"

# Get current version from version.hpp
get_current_version() {
    grep -oP '#define CHITTA_VERSION "\K[^"]+' chitta/include/chitta/version.hpp
}

# Bump version based on type
bump_version() {
    local current="$1"
    local type="$2"

    IFS='.' read -r major minor patch <<< "$current"

    case "$type" in
        major)
            echo "$((major + 1)).0.0"
            ;;
        minor)
            echo "$major.$((minor + 1)).0"
            ;;
        patch)
            echo "$major.$minor.$((patch + 1))"
            ;;
        *)
            echo "$type"  # Explicit version
            ;;
    esac
}

# Validate version format
validate_version() {
    [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

# Main
BUMP_TYPE="$1"

if [[ -z "$BUMP_TYPE" ]]; then
    echo "Usage: $0 <patch|minor|major|X.Y.Z>"
    echo ""
    echo "SemVer Guidelines:"
    echo "  patch  Bug fixes only (2.56.0 → 2.56.1)"
    echo "  minor  New features, backward compatible (2.56.0 → 2.57.0)"
    echo "  major  Breaking changes (2.56.0 → 3.0.0)"
    echo ""
    echo "Examples:"
    echo "  $0 patch   # Fixed a bug"
    echo "  $0 minor   # Added new tool"
    echo "  $0 major   # Changed protocol"
    exit 1
fi

CURRENT_VERSION=$(get_current_version)
NEW_VERSION=$(bump_version "$CURRENT_VERSION" "$BUMP_TYPE")

if ! validate_version "$NEW_VERSION"; then
    echo "Error: Invalid version format: $NEW_VERSION"
    echo "Version must be in format X.Y.Z"
    exit 1
fi

echo "=== Release: $CURRENT_VERSION → $NEW_VERSION ==="

# Check for uncommitted changes
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "Error: Uncommitted changes. Commit or stash first."
    exit 1
fi

# Confirm
read -p "Proceed with release v$NEW_VERSION? [y/N] " confirm
if [[ "$confirm" != "y" && "$confirm" != "Y" ]]; then
    echo "Aborted."
    exit 0
fi

# Update version.hpp
echo "Updating chitta/include/chitta/version.hpp..."
sed -i "s/#define CHITTA_VERSION \"[^\"]*\"/#define CHITTA_VERSION \"$NEW_VERSION\"/" \
    chitta/include/chitta/version.hpp

# Update plugin.json
echo "Updating .claude-plugin/plugin.json..."
sed -i "s/\"version\": \"[^\"]*\"/\"version\": \"$NEW_VERSION\"/" \
    .claude-plugin/plugin.json

# Verify updates
grep -q "\"$NEW_VERSION\"" chitta/include/chitta/version.hpp || { echo "version.hpp update failed"; exit 1; }
grep -q "\"$NEW_VERSION\"" .claude-plugin/plugin.json || { echo "plugin.json update failed"; exit 1; }

# Commit version bump
echo "Committing version bump..."
git add chitta/include/chitta/version.hpp .claude-plugin/plugin.json
git commit -m "chore: bump version to $NEW_VERSION"

# Create and push tag
echo "Creating tag v$NEW_VERSION..."
git tag "v$NEW_VERSION"

echo "Pushing to origin..."
git push origin main
git push origin "v$NEW_VERSION"

echo ""
echo "=== Release v$NEW_VERSION initiated ==="
echo ""
echo "GitHub Actions will build and publish:"
echo "  • linux-x64 binaries"
echo "  • macos-x64 binaries"
echo "  • macos-arm64 binaries"
echo "  • ONNX models"
echo ""
echo "Monitor: https://github.com/genomewalker/cc-soul/actions"
echo "Release: https://github.com/genomewalker/cc-soul/releases/tag/v$NEW_VERSION"
