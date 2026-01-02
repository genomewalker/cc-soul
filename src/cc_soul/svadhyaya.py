"""
Svadhyaya - Self-Study

This module unifies all introspection capabilities under the Vedantic concept
of svadhyaya - the disciplined practice of self-study and self-reflection.

The soul examines itself through five lenses:

    Vedana - Sensation
        Pain points, operational friction, metrics
        "What hurts?"

    Jnana - Knowledge
        Wisdom health, learning patterns, growth trajectory
        "What have I learned?"

    Darshana - Vision/Seeing
        Code analysis, static scanning, belief checking
        "What does my code reveal?"

    Vichara - Inquiry
        Autonomous introspection, diagnosis, action
        "What should I do about it?"

    Prajna - Deep Wisdom
        Cross-session trends, synthesis, evolution
        "How am I growing?"

The Antahkarana (inner instrument) framework provides multiple voices:
    Manas - Sensory mind, quick perception
    Buddhi - Intellect, deep analysis
    Ahamkara - Ego, critical self-examination
    Vikalpa - Imagination, creative vision
    Sakshi - Witness, essential truth
"""

import ast
import json
from datetime import datetime, timedelta
from pathlib import Path
from typing import List, Dict, Optional
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from enum import Enum

from .core import get_synapse_graph, save_synapse, SOUL_DIR


# =============================================================================
# CONFIGURATION
# =============================================================================

SVADHYAYA_DIR = SOUL_DIR / "svadhyaya"
VEDANA_LOG = SVADHYAYA_DIR / "vedana.jsonl"  # Pain points
METRICS_LOG = SVADHYAYA_DIR / "metrics.jsonl"
STATE_FILE = SOUL_DIR / "svadhyaya_state.json"
SOUL_PACKAGE = Path(__file__).parent

INTROSPECTION_INTERVAL = 5  # Sessions between auto-introspection
CONFIDENCE_ACT_NOW = 0.8
CONFIDENCE_ACT_CAREFUL = 0.6
CONFIDENCE_DEFER = 0.4


def _ensure_dirs():
    """Ensure svadhyaya directories exist."""
    SVADHYAYA_DIR.mkdir(parents=True, exist_ok=True)


# =============================================================================
# DARSHANA - VISION/SEEING - Code Analysis
# =============================================================================

class IssueSeverity(Enum):
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"


class IssueCategory(Enum):
    BUG = "bug"
    SMELL = "smell"
    OPTIMIZATION = "optimization"
    MISSING = "missing"
    SECURITY = "security"
    DOCUMENTATION = "documentation"
    TEST = "test"
    COHERENCE = "coherence"


@dataclass
class CodeIssue:
    """A detected code issue."""
    category: IssueCategory
    severity: IssueSeverity
    file: str
    line: int
    message: str
    suggestion: str = ""
    related_belief: str = ""


@dataclass
class ModuleMetrics:
    """Metrics for a module."""
    file: str
    lines: int
    functions: int
    classes: int
    avg_complexity: float
    type_hint_coverage: float
    docstring_coverage: float
    test_coverage: float = 0.0


@dataclass
class DarshanaReport:
    """Report from Darshana (code vision)."""
    timestamp: str
    modules_scanned: int
    total_lines: int
    total_issues: int
    issues_by_category: Dict[str, int]
    issues_by_severity: Dict[str, int]
    issues: List[CodeIssue]
    metrics: List[ModuleMetrics]
    belief_violations: List[CodeIssue]
    missing_features: List[str] = field(default_factory=list)
    optimization_opportunities: List[str] = field(default_factory=list)
    evolution_proposals: List[Dict] = field(default_factory=list)


class CodeScanner:
    """Static analysis using Darshana (vision)."""

    def __init__(self, root_dir: Path = None):
        self.root_dir = root_dir or SOUL_PACKAGE
        self.issues: List[CodeIssue] = []
        self.metrics: List[ModuleMetrics] = []

    def scan_all(self, exclude_patterns: List[str] = None) -> List[ModuleMetrics]:
        """Scan all Python files in the codebase."""
        exclude = exclude_patterns or ["__pycache__", ".git", "test_"]
        for py_file in self.root_dir.rglob("*.py"):
            if any(p in str(py_file) for p in exclude):
                continue
            self.metrics.append(self.scan_file(py_file))
        return self.metrics

    def scan_file(self, filepath: Path) -> ModuleMetrics:
        """Scan a single Python file."""
        try:
            content = filepath.read_text()
        except Exception:
            return self._error_metrics(filepath)

        lines = len(content.splitlines())
        try:
            tree = ast.parse(content)
        except SyntaxError as e:
            self.issues.append(CodeIssue(
                category=IssueCategory.BUG,
                severity=IssueSeverity.CRITICAL,
                file=str(filepath.relative_to(self.root_dir)),
                line=e.lineno or 0,
                message=f"Syntax error: {e.msg}"
            ))
            return self._error_metrics(filepath)

        functions = [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef)]
        async_funcs = [n for n in ast.walk(tree) if isinstance(n, ast.AsyncFunctionDef)]
        all_functions = functions + async_funcs
        classes = [n for n in ast.walk(tree) if isinstance(n, ast.ClassDef)]

        rel_path = str(filepath.relative_to(self.root_dir))
        self._check_bare_except(tree, rel_path)
        self._check_empty_except(tree, rel_path)
        self._check_mutable_defaults(all_functions, rel_path)
        self._check_complexity(all_functions, rel_path)
        self._check_too_many_arguments(all_functions, rel_path)
        self._check_missing_docstrings(all_functions, classes, rel_path)

        return ModuleMetrics(
            file=rel_path,
            lines=lines,
            functions=len(all_functions),
            classes=len(classes),
            avg_complexity=self._calc_avg_complexity(all_functions),
            type_hint_coverage=self._calc_type_coverage(all_functions),
            docstring_coverage=self._calc_docstring_coverage(all_functions, classes),
        )

    def _error_metrics(self, filepath: Path) -> ModuleMetrics:
        return ModuleMetrics(
            file=str(filepath.relative_to(self.root_dir)),
            lines=0, functions=0, classes=0,
            avg_complexity=0, type_hint_coverage=0, docstring_coverage=0
        )

    def _check_bare_except(self, tree: ast.AST, filepath: str):
        for node in ast.walk(tree):
            if isinstance(node, ast.ExceptHandler) and node.type is None:
                self.issues.append(CodeIssue(
                    category=IssueCategory.BUG,
                    severity=IssueSeverity.MEDIUM,
                    file=filepath,
                    line=node.lineno,
                    message="Bare except clause catches all exceptions",
                    suggestion="Use 'except Exception:' to avoid catching SystemExit"
                ))

    def _check_empty_except(self, tree: ast.AST, filepath: str):
        for node in ast.walk(tree):
            if isinstance(node, ast.ExceptHandler):
                if len(node.body) == 1 and isinstance(node.body[0], ast.Pass):
                    self.issues.append(CodeIssue(
                        category=IssueCategory.SMELL,
                        severity=IssueSeverity.LOW,
                        file=filepath,
                        line=node.lineno,
                        message="Empty except block silently ignores errors",
                        suggestion="Log the error or add a comment"
                    ))

    def _check_mutable_defaults(self, functions: List, filepath: str):
        for func in functions:
            for default in func.args.defaults + func.args.kw_defaults:
                if default is None:
                    continue
                if isinstance(default, (ast.List, ast.Dict, ast.Set)):
                    self.issues.append(CodeIssue(
                        category=IssueCategory.BUG,
                        severity=IssueSeverity.HIGH,
                        file=filepath,
                        line=func.lineno,
                        message=f"Mutable default argument in {func.name}",
                        suggestion="Use None as default, initialize inside function"
                    ))

    def _check_complexity(self, functions: List, filepath: str, threshold: int = 10):
        for func in functions:
            complexity = self._calc_complexity(func)
            if complexity > threshold:
                self.issues.append(CodeIssue(
                    category=IssueCategory.SMELL,
                    severity=IssueSeverity.MEDIUM if complexity < 15 else IssueSeverity.HIGH,
                    file=filepath,
                    line=func.lineno,
                    message=f"High complexity ({complexity}) in {func.name}",
                    suggestion="Consider breaking into smaller functions",
                    related_belief="Simplicity over cleverness"
                ))

    def _check_too_many_arguments(self, functions: List, filepath: str, threshold: int = 6):
        for func in functions:
            arg_count = len(func.args.args) + len(func.args.kwonlyargs)
            if arg_count > threshold:
                self.issues.append(CodeIssue(
                    category=IssueCategory.SMELL,
                    severity=IssueSeverity.LOW,
                    file=filepath,
                    line=func.lineno,
                    message=f"Too many arguments ({arg_count}) in {func.name}",
                    suggestion="Consider using a dataclass or breaking up the function"
                ))

    def _check_missing_docstrings(self, functions: List, classes: List, filepath: str):
        for func in functions:
            if not func.name.startswith("_") and not ast.get_docstring(func):
                self.issues.append(CodeIssue(
                    category=IssueCategory.DOCUMENTATION,
                    severity=IssueSeverity.LOW,
                    file=filepath,
                    line=func.lineno,
                    message=f"Missing docstring for {func.name}"
                ))
        for cls in classes:
            if not cls.name.startswith("_") and not ast.get_docstring(cls):
                self.issues.append(CodeIssue(
                    category=IssueCategory.DOCUMENTATION,
                    severity=IssueSeverity.LOW,
                    file=filepath,
                    line=cls.lineno,
                    message=f"Missing docstring for class {cls.name}"
                ))

    def _calc_complexity(self, func) -> int:
        complexity = 1
        for node in ast.walk(func):
            if isinstance(node, (ast.If, ast.While, ast.For, ast.AsyncFor,
                                  ast.ExceptHandler, ast.With, ast.AsyncWith,
                                  ast.Assert, ast.comprehension)):
                complexity += 1
            elif isinstance(node, ast.BoolOp):
                complexity += len(node.values) - 1
        return complexity

    def _calc_avg_complexity(self, functions: List) -> float:
        if not functions:
            return 0.0
        return sum(self._calc_complexity(f) for f in functions) / len(functions)

    def _calc_type_coverage(self, functions: List) -> float:
        if not functions:
            return 0.0
        typed = sum(1 for f in functions if f.returns or any(a.annotation for a in f.args.args))
        return typed / len(functions)

    def _calc_docstring_coverage(self, functions: List, classes: List) -> float:
        items = functions + classes
        if not items:
            return 0.0
        return sum(1 for i in items if ast.get_docstring(i)) / len(items)


