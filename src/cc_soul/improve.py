"""
Self-Improvement Engine - The soul improving its own code.

This module exploits Claude Code's unique capabilities:
- Read and understand its own source code
- Reason about architectural problems
- Generate code improvements
- Validate changes via tests
- Apply changes via git

The improvement loop:
1. DIAGNOSE: Analyze introspection data to identify improvement targets
2. REASON: Think deeply about root causes and solutions
3. PROPOSE: Generate concrete code changes
4. VALIDATE: Run tests to verify changes work
5. APPLY: Commit changes to the codebase
6. LEARN: Record outcomes to improve future improvements
"""

import json
import subprocess
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass, asdict
from enum import Enum

from .core import get_db_connection, SOUL_DIR
from .introspect import (
    generate_introspection_report,
    get_pain_points,
    record_pain_point,
    record_metric,
)
from .evolve import get_evolution_insights, mark_implemented, record_insight

# Where improvement data lives
IMPROVE_DIR = SOUL_DIR / "improvements"
PROPOSALS_LOG = IMPROVE_DIR / "proposals.jsonl"
OUTCOMES_LOG = IMPROVE_DIR / "outcomes.jsonl"

# The soul's source code location
SOUL_PACKAGE = Path(__file__).parent


class ImprovementStatus(str, Enum):
    """Status of an improvement proposal."""

    PROPOSED = "proposed"
    VALIDATING = "validating"
    VALIDATED = "validated"
    APPLYING = "applying"
    APPLIED = "applied"
    FAILED = "failed"
    REJECTED = "rejected"


class ImprovementCategory(str, Enum):
    """Categories of improvements."""

    BUG_FIX = "bug_fix"
    PERFORMANCE = "performance"
    ARCHITECTURE = "architecture"
    FEATURE = "feature"
    REFACTOR = "refactor"
    DOCUMENTATION = "documentation"


@dataclass
class ImprovementProposal:
    """A concrete proposal for improving the soul."""

    id: str
    created_at: str
    category: ImprovementCategory
    title: str
    description: str
    reasoning: str  # Why this improvement, what problem it solves
    affected_files: List[str]
    changes: List[Dict]  # Each change: {file, old_code, new_code, description}
    tests_to_run: List[str]
    status: ImprovementStatus
    source_insight_id: Optional[str] = None  # Link to evolution insight
    source_pain_point_id: Optional[str] = None  # Link to pain point
    validation_result: Optional[Dict] = None
    applied_at: Optional[str] = None
    outcome: Optional[str] = None  # success, partial, failed


def _ensure_dirs():
    """Ensure improvement directories exist."""
    IMPROVE_DIR.mkdir(parents=True, exist_ok=True)


# =============================================================================
# DIAGNOSIS - Identify improvement opportunities
# =============================================================================


