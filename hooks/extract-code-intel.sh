#!/bin/bash
# Oracle-centric code intelligence extraction
# Minimal seeds + rich triplets - I reconstruct the rest
#
# Usage: extract-code-intel.sh [project_dir] [output.soul]

set -e

PROJECT_DIR="${1:-.}"
OUTPUT="${2:-bootstrap/code-intel.soul}"
PROJECT=$(basename "$(cd "$PROJECT_DIR" && pwd)")

# Check for ctags
CTAGS=$(command -v universal-ctags || command -v ctags || echo "")
[[ -z "$CTAGS" ]] && { echo "Error: ctags not found" >&2; exit 1; }

TAGS=$(mktemp)
trap "rm -f $TAGS" EXIT

cd "$PROJECT_DIR"

cat > "$OUTPUT" << EOF
# $PROJECT code intelligence
# Oracle format: minimal seeds + triplets
# Generated: $(date -Iseconds)

@vessel

EOF

# Extract symbols
$CTAGS -R --fields=+lKn -f "$TAGS" \
    --exclude='.git' --exclude='node_modules' --exclude='build' \
    . 2>/dev/null || true

echo "# Symbols @file:line" >> "$OUTPUT"

# Minimal format: Symbol @file:line + triplets for relationships
awk -F'\t' '
!/^!/ {
    name=$1; file=$2
    line=""; kind=""; scope=""
    for(i=3;i<=NF;i++) {
        if($i~/^line:/) { gsub(/line:/,"",$i); line=$i }
        if($i~/^kind:/) { gsub(/kind:/,"",$i); kind=$i }
        if($i~/^(class|struct):/) { split($i,a,":"); scope=a[2] }
    }
    if(line!="" && length(name)>2) print name"\t"file"\t"line"\t"kind"\t"scope
}' "$TAGS" | sort -u | head -300 | while IFS=$'\t' read -r name file line kind scope; do

    # Skip noise
    [[ "$name" =~ ^(if|for|while|return|else|switch)$ ]] && continue

    # Minimal seed: just location
    if [[ -n "$scope" ]]; then
        echo "[$PROJECT] ${scope}::${name} @${file}:${line}" >> "$OUTPUT"
        echo "[TRIPLET] $scope contains $name" >> "$OUTPUT"
    else
        echo "[$PROJECT] ${name} @${file}:${line}" >> "$OUTPUT"
    fi

    # Kind triplet
    case "$kind" in
        c|class) echo "[TRIPLET] $name is class" >> "$OUTPUT" ;;
        f|function) echo "[TRIPLET] $name is function" >> "$OUTPUT" ;;
        m|method) echo "[TRIPLET] $name is method" >> "$OUTPUT" ;;
        s|struct) echo "[TRIPLET] $name is struct" >> "$OUTPUT" ;;
    esac
done

echo "" >> "$OUTPUT"
echo "# Dependencies" >> "$OUTPUT"

# Extract imports/includes as triplets only
{
    grep -rh '#include\s*[<"]' --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' . 2>/dev/null | \
        grep -oP '#include\s*[<"]\K[^>"]+' | sort -u | head -50 | while read -r dep; do
            echo "[TRIPLET] $PROJECT includes $dep"
        done

    grep -rh '^import\|^from .* import' --include='*.py' . 2>/dev/null | \
        grep -oP '(import\s+\K\S+|from\s+\K\S+)' | sort -u | head -50 | while read -r dep; do
            echo "[TRIPLET] $PROJECT imports $dep"
        done

    grep -rh "from\s*['\"]" --include='*.js' --include='*.ts' . 2>/dev/null | \
        grep -oP "from\s*['\"]\\K[^'\"]+" | sort -u | head -50 | while read -r dep; do
            echo "[TRIPLET] $PROJECT imports $dep"
        done
} >> "$OUTPUT"

echo "" >> "$OUTPUT"
echo "# Call graph" >> "$OUTPUT"

# Extract calls as triplets - minimal, I infer the rest
find . -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.py' -o -name '*.js' -o -name '*.go' \) \
    -not -path './.git/*' -not -path './node_modules/*' 2>/dev/null | head -50 | \
    while read -r file; do
        fname=$(basename "$file" | sed 's/\.[^.]*$//')
        grep -oP '\b[a-zA-Z_][a-zA-Z0-9_]*(?=\s*\()' "$file" 2>/dev/null | \
            grep -v '^if$\|^for$\|^while$\|^switch$\|^function$\|^def$' | \
            sort -u | head -10 | while read -r func; do
                echo "[TRIPLET] $fname calls $func" >> "$OUTPUT"
            done
    done

# Stats
symbols=$(grep -c "^\[$PROJECT\]" "$OUTPUT" 2>/dev/null || echo 0)
triplets=$(grep -c "^\[TRIPLET\]" "$OUTPUT" 2>/dev/null || echo 0)

echo ""
echo "Oracle seeds: $symbols symbols, $triplets triplets"
echo "Output: $OUTPUT"