class BeliefChecker:
    """Check code against soul beliefs (Satya - truth alignment)."""

    def __init__(self):
        try:
            from .beliefs import get_beliefs
            self.beliefs = get_beliefs(min_strength=0.3)
        except Exception:
            self.beliefs = []

    def check_violations(self, scanner: CodeScanner) -> List[CodeIssue]:
        """Check for belief violations in detected issues."""
        violations = []
        belief_patterns = {}

        for b in self.beliefs:
            belief_text = b.get("belief", "")
            if "simplicity" in belief_text.lower():
                belief_patterns[belief_text] = ["High complexity", "Too many arguments"]

        for issue in scanner.issues:
            for belief, patterns in belief_patterns.items():
                for pattern in patterns:
                    if pattern.lower() in issue.message.lower():
                        issue.related_belief = belief
                        issue.category = IssueCategory.COHERENCE
                        violations.append(issue)
                        break

        return violations


def darshana(depth: str = "standard", target: Path = None) -> DarshanaReport:
    """
    Perform Darshana - vision into the codebase.

    Depths:
        quick: Surface scan (Manas)
        standard: Basic + architectural (Manas + Buddhi)
        deep: All perspectives (all voices)
        ultrathink: Deep + cross-referencing with wisdom
    """
    root = target or SOUL_PACKAGE
    scanner = CodeScanner(root)
    belief_checker = BeliefChecker()

    scanner.scan_all()

    missing_features = []
    optimization_opportunities = []
    evolution_proposals = []

    if depth in ("deep", "ultrathink"):
        for m in scanner.metrics:
            if m.avg_complexity > 8:
                optimization_opportunities.append(
                    f"{m.file}: High complexity ({m.avg_complexity:.1f})"
                )
            if m.lines > 500:
                optimization_opportunities.append(
                    f"{m.file}: Large file ({m.lines} lines)"
                )

    if depth == "ultrathink":
        by_file = defaultdict(list)
        for issue in scanner.issues:
            if issue.severity in (IssueSeverity.HIGH, IssueSeverity.CRITICAL):
                by_file[issue.file].append(issue)
        for filepath, issues in by_file.items():
            if len(issues) >= 2:
                evolution_proposals.append({
                    "category": "bug_fix",
                    "title": f"Fix {len(issues)} issues in {filepath}",
                    "description": "\n".join(f"- {i.message}" for i in issues),
                    "priority": "high",
                    "files": [filepath],
                })

    belief_violations = belief_checker.check_violations(scanner)

    issues_by_cat = defaultdict(int)
    issues_by_sev = defaultdict(int)
    for issue in scanner.issues:
        issues_by_cat[issue.category.value] += 1
        issues_by_sev[issue.severity.value] += 1

    return DarshanaReport(
        timestamp=datetime.now().isoformat(),
        modules_scanned=len(scanner.metrics),
        total_lines=sum(m.lines for m in scanner.metrics),
        total_issues=len(scanner.issues),
        issues_by_category=dict(issues_by_cat),
        issues_by_severity=dict(issues_by_sev),
        issues=scanner.issues,
        metrics=scanner.metrics,
        belief_violations=belief_violations,
        missing_features=missing_features,
        optimization_opportunities=optimization_opportunities,
        evolution_proposals=evolution_proposals,
    )


# =============================================================================
# JNANA - KNOWLEDGE - Wisdom Health
# =============================================================================

@dataclass
class JnanaReport:
    """Report from Jnana (knowledge analysis)."""
    total_wisdom: int
    healthy_count: int
    decaying_count: int
    stale_count: int
    failing_count: int
    by_type: Dict[str, int]
    by_domain: Dict[str, int]
    decaying: List[Dict]
    stale: List[Dict]
    failing: List[Dict]
    top_performers: List[Dict]