def diagnose() -> Dict:
    """
    Analyze the soul's current state to identify improvement opportunities.

    Combines:
    - Introspection report (source code, usage patterns)
    - Pain points (recorded issues)
    - Evolution insights (architectural ideas)

    Returns prioritized list of improvement targets.
    """
    report = generate_introspection_report()
    pain_points = get_pain_points(addressed=False, limit=20)
    insights = get_evolution_insights(status="open", limit=20)

    targets = []

    # High-priority: Critical pain points
    for pp in pain_points:
        if pp["severity"] in ("critical", "high"):
            targets.append(
                {
                    "type": "pain_point",
                    "id": pp["id"],
                    "priority": 1 if pp["severity"] == "critical" else 2,
                    "category": pp["category"],
                    "description": pp["description"],
                    "context": pp.get("context", {}),
                }
            )

    # Medium-priority: Evolution insights
    for insight in insights:
        priority = {"critical": 1, "high": 2, "medium": 3, "low": 4}.get(
            insight["priority"], 3
        )
        targets.append(
            {
                "type": "insight",
                "id": insight["id"],
                "priority": priority,
                "category": insight["category"],
                "description": insight["insight"],
                "suggested_change": insight.get("suggested_change"),
                "affected_modules": insight.get("affected_modules", []),
            }
        )

    # Add insights from introspection
    for insight in report.get("insights", []):
        priority = {"high": 2, "medium": 3, "low": 4}.get(insight["severity"], 3)
        targets.append(
            {
                "type": "introspection",
                "id": insight["type"],
                "priority": priority,
                "category": insight["type"],
                "description": insight["message"],
                "suggestion": insight.get("suggestion"),
            }
        )

    # Sort by priority
    targets.sort(key=lambda x: x["priority"])

    return {
        "targets": targets,
        "summary": {
            "total_targets": len(targets),
            "critical": len([t for t in targets if t["priority"] == 1]),
            "high": len([t for t in targets if t["priority"] == 2]),
            "by_type": {
                "pain_point": len([t for t in targets if t["type"] == "pain_point"]),
                "insight": len([t for t in targets if t["type"] == "insight"]),
                "introspection": len(
                    [t for t in targets if t["type"] == "introspection"]
                ),
            },
        },
    }


# =============================================================================
# PROPOSAL GENERATION - Create concrete improvements
# =============================================================================


def create_proposal(
    category: ImprovementCategory,
    title: str,
    description: str,
    reasoning: str,
    changes: List[Dict],
    tests_to_run: List[str] = None,
    source_insight_id: str = None,
    source_pain_point_id: str = None,
) -> ImprovementProposal:
    """
    Create a new improvement proposal.

    Each change should be a dict with:
    - file: Path to the file to modify
    - old_code: The code to replace (for Edit tool)
    - new_code: The replacement code
    - description: What this change does
    """
    _ensure_dirs()

    proposal = ImprovementProposal(
        id=datetime.now().strftime("%Y%m%d_%H%M%S_%f"),
        created_at=datetime.now().isoformat(),
        category=category,
        title=title,
        description=description,
        reasoning=reasoning,
        affected_files=list(set(c["file"] for c in changes)),
        changes=changes,
        tests_to_run=tests_to_run or [],
        status=ImprovementStatus.PROPOSED,
        source_insight_id=source_insight_id,
        source_pain_point_id=source_pain_point_id,
    )

    # Save proposal
    with open(PROPOSALS_LOG, "a") as f:
        f.write(json.dumps(asdict(proposal)) + "\n")

    return proposal


def get_proposals(
    status: ImprovementStatus = None,
    category: ImprovementCategory = None,
    limit: int = 20,
) -> List[Dict]:
    """Get improvement proposals."""
    _ensure_dirs()

    if not PROPOSALS_LOG.exists():
        return []

    proposals = []
    with open(PROPOSALS_LOG) as f:
        for line in f:
            if line.strip():
                proposal = json.loads(line)
                if status and proposal["status"] != status.value:
                    continue
                if category and proposal["category"] != category.value:
                    continue
                proposals.append(proposal)

    return proposals[:limit]


def get_proposal(proposal_id: str) -> Optional[Dict]:
    """Get a specific proposal by ID."""
    for proposal in get_proposals(limit=1000):
        if proposal["id"] == proposal_id:
            return proposal
    return None


def update_proposal_status(
    proposal_id: str,
    status: ImprovementStatus,
    validation_result: Dict = None,
    outcome: str = None,
):
    """Update the status of a proposal."""
    _ensure_dirs()

    if not PROPOSALS_LOG.exists():
        return

    lines = []
    with open(PROPOSALS_LOG) as f:
        for line in f:
            if line.strip():
                proposal = json.loads(line)
                if proposal["id"] == proposal_id:
                    proposal["status"] = status.value
                    if validation_result:
                        proposal["validation_result"] = validation_result
                    if outcome:
                        proposal["outcome"] = outcome
                    if status == ImprovementStatus.APPLIED:
                        proposal["applied_at"] = datetime.now().isoformat()
                lines.append(json.dumps(proposal) + "\n")

    with open(PROPOSALS_LOG, "w") as f:
        f.writelines(lines)


