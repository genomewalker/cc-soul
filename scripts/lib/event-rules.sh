#!/bin/bash
# Event Rules Library - Prefilter rules for high-signal event capture
#
# Functions:
#   is_high_signal_bash <cmd> <cwd>  - Check if bash command is worth capturing
#   is_high_signal_read <path>       - Check if file read is worth capturing
#   is_high_signal_edit <path>       - Check if file edit is worth capturing
#   get_bash_signal <cmd>            - Get signal type for bash command
#   get_read_signal <path>           - Get signal type for file read
#   get_edit_signal <path>           - Get signal type for file edit
#   is_repeated <key>                - Check if event was seen recently
#   track_event <key>                - Record event for repetition tracking

SESSION_CACHE="${HOME}/.claude/mind/.event_session_cache"
REPEAT_WINDOW=300  # 5 minutes

# Initialize session cache
mkdir -p "$(dirname "$SESSION_CACHE")"
touch "$SESSION_CACHE"

# Clean old entries from session cache (older than REPEAT_WINDOW)
cleanup_session_cache() {
    local now=$(date +%s)
    local cutoff=$((now - REPEAT_WINDOW))
    if [[ -f "$SESSION_CACHE" ]]; then
        awk -F: -v cutoff="$cutoff" '$2 > cutoff' "$SESSION_CACHE" > "${SESSION_CACHE}.tmp" 2>/dev/null
        mv "${SESSION_CACHE}.tmp" "$SESSION_CACHE" 2>/dev/null || true
    fi
}

# Track event for repetition detection
track_event() {
    local key="$1"
    local ts=$(date +%s)
    echo "${key}:${ts}" >> "$SESSION_CACHE"
    cleanup_session_cache
}

# Check if event was seen recently (returns count)
is_repeated() {
    local key="$1"
    local now=$(date +%s)
    local cutoff=$((now - REPEAT_WINDOW))
    local count=0

    if [[ -f "$SESSION_CACHE" ]]; then
        count=$(grep -c "^${key}:" "$SESSION_CACHE" 2>/dev/null || echo 0)
    fi

    [[ $count -ge 2 ]]
}

# Check if bash command is high-signal
is_high_signal_bash() {
    local cmd="$1"
    local cwd="$2"

    # Exclusions (noise)
    case "$cmd" in
        ls*|pwd|echo*|cat\ *|head\ *|tail\ *|wc\ *)
            return 1
            ;;
        cd\ *)
            return 1
            ;;
    esac

    # High-signal patterns
    case "$cmd" in
        # Script executions
        *scripts/*.sh*|*.scripts/*.sh*|./scripts/*|./.scripts/*)
            return 0
            ;;
        # Release/deploy commands
        *release*|*deploy*|*publish*)
            return 0
            ;;
        # Git operations (tags, push)
        git\ tag*|git\ push*)
            return 0
            ;;
        # Build commands
        cmake*|make*|cargo\ build*|npm\ run\ build*|go\ build*)
            return 0
            ;;
        # Test commands
        pytest*|npm\ test*|cargo\ test*|go\ test*|make\ test*)
            return 0
            ;;
        # Package management
        npm\ install*|pip\ install*|cargo\ add*)
            return 0
            ;;
        # Docker
        docker\ build*|docker\ push*|docker-compose\ up*)
            return 0
            ;;
    esac

    return 1
}

# Get signal type for bash command
get_bash_signal() {
    local cmd="$1"

    case "$cmd" in
        *scripts/*.sh*|*.scripts/*.sh*)
            echo "script_run"
            ;;
        *release*|*deploy*|*publish*)
            echo "release"
            ;;
        git\ tag*|git\ push*)
            echo "git_release"
            ;;
        cmake*|make*|*build*)
            echo "build"
            ;;
        pytest*|*test*)
            echo "test"
            ;;
        npm\ install*|pip\ install*|cargo\ add*)
            echo "dependency"
            ;;
        docker*)
            echo "docker"
            ;;
        *)
            echo "command"
            ;;
    esac
}

# Check if file read is high-signal
is_high_signal_read() {
    local path="$1"

    # Exclusions
    case "$path" in
        *node_modules/*|*.git/*|*__pycache__/*|*/.cache/*)
            return 1
            ;;
        *.pyc|*.o|*.so|*.dylib)
            return 1
            ;;
    esac

    # High-signal patterns
    case "$path" in
        # Scripts
        *.sh|*scripts/*|*.scripts/*)
            return 0
            ;;
        # Config files
        *.json|*.yaml|*.yml|*.toml|*.ini|*.conf)
            return 0
            ;;
        # Documentation
        *README*|*CLAUDE*|*CONTRIBUTING*|*CHANGELOG*)
            return 0
            ;;
        # Makefiles
        Makefile|CMakeLists.txt|*.cmake)
            return 0
            ;;
        # Package manifests
        package.json|Cargo.toml|pyproject.toml|go.mod)
            return 0
            ;;
        # CI/CD
        *.github/workflows/*|.gitlab-ci.yml|Jenkinsfile)
            return 0
            ;;
    esac

    return 1
}

# Get signal type for file read
get_read_signal() {
    local path="$1"

    case "$path" in
        *.sh|*scripts/*)
            echo "script_read"
            ;;
        *.json|*.yaml|*.yml|*.toml)
            echo "config_read"
            ;;
        *README*|*CLAUDE*)
            echo "doc_read"
            ;;
        Makefile|CMakeLists.txt)
            echo "build_config_read"
            ;;
        package.json|Cargo.toml|pyproject.toml)
            echo "manifest_read"
            ;;
        *.github/workflows/*)
            echo "ci_read"
            ;;
        *)
            echo "file_read"
            ;;
    esac
}

# Check if file edit is high-signal
is_high_signal_edit() {
    local path="$1"

    # Exclusions (same as read)
    case "$path" in
        *node_modules/*|*.git/*|*__pycache__/*|*/.cache/*)
            return 1
            ;;
        *.pyc|*.o|*.so|*.dylib)
            return 1
            ;;
    esac

    # All edits to non-excluded files are somewhat interesting
    # But prioritize certain types
    case "$path" in
        # High priority
        *.sh|*scripts/*|*.json|*.yaml|*.yml|*.toml)
            return 0
            ;;
        *README*|*CLAUDE*|Makefile|CMakeLists.txt)
            return 0
            ;;
        # Source code edits (medium priority, but capture)
        *.py|*.js|*.ts|*.go|*.rs|*.cpp|*.hpp|*.c|*.h)
            return 0
            ;;
    esac

    return 1
}

# Get signal type for file edit
get_edit_signal() {
    local path="$1"

    case "$path" in
        *.sh|*scripts/*)
            echo "script_edit"
            ;;
        *.json|*.yaml|*.yml|*.toml)
            echo "config_edit"
            ;;
        *README*|*CLAUDE*)
            echo "doc_edit"
            ;;
        *.py|*.js|*.ts|*.go|*.rs|*.cpp|*.hpp)
            echo "code_edit"
            ;;
        *)
            echo "file_edit"
            ;;
    esac
}