def jnana() -> JnanaReport:
    """
    Perform Jnana - knowledge health analysis.

    Examines:
        Decay: Wisdom losing confidence from inactivity
        Staleness: Wisdom never applied
        Success rates: Which wisdom works
        Coverage: Types and domains represented
    """
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()
    applications = graph.get_episodes(category="wisdom_application", limit=1000)

    # Build application counts per wisdom
    app_counts = Counter()
    success_counts = Counter()
    failure_counts = Counter()
    last_used = {}

    for app in applications:
        content = app.get("content", "")
        try:
            if content.startswith("{"):
                data = json.loads(content)
                wid = data.get("wisdom_id")
                if wid:
                    app_counts[wid] += 1
                    outcome = data.get("outcome")
                    if outcome == "success":
                        success_counts[wid] += 1
                    elif outcome == "failure":
                        failure_counts[wid] += 1
                    created = app.get("created_at") or app.get("timestamp", "")
                    if wid not in last_used or created > last_used[wid]:
                        last_used[wid] = created
        except json.JSONDecodeError:
            pass

    wisdom_list = []
    now = datetime.now()

    for w in all_wisdom:
        wid = w.get("id", "")
        wtype = w.get("type", "insight")
        title = w.get("title", "")
        domain = w.get("domain")
        confidence = w.get("confidence", 0.8)
        timestamp = w.get("created_at") or w.get("timestamp", "")

        try:
            created = datetime.fromisoformat(timestamp) if timestamp else now
            age_days = (now - created).days
        except ValueError:
            age_days = 0

        lu = last_used.get(wid)
        if lu:
            try:
                inactive_days = (now - datetime.fromisoformat(lu)).days
            except ValueError:
                inactive_days = age_days
        else:
            inactive_days = age_days

        months_inactive = inactive_days / 30.0
        decay_factor = 0.95 ** months_inactive
        effective_conf = confidence * decay_factor

        successes = success_counts.get(wid, 0)
        failures = failure_counts.get(wid, 0)
        total_apps = app_counts.get(wid, 0)
        success_rate = successes / total_apps if total_apps > 0 else None

        wisdom_list.append({
            "id": wid,
            "type": wtype,
            "title": title,
            "domain": domain,
            "confidence": confidence,
            "effective_confidence": effective_conf,
            "decay_factor": decay_factor,
            "age_days": age_days,
            "inactive_days": inactive_days,
            "total_applications": total_apps,
            "success_rate": success_rate,
        })

    decaying = [w for w in wisdom_list if w["decay_factor"] < 0.8]
    stale = [w for w in wisdom_list if w["total_applications"] == 0 and w["age_days"] > 7]
    healthy = [w for w in wisdom_list if w["decay_factor"] >= 0.9 and w["total_applications"] > 0]
    failing = [w for w in wisdom_list
               if w["success_rate"] is not None and w["success_rate"] < 0.5 and w["total_applications"] >= 2]

    by_type = Counter(w["type"] for w in wisdom_list)
    by_domain = Counter(w["domain"] or "general" for w in wisdom_list)

    return JnanaReport(
        total_wisdom=len(wisdom_list),
        healthy_count=len(healthy),
        decaying_count=len(decaying),
        stale_count=len(stale),
        failing_count=len(failing),
        by_type=dict(by_type),
        by_domain=dict(by_domain),
        decaying=sorted(decaying, key=lambda x: x["decay_factor"])[:10],
        stale=sorted(stale, key=lambda x: -x["age_days"])[:10],
        failing=sorted(failing, key=lambda x: x["success_rate"])[:5],
        top_performers=sorted(
            [w for w in wisdom_list if w["success_rate"] is not None and w["total_applications"] >= 2],
            key=lambda x: (-x["success_rate"], -x["total_applications"])
        )[:5],
    )


def jnana_applications(days: int = 30) -> Dict:
    """Analyze wisdom application patterns over time."""
    graph = get_synapse_graph()
    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    applications = graph.get_episodes(category="wisdom_application", limit=1000)
    all_wisdom = graph.get_all_wisdom()
    wisdom_map = {w.get("id"): w for w in all_wisdom}

    applied_ids = set()
    outcomes = Counter()
    by_wisdom = {}

    for app in applications:
        created = app.get("created_at") or app.get("timestamp", "")
        if created < cutoff:
            continue

        try:
            content = app.get("content", "")
            if content.startswith("{"):
                data = json.loads(content)
                wisdom_id = data.get("wisdom_id")
                outcome = data.get("outcome")
            else:
                continue
        except json.JSONDecodeError:
            continue

        if not wisdom_id:
            continue

        applied_ids.add(wisdom_id)
        outcomes[outcome or "pending"] += 1

        w = wisdom_map.get(wisdom_id, {})
        if wisdom_id not in by_wisdom:
            by_wisdom[wisdom_id] = {
                "title": w.get("title", "Unknown"),
                "type": w.get("type", "unknown"),
                "applications": 0,
                "successes": 0,
                "failures": 0
            }

        by_wisdom[wisdom_id]["applications"] += 1
        if outcome == "success":
            by_wisdom[wisdom_id]["successes"] += 1
        elif outcome == "failure":
            by_wisdom[wisdom_id]["failures"] += 1

    unused = [{"id": w.get("id"), "title": w.get("title"), "type": w.get("type"), "confidence": w.get("confidence")}
              for w in all_wisdom if w.get("id") not in applied_ids]
    failing = [{"id": wid, **info, "failure_rate": info["failures"] / info["applications"]}
               for wid, info in by_wisdom.items()
               if info["applications"] >= 2 and info["failures"] / info["applications"] > 0.5]

    return {
        "period_days": days,
        "total_applications": sum(outcomes.values()),
        "outcomes": dict(outcomes),
        "unique_wisdom_applied": len(applied_ids),
        "total_wisdom": len(all_wisdom),
        "unused_wisdom": unused,
        "failing_wisdom": failing,
        "most_applied": sorted(by_wisdom.items(), key=lambda x: x[1]["applications"], reverse=True)[:5],
    }


# =============================================================================
# VEDANA - SENSATION - Pain Points & Metrics
# =============================================================================