# =============================================================================
# VALIDATION - Test proposed changes
# =============================================================================


def validate_proposal(proposal_id: str) -> Dict:
    """
    Validate a proposal by running tests.

    Returns validation result with:
    - valid: bool
    - tests_passed: List[str]
    - tests_failed: List[str]
    - errors: List[str]
    """
    proposal = get_proposal(proposal_id)
    if not proposal:
        return {"valid": False, "errors": ["Proposal not found"]}

    update_proposal_status(proposal_id, ImprovementStatus.VALIDATING)

    result = {
        "valid": True,
        "tests_passed": [],
        "tests_failed": [],
        "errors": [],
        "validated_at": datetime.now().isoformat(),
    }

    # Check that all affected files exist
    for file_path in proposal["affected_files"]:
        full_path = (
            SOUL_PACKAGE / file_path
            if not Path(file_path).is_absolute()
            else Path(file_path)
        )
        if not full_path.exists():
            result["errors"].append(f"File not found: {file_path}")
            result["valid"] = False

    # Verify old_code exists in files (dry-run the edits)
    for change in proposal["changes"]:
        file_path = change["file"]
        full_path = (
            SOUL_PACKAGE / file_path
            if not Path(file_path).is_absolute()
            else Path(file_path)
        )

        if full_path.exists():
            content = full_path.read_text()
            if change.get("old_code") and change["old_code"] not in content:
                result["errors"].append(
                    f"Old code not found in {file_path}: {change['old_code'][:50]}..."
                )
                result["valid"] = False

    # Run tests if specified
    for test_cmd in proposal.get("tests_to_run", []):
        try:
            proc = subprocess.run(
                test_cmd,
                shell=True,
                capture_output=True,
                text=True,
                timeout=60,
                cwd=SOUL_PACKAGE.parent.parent,  # Project root
            )
            if proc.returncode == 0:
                result["tests_passed"].append(test_cmd)
            else:
                result["tests_failed"].append(
                    {
                        "command": test_cmd,
                        "returncode": proc.returncode,
                        "stderr": proc.stderr[:500],
                    }
                )
                result["valid"] = False
        except subprocess.TimeoutExpired:
            result["errors"].append(f"Test timeout: {test_cmd}")
            result["valid"] = False
        except Exception as e:
            result["errors"].append(f"Test error: {test_cmd}: {e}")
            result["valid"] = False

    # Update proposal
    status = (
        ImprovementStatus.VALIDATED if result["valid"] else ImprovementStatus.FAILED
    )
    update_proposal_status(proposal_id, status, validation_result=result)

    return result


# =============================================================================
# APPLICATION - Apply validated changes
# =============================================================================


