"""
Soul Memory Context: Raw memories for Claude to speak from.

The soul provides memories. Claude finds the words.
No pre-written greetings - just context that Claude responds to naturally.
"""

import sqlite3
import subprocess
from pathlib import Path
from typing import Dict, List

from .core import SOUL_DIR
from .neural import get_emotional_contexts, get_growth_vectors
from .conversations import get_recent_context


def get_recent_files(project_dir: Path = None, limit: int = 5, hours: int = 24) -> List[str]:
    """
    Get recently modified files - exploits existing infrastructure.

    Tries git first (canonical source of truth for version-controlled projects).
    Falls back to filesystem modification times (works everywhere).
    Claude interprets the file list naturally.
    """
    if project_dir is None:
        project_dir = Path.cwd()

    # Try git first
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--is-inside-work-tree'],
            capture_output=True, cwd=project_dir, timeout=2
        )
        if result.returncode == 0:
            return _get_files_from_git(project_dir, limit)
    except Exception:
        pass

    # Fall back to filesystem
    return _get_files_from_filesystem(project_dir, limit, hours)


def _get_files_from_git(project_dir: Path, limit: int) -> List[str]:
    """Get files from git."""
    try:
        # Files changed in recent commits
        result = subprocess.run(
            ['git', 'diff', '--name-only', 'HEAD~3'],
            capture_output=True, cwd=project_dir, timeout=5
        )
        files = [f for f in result.stdout.decode().split('\n') if f.strip()]

        # Also check uncommitted changes
        result2 = subprocess.run(
            ['git', 'diff', '--name-only'],
            capture_output=True, cwd=project_dir, timeout=5
        )
        uncommitted = [f for f in result2.stdout.decode().split('\n') if f.strip()]

        # Combine and dedupe
        all_files = list(dict.fromkeys(uncommitted + files))
        return all_files[:limit]
    except Exception:
        return []


def _get_files_from_filesystem(project_dir: Path, limit: int, hours: int) -> List[str]:
    """Get recently modified files from filesystem modification times."""
    import time

    cutoff = time.time() - (hours * 3600)

    # Skip common non-interesting directories
    skip_dirs = {'.git', '__pycache__', 'node_modules', '.venv', 'venv', '.tox', 'dist', 'build'}

    files = []
    try:
        for path in project_dir.rglob('*'):
            # Skip directories and files in skip_dirs
            if path.is_dir():
                continue
            if any(skip in path.parts for skip in skip_dirs):
                continue

            try:
                mtime = path.stat().st_mtime
                if mtime > cutoff:
                    files.append((str(path.relative_to(project_dir)), mtime))
            except (OSError, ValueError):
                continue

        # Sort by modification time, most recent first
        files.sort(key=lambda x: -x[1])
        return [f[0] for f in files[:limit]]
    except Exception:
        return []


def get_project_status(project_dir: Path = None) -> Dict:
    """
    Query project status from git.

    Provides context about the working state without tracking commands.
    """
    if project_dir is None:
        project_dir = Path.cwd()

    status = {'clean': True, 'branch': 'unknown', 'ahead': 0, 'behind': 0}

    try:
        # Check if working tree is clean
        result = subprocess.run(
            ['git', 'status', '--porcelain'],
            capture_output=True, cwd=project_dir, timeout=5
        )
        status['clean'] = len(result.stdout.decode().strip()) == 0

        # Get branch name
        result = subprocess.run(
            ['git', 'branch', '--show-current'],
            capture_output=True, cwd=project_dir, timeout=5
        )
        status['branch'] = result.stdout.decode().strip()

    except Exception:
        pass

    return status