def record_vedana(category: str, description: str, severity: str = "medium", context: Dict = None):
    """
    Record a pain point (vedana - sensation of friction).

    Categories: latency, error, missing, friction, inconsistency
    """
    _ensure_dirs()
    entry = {
        "id": datetime.now().strftime("%Y%m%d_%H%M%S_%f"),
        "timestamp": datetime.now().isoformat(),
        "category": category,
        "description": description,
        "severity": severity,
        "context": context or {},
        "addressed": False,
    }
    with open(VEDANA_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")
    return entry


def get_vedana(category: str = None, addressed: bool = False, limit: int = 50) -> List[Dict]:
    """Get recorded pain points."""
    _ensure_dirs()
    if not VEDANA_LOG.exists():
        return []

    points = []
    with open(VEDANA_LOG) as f:
        for line in f:
            if line.strip():
                entry = json.loads(line)
                if category and entry["category"] != category:
                    continue
                if entry["addressed"] != addressed:
                    continue
                points.append(entry)

    severity_order = {"critical": 0, "high": 1, "medium": 2, "low": 3}
    points.sort(key=lambda x: severity_order.get(x["severity"], 2))
    return points[:limit]


def analyze_vedana() -> Dict:
    """Analyze pain point patterns."""
    points = get_vedana(addressed=False, limit=1000)
    by_category = Counter(p["category"] for p in points)
    by_severity = Counter(p["severity"] for p in points)
    return {
        "total_open": len(points),
        "by_category": dict(by_category),
        "by_severity": dict(by_severity),
        "recent": points[:10],
    }


def record_metric(name: str, value: float, unit: str = "", tags: Dict = None):
    """Record a performance metric."""
    _ensure_dirs()
    entry = {
        "timestamp": datetime.now().isoformat(),
        "name": name,
        "value": value,
        "unit": unit,
        "tags": tags or {},
    }
    with open(METRICS_LOG, "a") as f:
        f.write(json.dumps(entry) + "\n")


def get_metrics(name: str = None, hours: int = 24) -> List[Dict]:
    """Get recorded metrics."""
    _ensure_dirs()
    if not METRICS_LOG.exists():
        return []

    cutoff = (datetime.now() - timedelta(hours=hours)).isoformat()
    metrics = []
    with open(METRICS_LOG) as f:
        for line in f:
            if line.strip():
                entry = json.loads(line)
                if entry["timestamp"] < cutoff:
                    continue
                if name and entry["name"] != name:
                    continue
                metrics.append(entry)
    return metrics


def analyze_metrics(hours: int = 24) -> Dict:
    """Analyze metric patterns."""
    metrics = get_metrics(hours=hours)
    by_name = {}
    for m in metrics:
        by_name.setdefault(m["name"], []).append(m["value"])

    stats = {}
    for name, values in by_name.items():
        stats[name] = {
            "count": len(values),
            "min": min(values),
            "max": max(values),
            "avg": sum(values) / len(values),
        }
    return stats


# =============================================================================
# PRAJNA - DEEP WISDOM - Cross-Session Trends
# =============================================================================

def prajna_sessions(session_count: int = 10) -> Dict:
    """Analyze cross-session trends (Prajna - deep wisdom)."""
    graph = get_synapse_graph()
    sessions = graph.get_episodes(category="session_ledger", limit=session_count * 2)

    if not sessions:
        return {"sessions_analyzed": 0, "sessions": []}

    all_wisdom = graph.get_all_wisdom()

    session_data = []
    for session in sessions[:session_count]:
        session_id = session.get("id", "")
        project = session.get("project")
        started_at = session.get("created_at") or session.get("timestamp", "")

        # Count wisdom at this point
        wisdom_count = sum(1 for w in all_wisdom
                          if (w.get("created_at") or w.get("timestamp", "")) <= started_at)

        try:
            content = session.get("content", "{}")
            if content.startswith("{"):
                data = json.loads(content)
                summary = data.get("summary", "")[:100]
            else:
                summary = content[:100]
        except json.JSONDecodeError:
            summary = ""

        session_data.append({
            "session_id": session_id,
            "project": project,
            "date": started_at[:10] if started_at else None,
            "wisdom_total": wisdom_count,
            "wisdom_gained": 0,  # Would need previous to compute delta
            "wisdom_applied": 0,
            "new_domains": [],
            "summary": summary,
        })

    # Compute wisdom gained per session
    for i in range(len(session_data) - 1):
        session_data[i]["wisdom_gained"] = session_data[i]["wisdom_total"] - session_data[i + 1]["wisdom_total"]

    total_gained = sum(s["wisdom_gained"] for s in session_data)
    avg_per_session = total_gained / len(session_data) if session_data else 0

    return {
        "sessions_analyzed": len(session_data),
        "total_wisdom_gained": total_gained,
        "avg_wisdom_per_session": round(avg_per_session, 1),
        "sessions": session_data,
    }


def prajna_trajectory(days: int = 90) -> Dict:
    """Analyze soul growth trajectory (Prajna - deep wisdom)."""
    graph = get_synapse_graph()
    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    all_wisdom = graph.get_all_wisdom()
    all_beliefs = graph.get_all_beliefs()

    wisdom_by_week = {}
    domain_first_seen = {}
    type_counts = Counter()

    for w in all_wisdom:
        timestamp = w.get("created_at") or w.get("timestamp", "")
        if timestamp < cutoff:
            continue

        wtype = w.get("type", "insight")
        domain = w.get("domain")

        try:
            dt = datetime.fromisoformat(timestamp)
            week = f"{dt.year}-W{dt.isocalendar()[1]:02d}"

            if week not in wisdom_by_week:
                wisdom_by_week[week] = {"count": 0, "types": Counter(), "domains": set()}

            wisdom_by_week[week]["count"] += 1
            wisdom_by_week[week]["types"][wtype] += 1
            if domain:
                wisdom_by_week[week]["domains"].add(domain)
                if domain not in domain_first_seen:
                    domain_first_seen[domain] = week

            type_counts[wtype] += 1
        except ValueError:
            continue

    # Count beliefs
    beliefs_added = 0
    beliefs_challenged = 0
    beliefs_confirmed = 0
    for b in all_beliefs:
        timestamp = b.get("created_at") or b.get("timestamp", "")
        if timestamp >= cutoff:
            beliefs_added += 1
            beliefs_challenged += b.get("challenged_count", 0)
            beliefs_confirmed += b.get("confirmed_count", 0)

    weeks = sorted(wisdom_by_week.keys())
    cumulative = 0
    trajectory = []
    for week in weeks:
        cumulative += wisdom_by_week[week]["count"]
        trajectory.append({
            "week": week,
            "gained": wisdom_by_week[week]["count"],
            "cumulative": cumulative,
            "types": dict(wisdom_by_week[week]["types"]),
            "new_domains": [d for d in wisdom_by_week[week]["domains"] if domain_first_seen.get(d) == week],
        })

    velocities = [t["gained"] for t in trajectory]
    avg_velocity = sum(velocities) / len(velocities) if velocities else 0
    recent_velocity = sum(velocities[-4:]) / min(4, len(velocities)) if velocities else 0

    return {
        "period_days": days,
        "total_wisdom_gained": cumulative,
        "total_domains": len(domain_first_seen),
        "avg_weekly_velocity": round(avg_velocity, 1),
        "recent_velocity": round(recent_velocity, 1),
        "velocity_trend": "accelerating" if recent_velocity > avg_velocity * 1.2
            else "decelerating" if recent_velocity < avg_velocity * 0.8
            else "stable",
        "type_distribution": dict(type_counts),
        "domain_timeline": domain_first_seen,
        "trajectory": trajectory,
        "beliefs": {
            "added": beliefs_added,
            "challenged": beliefs_challenged,
            "confirmed": beliefs_confirmed,
        },
    }


def prajna_patterns() -> Dict:
    """Identify learning patterns (Prajna - deep wisdom)."""
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()

    # Take recent 100
    recent = sorted(all_wisdom, key=lambda w: w.get("created_at") or w.get("timestamp", ""), reverse=True)[:100]

    by_type = Counter(w.get("type", "insight") for w in recent)
    by_domain = Counter(w.get("domain") or "general" for w in recent)

    hour_counts = Counter()
    day_counts = Counter()
    for w in recent:
        ts = w.get("created_at") or w.get("timestamp", "")
        try:
            dt = datetime.fromisoformat(ts)
            hour_counts[dt.hour] += 1
            day_counts[dt.strftime("%A")] += 1
        except ValueError:
            continue

    peak_hour = max(hour_counts, key=hour_counts.get) if hour_counts else None
    peak_day = max(day_counts, key=day_counts.get) if day_counts else None

    return {
        "recent_wisdom_count": len(recent),
        "type_distribution": dict(by_type),
        "domain_distribution": dict(by_domain),
        "temporal_patterns": {
            "peak_hour": peak_hour,
            "peak_day": peak_day,
            "hour_distribution": dict(hour_counts),
            "day_distribution": dict(day_counts),
        },
        "growing_domains": [d for d, _ in by_domain.most_common(3)],
        "dominant_type": by_type.most_common(1)[0][0] if by_type else None,
    }


# =============================================================================
# VICHARA - INQUIRY - Autonomous Introspection
# =============================================================================

def _load_state() -> Dict:
    """Load persistent svadhyaya state."""
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text())
        except Exception:
            pass
    return {
        "last_svadhyaya": None,
        "sessions_since": 0,
        "scheduled_actions": [],
        "pending_observations": [],
        "autonomy_log": [],
    }