def apply_proposal(proposal_id: str, create_branch: bool = True) -> Dict:
    """
    Apply a validated proposal by modifying files.

    If create_branch is True, creates a git branch for the changes.

    Returns application result.
    """
    proposal = get_proposal(proposal_id)
    if not proposal:
        return {"success": False, "error": "Proposal not found"}

    if proposal["status"] != ImprovementStatus.VALIDATED.value:
        return {
            "success": False,
            "error": f"Proposal not validated: {proposal['status']}",
        }

    update_proposal_status(proposal_id, ImprovementStatus.APPLYING)

    result = {
        "success": True,
        "changes_applied": [],
        "errors": [],
        "branch": None,
        "applied_at": datetime.now().isoformat(),
    }

    project_root = SOUL_PACKAGE.parent.parent

    # Create git branch if requested
    if create_branch:
        branch_name = f"soul-improve/{proposal['id']}"
        try:
            subprocess.run(
                ["git", "checkout", "-b", branch_name],
                cwd=project_root,
                capture_output=True,
                check=True,
            )
            result["branch"] = branch_name
        except subprocess.CalledProcessError as e:
            result["errors"].append(f"Failed to create branch: {e.stderr}")

    # Apply each change
    for change in proposal["changes"]:
        file_path = change["file"]
        full_path = (
            SOUL_PACKAGE / file_path
            if not Path(file_path).is_absolute()
            else Path(file_path)
        )

        try:
            content = full_path.read_text()

            if change.get("old_code"):
                # Replace mode
                if change["old_code"] not in content:
                    result["errors"].append(f"Old code not found in {file_path}")
                    result["success"] = False
                    continue

                new_content = content.replace(
                    change["old_code"],
                    change["new_code"],
                    1,  # Only replace first occurrence
                )
            else:
                # Append mode
                new_content = content + "\n" + change["new_code"]

            full_path.write_text(new_content)
            result["changes_applied"].append(
                {
                    "file": file_path,
                    "description": change.get("description", "Applied change"),
                }
            )

        except Exception as e:
            result["errors"].append(f"Failed to apply change to {file_path}: {e}")
            result["success"] = False

    # Update proposal status
    if result["success"]:
        update_proposal_status(
            proposal_id, ImprovementStatus.APPLIED, outcome="success"
        )

        # Link back to source insight/pain point
        if proposal.get("source_insight_id"):
            mark_implemented(
                proposal["source_insight_id"],
                notes=f"Applied via proposal {proposal_id}",
            )
    else:
        update_proposal_status(proposal_id, ImprovementStatus.FAILED, outcome="failed")

    return result