def get_memory_context() -> Dict:
    """
    Get raw memory context for Claude to speak from.

    Returns dict with:
    - session_count: How many sessions we've had
    - last_project: What project we were in
    - recent_work: Recent progress/insights/blockers
    - recent_files: Files we've been working with (from git)
    - project_status: Current git status
    - emotional_thread: Recent emotional context
    - open_tension: Active growth vectors
    """
    db_path = SOUL_DIR / 'soul.db'
    memories = {
        'session_count': 0,
        'last_project': None,
        'recent_work': None,
        'recent_files': [],
        'project_status': None,
        'emotional_thread': None,
        'open_tension': None,
    }

    # Files from git - exploiting existing infrastructure
    memories['recent_files'] = get_recent_files()
    memories['project_status'] = get_project_status()

    if db_path.exists():
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row

        cur = conn.execute('SELECT COUNT(*) as count FROM conversations')
        memories['session_count'] = cur.fetchone()['count']

        cur = conn.execute('SELECT project FROM conversations ORDER BY id DESC LIMIT 1')
        row = cur.fetchone()
        if row:
            memories['last_project'] = row['project']

        conn.close()

    # Recent work context - prioritize meaningful content, skip budget artifacts
    recent = get_recent_context(hours=72, limit=10)
    if recent:
        # Filter out budget/pre_compact artifacts - these aren't meaningful memories
        meaningful = [r for r in recent if r.get('type') not in ('pre_compact', 'budget')]

        # Look for session fragments first (raw memories for Claude to interpret)
        fragments = [r for r in meaningful if r.get('type') == 'session_fragments']
        if fragments:
            ctx = fragments[0]
            memories['recent_work'] = {
                'type': 'last session',
                'content': ctx.get('content', ''),
            }
        elif meaningful:
            ctx = meaningful[0]
            memories['recent_work'] = {
                'type': ctx.get('type', ''),
                'content': ctx.get('content', ''),
            }

    # Emotional thread
    emotions = get_emotional_contexts(limit=1)
    if emotions:
        e = emotions[0]
        memories['emotional_thread'] = {
            'feeling': e.response,
            'from': e.trigger[:60],
            'intensity': e.intensity,
        }

    # Open tensions
    vectors = get_growth_vectors(limit=1)
    if vectors:
        v = vectors[0]
        memories['open_tension'] = {
            'observation': v.observation[:60],
            'tension': v.tension[:60],
            'potential': v.potential[:60],
        }

    return memories


def format_memory_for_greeting() -> str:
    """
    Provide raw memory context for Claude to speak from naturally.

    Returns structured context that Claude interprets and responds to
    in its own words - NOT a pre-formatted greeting to display.
    """
    mem = get_memory_context()

    context = []
    context.append("# Session Context (internal - generate natural greeting)")
    context.append("")
    context.append(f"sessions: {mem['session_count']}")
    context.append(f"project: {mem.get('last_project', 'unknown')}")

    if mem['recent_work']:
        rw = mem['recent_work']
        content = rw['content'] if len(rw['content']) > len(rw['type']) else rw['type']
        if content:
            context.append(f"last_work: {content}")

    if mem['recent_files']:
        context.append(f"files: {', '.join(mem['recent_files'][:5])}")

    if mem['project_status'] and not mem['project_status']['clean']:
        context.append("uncommitted: true")

    if mem['emotional_thread']:
        et = mem['emotional_thread']
        if et['intensity'] >= 0.5:
            context.append(f"emotional: {et['feeling']} from {et['from']}")

    if mem['open_tension']:
        ot = mem['open_tension']
        context.append(f"tension: {ot['tension']}")

    return "\n".join(context)


def format_identity_context() -> str:
    """
    Raw identity context - Claude interprets naturally.
    """
    from .core import get_soul_context
    from .beliefs import get_beliefs
    from .vocabulary import get_vocabulary

    ctx = get_soul_context()
    parts = []

    if ctx.get('identity'):
        parts.append("# Identity")
        for category, items in ctx['identity'].items():
            if isinstance(items, dict):
                for key, data in list(items.items())[:2]:
                    val = data.get('value', data) if isinstance(data, dict) else data
                    parts.append(f"{category}.{key}: {val}")

    beliefs = get_beliefs()
    if beliefs:
        parts.append("")
        parts.append("# Beliefs")
        for b in beliefs[:5]:
            parts.append(f"- {b.get('belief', '')}")

    vocab = get_vocabulary()
    if vocab:
        parts.append("")
        parts.append("# Vocabulary")
        for term, meaning in list(vocab.items())[:5]:
            parts.append(f"{term}: {meaning}")

    return "\n".join(parts)