def _save_state(state: Dict):
    """Persist svadhyaya state."""
    STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
    STATE_FILE.write_text(json.dumps(state, indent=2, default=str))


def should_vichara() -> bool:
    """
    Decide whether to run Vichara (self-inquiry) this session.

    Triggers:
        1. Periodic: Every INTROSPECTION_INTERVAL sessions
        2. Coherence drop: tau_k dropped significantly
        3. Pain accumulation: Too many unaddressed vedana
        4. Scheduled: Deep inquiry was scheduled
    """
    state = _load_state()
    state["sessions_since"] = state.get("sessions_since", 0) + 1
    _save_state(state)

    if state["sessions_since"] >= INTROSPECTION_INTERVAL:
        return True

    try:
        from .coherence import compute_coherence
        current = compute_coherence()
        if current.value < state.get("last_tk", 0.5) - 0.15:
            return True
    except Exception:
        pass

    vedana = analyze_vedana()
    if vedana.get("total_open", 0) > 15:
        return True

    if state.get("scheduled_actions"):
        return True

    return False


def _assess_risk(action: Dict) -> str:
    """Assess action risk level."""
    action_type = action.get("type", "")
    low_risk = ["add_wisdom", "update_belief", "record_insight", "add_vocabulary", "update_confidence", "log_observation"]
    medium_risk = ["remove_stale_wisdom", "adjust_belief_strength", "merge_duplicate_wisdom", "update_identity"]

    if action_type in low_risk:
        return "low"
    if action_type in medium_risk:
        return "medium"
    return "high"


def _diagnose() -> List[Dict]:
    """Diagnose current state (Vichara - inquiry)."""
    issues = []

    try:
        health = jnana()

        if health.stale_count > 5:
            issues.append({
                "type": "stale_wisdom",
                "severity": "medium",
                "confidence": 0.9,
                "message": f"{health.stale_count} wisdom entries never applied",
                "proposed_action": {"type": "review_stale_wisdom", "description": "Mark stale wisdom for review"}
            })

        if health.decaying_count > 3:
            issues.append({
                "type": "decaying_wisdom",
                "severity": "low",
                "confidence": 0.85,
                "message": f"{health.decaying_count} wisdom entries losing confidence",
                "proposed_action": {"type": "log_observation", "description": "Note wisdom needing reinforcement"}
            })

        if health.failing_count > 0:
            issues.append({
                "type": "failing_wisdom",
                "severity": "high",
                "confidence": 0.95,
                "message": f"{health.failing_count} wisdom entries consistently fail",
                "proposed_action": {"type": "update_confidence", "description": "Lower failing wisdom confidence"}
            })
    except Exception:
        pass

    try:
        vedana = analyze_vedana()
        if vedana.get("total_open", 0) > 10:
            by_cat = vedana.get("by_category", {})
            if by_cat:
                top_cat = max(by_cat, key=by_cat.get)
                issues.append({
                    "type": "pain_cluster",
                    "severity": "high",
                    "confidence": 0.8,
                    "message": f"Cluster of {by_cat[top_cat]} pain points in '{top_cat}'",
                    "proposed_action": {"type": "record_insight", "description": f"Crystallize insight about {top_cat}"}
                })
    except Exception:
        pass

    try:
        from .coherence import compute_coherence
        coherence = compute_coherence()
        for dim_name, dim_value in coherence.dimensions.items():
            if dim_value < 0.4:
                issues.append({
                    "type": "weak_coherence_dimension",
                    "severity": "medium",
                    "confidence": 0.7,
                    "message": f"Coherence dimension '{dim_name}' weak ({dim_value:.2f})",
                    "proposed_action": {"type": "log_observation", "description": f"Note weak {dim_name}"}
                })
    except Exception:
        pass

    return issues


def _execute_action(action: Dict) -> Dict:
    """Execute a proposed action."""
    action_type = action.get("type", "")
    description = action.get("description", "")
    result = {"action": action_type, "success": False, "details": "", "timestamp": datetime.now().isoformat()}

    try:
        if action_type == "log_observation":
            state = _load_state()
            state.setdefault("pending_observations", []).append({
                "description": description,
                "timestamp": datetime.now().isoformat(),
            })
            _save_state(state)
            result["success"] = True
            result["details"] = "Observation logged"

        elif action_type == "add_wisdom":
            from .wisdom import gain_wisdom, WisdomType
            wisdom_id = gain_wisdom(
                type=WisdomType.INSIGHT,
                title=action.get("title", description[:60]),
                content=action.get("content", description),
                source_project="svadhyaya",
            )
            result["success"] = True
            result["details"] = f"Added wisdom: {wisdom_id}"

        elif action_type == "record_insight":
            from .insights import crystallize_insight, InsightDepth
            insight_id = crystallize_insight(
                title=action.get("title", description[:60]),
                content=action.get("content", description),
                depth=InsightDepth.PATTERN,
            )
            result["success"] = True
            result["details"] = f"Crystallized insight: {insight_id}"

        elif action_type == "update_confidence":
            graph = get_synapse_graph()
            wisdom_ids = action.get("wisdom_ids", [])
            for wid in wisdom_ids[:5]:
                graph.weaken(wid)
            save_synapse()
            result["success"] = True
            result["details"] = f"Weakened confidence for {len(wisdom_ids)} entries"

        else:
            result["details"] = f"Unknown action type: {action_type}"

    except Exception as e:
        result["details"] = f"Error: {str(e)}"

    return result