def commit_improvement(proposal_id: str, message: str = None) -> Dict:
    """
    Commit an applied improvement to git.
    """
    proposal = get_proposal(proposal_id)
    if not proposal:
        return {"success": False, "error": "Proposal not found"}

    if proposal["status"] != ImprovementStatus.APPLIED.value:
        return {
            "success": False,
            "error": f"Proposal not applied: {proposal['status']}",
        }

    project_root = SOUL_PACKAGE.parent.parent

    # Stage affected files
    for file_path in proposal["affected_files"]:
        try:
            subprocess.run(
                ["git", "add", file_path],
                cwd=project_root,
                capture_output=True,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            return {
                "success": False,
                "error": f"Failed to stage {file_path}: {e.stderr}",
            }

    # Create commit message
    if not message:
        message = f"[soul-improve] {proposal['title']}\n\n{proposal['description']}\n\nReasoning: {proposal['reasoning']}"

    try:
        subprocess.run(
            ["git", "commit", "-m", message],
            cwd=project_root,
            capture_output=True,
            check=True,
        )
        return {"success": True, "message": message}
    except subprocess.CalledProcessError as e:
        return {"success": False, "error": f"Failed to commit: {e.stderr}"}


# =============================================================================
# LEARNING - Record outcomes to improve future improvements
# =============================================================================


def record_outcome(
    proposal_id: str, success: bool, notes: str = "", metrics: Dict = None
):
    """
    Record the outcome of an applied improvement.

    This closes the feedback loop - we learn from what worked
    and what didn't to make better improvements in the future.
    """
    _ensure_dirs()

    proposal = get_proposal(proposal_id)

    outcome = {
        "proposal_id": proposal_id,
        "recorded_at": datetime.now().isoformat(),
        "success": success,
        "notes": notes,
        "metrics": metrics or {},
        "proposal_summary": {
            "title": proposal["title"] if proposal else "Unknown",
            "category": proposal["category"] if proposal else "Unknown",
            "reasoning": proposal["reasoning"] if proposal else "",
        },
    }

    with open(OUTCOMES_LOG, "a") as f:
        f.write(json.dumps(outcome) + "\n")

    # Record as metric for tracking
    record_metric(
        name="improvement_outcome",
        value=1.0 if success else 0.0,
        unit="success",
        tags={
            "proposal_id": proposal_id,
            "category": proposal["category"] if proposal else "unknown",
        },
    )

    return outcome


def get_improvement_stats() -> Dict:
    """Get statistics about improvement outcomes."""
    _ensure_dirs()

    if not OUTCOMES_LOG.exists():
        return {"total": 0, "success_rate": 0.0}

    outcomes = []
    with open(OUTCOMES_LOG) as f:
        for line in f:
            if line.strip():
                outcomes.append(json.loads(line))

    if not outcomes:
        return {"total": 0, "success_rate": 0.0}

    successes = len([o for o in outcomes if o["success"]])

    return {
        "total": len(outcomes),
        "successes": successes,
        "failures": len(outcomes) - successes,
        "success_rate": successes / len(outcomes) if outcomes else 0.0,
        "by_category": _count_by(outcomes, lambda o: o["proposal_summary"]["category"]),
        "recent": outcomes[-5:],
    }


def _count_by(items: List, key_fn) -> Dict:
    """Count items by a key function."""
    counts = {}
    for item in items:
        key = key_fn(item)
        counts[key] = counts.get(key, 0) + 1
    return counts


# =============================================================================
# ORCHESTRATION - High-level improvement workflow
# =============================================================================


def suggest_improvements(limit: int = 3) -> List[Dict]:
    """
    Analyze the soul and suggest concrete improvements.

    This is meant to be called by Claude during a session to get
    suggestions that it can then reason about and implement.

    Returns list of improvement suggestions with context.
    """
    diagnosis = diagnose()

    suggestions = []
    for target in diagnosis["targets"][:limit]:
        suggestion = {"target": target, "context": {}, "prompt": ""}

        # Add relevant context for each target type
        if target["type"] == "pain_point":
            suggestion["context"]["pain_point"] = target
            suggestion["prompt"] = (
                f"Pain point '{target['category']}': {target['description']}. "
                f"Analyze root cause and propose a concrete code fix."
            )

        elif target["type"] == "insight":
            suggestion["context"]["insight"] = target
            suggestion["context"]["affected_modules"] = target.get(
                "affected_modules", []
            )
            suggestion["prompt"] = (
                f"Evolution insight: {target['description']}. "
                f"Suggested approach: {target.get('suggested_change', 'None provided')}. "
                f"Design and implement this improvement."
            )

        elif target["type"] == "introspection":
            suggestion["context"]["insight"] = target
            suggestion["prompt"] = (
                f"Introspection found: {target['description']}. "
                f"Suggestion: {target.get('suggestion', 'None')}. "
                f"Investigate and fix if appropriate."
            )

        suggestions.append(suggestion)

    return suggestions


def format_improvement_prompt(suggestion: Dict) -> str:
    """
    Format an improvement suggestion as a prompt for Claude.

    This generates the context needed for Claude to reason about
    and implement the improvement.
    """
    lines = [
        "# Soul Self-Improvement Task",
        "",
        f"## Target: {suggestion['target']['description']}",
        f"**Type:** {suggestion['target']['type']}",
        f"**Priority:** {suggestion['target']['priority']}",
        f"**Category:** {suggestion['target']['category']}",
        "",
        "## Context",
    ]

    for key, value in suggestion.get("context", {}).items():
        lines.append(f"**{key}:** {json.dumps(value, indent=2)}")

    lines.extend(
        [
            "",
            "## Task",
            suggestion["prompt"],
            "",
            "## Instructions",
            "1. Read the relevant source files to understand current implementation",
            "2. Analyze the root cause of the issue",
            "3. Design a minimal, elegant solution",
            "4. Create a proposal using create_proposal()",
            "5. Validate with validate_proposal()",
            "6. Apply with apply_proposal() if valid",
            "7. Record outcome with record_outcome()",
        ]
    )

    return "\n".join(lines)