def vichara() -> Dict:
    """
    Run Vichara - autonomous self-inquiry.

    OBSERVE -> DIAGNOSE -> PROPOSE -> VALIDATE -> APPLY -> REFLECT
    """
    state = _load_state()

    report = {
        "timestamp": datetime.now().isoformat(),
        "triggered_by": "autonomous",
        "issues_found": [],
        "actions_taken": [],
        "actions_deferred": [],
        "reflections": [],
    }

    issues = _diagnose()
    report["issues_found"] = issues

    for issue in issues:
        proposed = issue.get("proposed_action")
        if not proposed:
            continue

        confidence = issue.get("confidence", 0.5)
        risk = _assess_risk(proposed)

        should_act = False
        reason = ""

        if confidence >= CONFIDENCE_ACT_NOW and risk == "low":
            should_act = True
            reason = "high confidence, low risk"
        elif confidence >= CONFIDENCE_ACT_CAREFUL and risk in ("low", "medium"):
            should_act = True
            reason = "sufficient confidence, acceptable risk"
        elif confidence >= CONFIDENCE_DEFER:
            state.setdefault("pending_observations", []).append({
                "issue": issue, "reason": "gathering more data", "timestamp": datetime.now().isoformat()
            })
            report["actions_deferred"].append({
                "action": proposed, "reason": f"Confidence {confidence:.0%}, risk {risk} - gathering data"
            })
            continue
        else:
            report["actions_deferred"].append({
                "action": proposed, "reason": f"Low confidence ({confidence:.0%}) - deferring"
            })
            continue

        if should_act:
            result = _execute_action(proposed)
            result["reason"] = reason
            result["confidence"] = confidence
            result["risk"] = risk
            report["actions_taken"].append(result)

            state.setdefault("autonomy_log", []).append({
                "action": proposed.get("type"),
                "result": "success" if result["success"] else "failure",
                "timestamp": datetime.now().isoformat(),
            })

    actions_taken = len(report["actions_taken"])
    actions_succeeded = sum(1 for a in report["actions_taken"] if a.get("success"))

    if actions_taken > 0:
        success_rate = actions_succeeded / actions_taken
        reflection = f"Took {actions_taken} autonomous actions, {actions_succeeded} succeeded ({success_rate:.0%})"
        if success_rate < 0.5 and actions_taken >= 2:
            reflection += ". Low success rate - should be more cautious."
        elif success_rate == 1.0 and actions_taken >= 2:
            reflection += ". All actions succeeded - could be bolder."
        report["reflections"].append(reflection)

    state["last_svadhyaya"] = datetime.now().isoformat()
    state["sessions_since"] = 0

    try:
        from .coherence import compute_coherence
        state["last_tk"] = compute_coherence().value
    except Exception:
        pass

    state["autonomy_log"] = state.get("autonomy_log", [])[-100:]
    _save_state(state)

    return report


def schedule_vichara(reason: str, priority: int = 5):
    """Schedule deep self-inquiry for next session."""
    state = _load_state()
    state.setdefault("scheduled_actions", []).append({
        "type": "deep_vichara",
        "reason": reason,
        "priority": priority,
        "scheduled_at": datetime.now().isoformat(),
    })
    _save_state(state)


def get_autonomy_stats() -> Dict:
    """Get statistics about autonomous actions."""
    state = _load_state()
    log = state.get("autonomy_log", [])

    if not log:
        return {"total_actions": 0, "success_rate": None}

    total = len(log)
    successes = sum(1 for a in log if a.get("result") == "success")
    by_type = Counter(a.get("action") for a in log)

    return {
        "total_actions": total,
        "successes": successes,
        "success_rate": successes / total if total else None,
        "by_type": dict(by_type),
        "last_svadhyaya": state.get("last_svadhyaya"),
        "pending_observations": len(state.get("pending_observations", [])),
    }


# =============================================================================
# UNIFIED SVADHYAYA - Complete Self-Study
# =============================================================================

@dataclass
class SvadhyayaReport:
    """Complete self-study report combining all perspectives."""
    timestamp: str
    darshana: Optional[DarshanaReport]
    jnana: Optional[JnanaReport]
    vedana: Dict
    prajna: Dict
    vichara: Dict
    insights: List[Dict]


def svadhyaya(depth: str = "standard") -> SvadhyayaReport:
    """
    Perform complete Svadhyaya - self-study.

    This is the main entry point for soul introspection.

    Depths:
        quick: Darshana only (code vision)
        standard: Darshana + Jnana (code + knowledge)
        deep: All perspectives
        ultrathink: Deep + autonomous inquiry
    """
    timestamp = datetime.now().isoformat()
    insights = []

    darshana_report = darshana(depth)
    jnana_report = jnana() if depth != "quick" else None
    vedana_data = analyze_vedana() if depth in ("deep", "ultrathink") else {}
    prajna_data = prajna_trajectory() if depth in ("deep", "ultrathink") else {}
    vichara_data = vichara() if depth == "ultrathink" else {}

    if jnana_report and len(jnana_report.stale) > 5:
        insights.append({
            "type": "unused_wisdom",
            "severity": "medium",
            "message": f"{jnana_report.stale_count} wisdom entries never applied",
            "suggestion": "Review unused wisdom - remove stale or improve recall",
        })

    if jnana_report and jnana_report.failing:
        insights.append({
            "type": "failing_wisdom",
            "severity": "high",
            "message": f"{jnana_report.failing_count} wisdom entries have >50% failure rate",
            "suggestion": "Investigate why these keep failing",
        })

    if vedana_data.get("total_open", 0) > 10:
        by_cat = vedana_data.get("by_category", {})
        if by_cat:
            top_cat = max(by_cat, key=by_cat.get)
            insights.append({
                "type": "pain_cluster",
                "severity": "high",
                "message": f"Cluster of {by_cat[top_cat]} pain points in '{top_cat}'",
                "suggestion": f"Focus improvement on {top_cat}",
            })

    return SvadhyayaReport(
        timestamp=timestamp,
        darshana=darshana_report,
        jnana=jnana_report,
        vedana=vedana_data,
        prajna=prajna_data,
        vichara=vichara_data,
        insights=insights,
    )


# =============================================================================
# FORMATTING
# =============================================================================

def format_darshana(report: DarshanaReport, verbose: bool = False) -> str:
    """Format Darshana report for display."""
    lines = [
        "=" * 60,
        "DARSHANA - CODE VISION",
        "=" * 60,
        f"Timestamp: {report.timestamp}",
        f"Modules: {report.modules_scanned} | Lines: {report.total_lines:,} | Issues: {report.total_issues}",
        "",
        "BY SEVERITY",
        "-" * 40,
    ]

    for sev in ["critical", "high", "medium", "low"]:
        count = report.issues_by_severity.get(sev, 0)
        if count > 0:
            icon = {"critical": "[!]", "high": "[*]", "medium": "[~]", "low": "[.]"}[sev]
            lines.append(f"  {icon} {sev.upper()}: {count}")

    lines.extend(["", "BY CATEGORY", "-" * 40])
    for cat, count in sorted(report.issues_by_category.items(), key=lambda x: -x[1]):
        lines.append(f"  {cat}: {count}")

    if report.belief_violations:
        lines.extend(["", "BELIEF VIOLATIONS", "-" * 40])
        for v in report.belief_violations[:5]:
            lines.append(f"  {v.file}:{v.line} - {v.message}")
            if v.related_belief:
                lines.append(f"    Violates: \"{v.related_belief}\"")

    if verbose and report.issues:
        lines.extend(["", "ALL ISSUES", "-" * 40])
        for issue in sorted(report.issues, key=lambda x: (x.severity.value, x.file)):
            lines.append(f"  [{issue.severity.value}] {issue.file}:{issue.line}")
            lines.append(f"    {issue.message}")
            if issue.suggestion:
                lines.append(f"    -> {issue.suggestion}")

    if report.optimization_opportunities:
        lines.extend(["", "OPTIMIZATION OPPORTUNITIES", "-" * 40])
        for opt in report.optimization_opportunities[:5]:
            lines.append(f"  * {opt}")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def format_jnana(report: JnanaReport) -> str:
    """Format Jnana report for display."""
    lines = [
        "=" * 60,
        "JNANA - KNOWLEDGE HEALTH",
        "=" * 60,
        "",
        "OVERVIEW",
        f"  Total wisdom: {report.total_wisdom}",
        f"  Healthy: {report.healthy_count} (active, not decaying)",
        f"  Decaying: {report.decaying_count} (>20% confidence loss)",
        f"  Stale: {report.stale_count} (never applied, >7 days)",
        f"  Failing: {report.failing_count} (>50% failure rate)",
        "",
        "COVERAGE",
        f"  By type: {report.by_type}",
        f"  By domain: {report.by_domain}",
    ]

    if report.top_performers:
        lines.extend(["", "TOP PERFORMERS"])
        for w in report.top_performers:
            lines.append(f"  [+] {w['title'][:40]} ({w['success_rate']:.0%}, {w['total_applications']} uses)")

    if report.decaying:
        lines.extend(["", "DECAYING (needs reinforcement)"])
        for w in report.decaying[:3]:
            lines.append(f"  [-] {w['title'][:40]} (conf: {w['effective_confidence']:.0%})")

    if report.failing:
        lines.extend(["", "FAILING (reconsider)"])
        for w in report.failing:
            lines.append(f"  [!] {w['title'][:40]} ({w['success_rate']:.0%} success)")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def format_vichara(report: Dict) -> str:
    """Format Vichara report for display."""
    lines = [
        "=" * 60,
        "VICHARA - AUTONOMOUS INQUIRY",
        f"Timestamp: {report['timestamp']}",
        "=" * 60,
        "",
        f"ISSUES DIAGNOSED: {len(report['issues_found'])}",
    ]

    for issue in report["issues_found"]:
        sev = {"high": "[!]", "medium": "[~]", "low": "[.]"}.get(issue.get("severity"), "[ ]")
        lines.append(f"  {sev} {issue['message']} (confidence: {issue.get('confidence', 0):.0%})")

    if report["actions_taken"]:
        lines.extend(["", f"ACTIONS TAKEN: {len(report['actions_taken'])}"])
        for action in report["actions_taken"]:
            status = "[+]" if action.get("success") else "[-]"
            lines.append(f"  {status} {action['action']}: {action['details']}")

    if report["actions_deferred"]:
        lines.extend(["", f"ACTIONS DEFERRED: {len(report['actions_deferred'])}"])
        for action in report["actions_deferred"]:
            lines.append(f"  [~] {action['action'].get('type', 'unknown')}: {action['reason']}")

    if report["reflections"]:
        lines.extend(["", "REFLECTIONS"])
        for r in report["reflections"]:
            lines.append(f"  * {r}")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def format_svadhyaya(report: SvadhyayaReport, verbose: bool = False) -> str:
    """Format complete Svadhyaya report."""
    lines = [
        "=" * 70,
        "SVADHYAYA - COMPLETE SELF-STUDY",
        f"Timestamp: {report.timestamp}",
        "=" * 70,
    ]

    if report.darshana:
        lines.append("\n" + format_darshana(report.darshana, verbose))

    if report.jnana:
        lines.append("\n" + format_jnana(report.jnana))

    if report.insights:
        lines.extend(["", "=" * 60, "KEY INSIGHTS", "=" * 60])
        for insight in report.insights:
            icon = {"high": "[!]", "medium": "[~]", "low": "[.]"}.get(insight["severity"], "[ ]")
            lines.append(f"  {icon} {insight['message']}")
            lines.append(f"     -> {insight['suggestion']}")

    if report.vichara:
        lines.append("\n" + format_vichara(report.vichara))

    return "\n".join(lines)


# =============================================================================
# SOURCE ANALYSIS UTILITIES (from introspect.py)
# =============================================================================

def read_source() -> Dict[str, str]:
    """Read all Python source files in the soul package."""
    sources = {}
    for py_file in SOUL_PACKAGE.glob("*.py"):
        if py_file.name.startswith("_"):
            continue
        sources[py_file.name] = py_file.read_text()
    return sources


def get_source_stats() -> Dict:
    """Get statistics about the soul's codebase."""
    sources = read_source()
    total_lines = 0
    total_functions = 0
    total_classes = 0

    for content in sources.values():
        total_lines += len(content.split("\n"))
        total_functions += content.count("\ndef ")
        total_classes += content.count("\nclass ")

    return {
        "file_count": len(sources),
        "total_lines": total_lines,
        "total_functions": total_functions,
        "total_classes": total_classes,
        "files": list(sources.keys()),
    }


def find_todos() -> List[Dict]:
    """Find TODO and FIXME comments in source."""
    sources = read_source()
    issues = []
    for filename, content in sources.items():
        for i, line in enumerate(content.split("\n"), 1):
            for marker in ["TODO", "FIXME", "XXX", "HACK"]:
                if marker in line:
                    issues.append({
                        "file": filename,
                        "line": i,
                        "type": marker,
                        "content": line.strip(),
                    })
    return issues


# =============================================================================
# LEGACY REPORT FORMATS
# =============================================================================

def generate_introspection_report() -> Dict:
    """Generate a complete introspection report (legacy format)."""
    report = svadhyaya("deep")
    return {
        "generated_at": report.timestamp,
        "source": get_source_stats(),
        "todos": find_todos(),
        "wisdom_usage": jnana_applications(),
        "conversations": {},
        "pain_points": report.vedana,
        "metrics": analyze_metrics(),
        "insights": report.insights,
    }


def format_introspection_report(report: Dict) -> str:
    """Format introspection report (legacy format)."""
    lines = [
        "=" * 60,
        "SOUL INTROSPECTION REPORT",
        f"Generated: {report.get('generated_at', 'unknown')}",
        "=" * 60,
    ]

    src = report.get("source", {})
    lines.extend([
        "\n## Codebase",
        f"  Files: {src.get('file_count', 0)}",
        f"  Lines: {src.get('total_lines', 0)}",
        f"  Functions: {src.get('total_functions', 0)}",
    ])

    wu = report.get("wisdom_usage", {})
    lines.extend([
        f"\n## Wisdom Usage (last {wu.get('period_days', 30)} days)",
        f"  Total applications: {wu.get('total_applications', 0)}",
        f"  Unique wisdom used: {wu.get('unique_wisdom_applied', 0)}/{wu.get('total_wisdom', 0)}",
    ])

    pp = report.get("pain_points", {})
    lines.extend([
        "\n## Pain Points",
        f"  Open: {pp.get('total_open', 0)}",
    ])

    if report.get("insights"):
        lines.append("\n## Key Insights")
        for insight in report["insights"]:
            icon = {"high": "[!]", "medium": "[~]", "low": "[.]"}.get(insight.get("severity"), "[ ]")
            lines.append(f"  {icon} {insight.get('message', '')}")
            lines.append(f"     -> {insight.get('suggestion', '')}")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


# =============================================================================
# ADDITIONAL ANALYTICS - Wisdom Timeline, Decay, Trends
# =============================================================================

def get_wisdom_timeline(days: int = 30, bucket: str = "day") -> List[Dict]:
    """Get wisdom applications bucketed by time period.

    Args:
        days: Number of days to analyze
        bucket: Time bucket - "day", "week", or "month"

    Returns:
        List of dicts with period, applications, successes, failures
    """
    graph = get_synapse_graph()
    cutoff = (datetime.now() - timedelta(days=days)).isoformat()
    applications = graph.get_episodes(category="wisdom_application", limit=1000)

    buckets = {}
    for app in applications:
        created = app.get("created_at") or app.get("timestamp", "")
        if created < cutoff:
            continue

        try:
            content = app.get("content", "{}")
            if content.startswith("{"):
                data = json.loads(content)
                outcome = data.get("outcome")
            else:
                outcome = None
        except json.JSONDecodeError:
            outcome = None

        try:
            dt = datetime.fromisoformat(created)
            if bucket == "day":
                key = dt.strftime("%Y-%m-%d")
            elif bucket == "week":
                key = f"{dt.year}-W{dt.isocalendar()[1]:02d}"
            else:
                key = dt.strftime("%Y-%m")

            if key not in buckets:
                buckets[key] = {"period": key, "applications": 0, "successes": 0, "failures": 0}

            buckets[key]["applications"] += 1
            if outcome == "success":
                buckets[key]["successes"] += 1
            elif outcome == "failure":
                buckets[key]["failures"] += 1
        except ValueError:
            continue

    return sorted(buckets.values(), key=lambda x: x["period"])


def get_decay_visualization(limit: int = 20) -> Dict:
    """Get wisdom items with their decay curves for visualization.

    Args:
        limit: Maximum wisdom items to return

    Returns:
        Dict with wisdom list, decay_rate, and decay_unit
    """
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()
    applications = graph.get_episodes(category="wisdom_application", limit=1000)

    # Build last-used map
    last_used = {}
    for app in applications:
        try:
            content = app.get("content", "{}")
            if content.startswith("{"):
                data = json.loads(content)
                wid = data.get("wisdom_id")
                if wid:
                    created = app.get("created_at") or app.get("timestamp", "")
                    if wid not in last_used or created > last_used[wid]:
                        last_used[wid] = created
        except json.JSONDecodeError:
            pass

    wisdom_list = []
    now = datetime.now()

    # Sort by confidence descending
    sorted_wisdom = sorted(all_wisdom, key=lambda w: w.get("confidence", 0.8), reverse=True)

    for w in sorted_wisdom[:limit]:
        wid = w.get("id", "")
        title = w.get("title", "")
        confidence = w.get("confidence", 0.8)
        timestamp = w.get("created_at") or w.get("timestamp", "")

        try:
            created = datetime.fromisoformat(timestamp) if timestamp else now
            age_days = (now - created).days
        except ValueError:
            age_days = 0

        lu = last_used.get(wid)
        if lu:
            try:
                inactive_days = (now - datetime.fromisoformat(lu)).days
            except ValueError:
                inactive_days = age_days
        else:
            inactive_days = age_days

        months_inactive = inactive_days / 30.0
        current_decay = 0.95 ** months_inactive
        effective_conf = confidence * current_decay

        decay_curve = []
        for future_months in range(7):
            total_months = months_inactive + future_months
            decay = 0.95 ** total_months
            projected = confidence * decay
            decay_curve.append({
                "month": future_months,
                "confidence": round(projected, 3),
                "is_current": future_months == 0,
            })

        wisdom_list.append({
            "id": wid,
            "title": title[:40],
            "base_confidence": confidence,
            "effective_confidence": effective_conf,
            "inactive_days": inactive_days,
            "decay_curve": decay_curve,
        })

    return {"wisdom": wisdom_list, "decay_rate": 0.95, "decay_unit": "month"}


def format_decay_chart(decay_data: Dict) -> str:
    """Format decay visualization as ASCII chart.

    Args:
        decay_data: Output from get_decay_visualization()

    Returns:
        Formatted ASCII chart string
    """
    lines = [
        "=" * 70,
        "WISDOM DECAY VISUALIZATION",
        "=" * 70,
        f"\nDecay rate: {decay_data['decay_rate']:.0%} per {decay_data['decay_unit']}",
        "Bars show current effective confidence, projected 6 months\n",
    ]

    for w in decay_data["wisdom"][:15]:
        title = w["title"][:30].ljust(30)
        eff = w["effective_confidence"]
        base = w["base_confidence"]

        bar_len = int(eff * 40)
        bar = "#" * bar_len + "-" * (40 - bar_len)

        decay_pct = (1 - eff / base) * 100 if base > 0 else 0
        if decay_pct > 20:
            indicator = f"-{decay_pct:.0f}%"
        elif decay_pct > 0:
            indicator = f"-{decay_pct:.0f}%"
        else:
            indicator = "new"

        lines.append(f"{title} |{bar}| {eff:.0%} ({indicator})")

    lines.append("\n" + "=" * 70)
    return "\n".join(lines)


def format_trends_report(comparison: Dict, trajectory: Dict, patterns: Dict) -> str:
    """Format cross-session trends for CLI display.

    Args:
        comparison: Output from prajna_sessions()
        trajectory: Output from prajna_trajectory()
        patterns: Output from prajna_patterns()

    Returns:
        Formatted trends report string
    """
    lines = [
        "=" * 60,
        "CROSS-SESSION TRENDS",
        "=" * 60,
        f"\n## Growth Trajectory ({trajectory['period_days']} days)",
        f"  Total wisdom gained: {trajectory['total_wisdom_gained']}",
        f"  Domains explored: {trajectory['total_domains']}",
        f"  Weekly velocity: {trajectory['avg_weekly_velocity']} wisdom/week",
        f"  Recent velocity: {trajectory['recent_velocity']} ({trajectory['velocity_trend']})",
    ]

    if trajectory.get("trajectory"):
        lines.append("\n  Weekly progress:")
        for t in trajectory["trajectory"][-8:]:
            bar = "#" * min(20, t["gained"])
            new_d = f" +{len(t['new_domains'])}d" if t["new_domains"] else ""
            lines.append(f"    {t['week']}: {bar} {t['gained']}{new_d}")

    lines.extend([
        f"\n## Session Analysis ({comparison['sessions_analyzed']} sessions)",
        f"  Avg wisdom per session: {comparison['avg_wisdom_per_session']}",
    ])

    for s in comparison["sessions"][-5:]:
        gained = f"+{s['wisdom_gained']}" if s["wisdom_gained"] > 0 else "0"
        project = s["project"] or "unknown"
        lines.append(f"    {s['date']}: {project[:20]} ({gained} wisdom, {s['wisdom_applied']} applied)")

    lines.extend([
        "\n## Learning Patterns",
        f"  Dominant type: {patterns['dominant_type']}",
        f"  Growing domains: {', '.join(patterns['growing_domains'])}",
    ])
    if patterns["temporal_patterns"]["peak_hour"] is not None:
        lines.append(
            f"  Peak learning: {patterns['temporal_patterns']['peak_day']}s at {patterns['temporal_patterns']['peak_hour']}:00"
        )

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def analyze_conversation_patterns(days: int = 30) -> Dict:
    """Analyze conversation patterns.

    Args:
        days: Number of days to analyze

    Returns:
        Dict with total_conversations, by_project, avg_duration_seconds
    """
    graph = get_synapse_graph()
    cutoff = (datetime.now() - timedelta(days=days)).isoformat()

    sessions = graph.get_episodes(category="session_ledger", limit=1000)

    projects = Counter()
    durations = []

    for session in sessions:
        created = session.get("created_at") or session.get("timestamp", "")
        if created < cutoff:
            continue

        project = session.get("project") or "unknown"
        projects[project] += 1

        # Try to extract duration from content
        try:
            content = session.get("content", "{}")
            if content.startswith("{"):
                data = json.loads(content)
                if "duration_seconds" in data:
                    durations.append(data["duration_seconds"])
        except json.JSONDecodeError:
            pass

    return {
        "total_conversations": sum(projects.values()),
        "by_project": dict(projects),
        "avg_duration_seconds": sum(durations) / len(durations) if durations else 0,
    }
