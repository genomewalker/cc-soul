"""
Soul Self-Introspection - The soul examines its own codebase.

This module implements deep code introspection using the Antahkarana
(inner instrument) framework. Multiple perspectives analyze the code:

- Manas (Sensory): Quick surface scan - obvious issues
- Buddhi (Intellect): Deep analysis - architecture, patterns
- Ahamkara (Critical): Find flaws - what could break
- Vikalpa (Creative): Novel improvements - missing features
- Sakshi (Witness): Essential truth - what really matters

The findings feed into the evolution cycle (Vikāsa) for self-improvement.
"""

import ast
import os
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Set
from datetime import datetime
from enum import Enum
from collections import defaultdict


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
    COHERENCE = "coherence"  # Violates soul beliefs


@dataclass
class CodeIssue:
    """A detected code issue."""
    category: IssueCategory
    severity: IssueSeverity
    file: str
    line: int
    message: str
    suggestion: str = ""
    related_belief: str = ""  # Which belief this violates


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
class IntrospectionReport:
    """Complete introspection report."""
    timestamp: str
    modules_scanned: int
    total_lines: int
    total_issues: int
    issues_by_category: Dict[str, int]
    issues_by_severity: Dict[str, int]
    issues: List[CodeIssue]
    metrics: List[ModuleMetrics]
    belief_violations: List[CodeIssue]
    missing_features: List[str]
    optimization_opportunities: List[str]
    evolution_proposals: List[Dict]


class CodeScanner:
    """Static analysis scanner for the soul codebase."""

    def __init__(self, root_dir: Path = None):
        if root_dir is None:
            root_dir = Path(__file__).parent
        self.root_dir = root_dir
        self.issues: List[CodeIssue] = []
        self.metrics: List[ModuleMetrics] = []

    def scan_all(self, exclude_patterns: List[str] = None) -> List[ModuleMetrics]:
        """Scan all Python files in the codebase."""
        exclude = exclude_patterns or ["__pycache__", ".git", "test_"]

        for py_file in self.root_dir.rglob("*.py"):
            if any(p in str(py_file) for p in exclude):
                continue
            metrics = self.scan_file(py_file)
            self.metrics.append(metrics)

        return self.metrics

    def scan_file(self, filepath: Path) -> ModuleMetrics:
        """Scan a single Python file."""
        try:
            content = filepath.read_text()
        except Exception:
            return self._error_metrics(filepath, "Cannot read file")

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
            return self._error_metrics(filepath, "Syntax error")

        # Collect structures
        functions = [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef)]
        async_functions = [n for n in ast.walk(tree) if isinstance(n, ast.AsyncFunctionDef)]
        all_functions = functions + async_functions
        classes = [n for n in ast.walk(tree) if isinstance(n, ast.ClassDef)]

        # Run checks
        rel_path = str(filepath.relative_to(self.root_dir))
        self._check_bare_except(tree, rel_path)
        self._check_empty_except(tree, rel_path)
        self._check_mutable_defaults(all_functions, rel_path)
        self._check_complexity(all_functions, rel_path)
        self._check_too_many_arguments(all_functions, rel_path)
        self._check_missing_docstrings(all_functions, classes, rel_path)
        self._check_hardcoded_values(tree, rel_path)

        return ModuleMetrics(
            file=rel_path,
            lines=lines,
            functions=len(all_functions),
            classes=len(classes),
            avg_complexity=self._calc_avg_complexity(all_functions),
            type_hint_coverage=self._calc_type_coverage(all_functions),
            docstring_coverage=self._calc_docstring_coverage(all_functions, classes),
        )

    def _error_metrics(self, filepath: Path, error: str) -> ModuleMetrics:
        return ModuleMetrics(
            file=str(filepath.relative_to(self.root_dir)),
            lines=0, functions=0, classes=0,
            avg_complexity=0, type_hint_coverage=0, docstring_coverage=0
        )

    def _check_bare_except(self, tree: ast.AST, filepath: str):
        """Bare except catches everything including SystemExit."""
        for node in ast.walk(tree):
            if isinstance(node, ast.ExceptHandler) and node.type is None:
                self.issues.append(CodeIssue(
                    category=IssueCategory.BUG,
                    severity=IssueSeverity.MEDIUM,
                    file=filepath,
                    line=node.lineno,
                    message="Bare except clause catches all exceptions",
                    suggestion="Use 'except Exception:' to avoid catching SystemExit/KeyboardInterrupt"
                ))

    def _check_empty_except(self, tree: ast.AST, filepath: str):
        """Empty except blocks hide errors."""
        for node in ast.walk(tree):
            if isinstance(node, ast.ExceptHandler):
                if len(node.body) == 1 and isinstance(node.body[0], ast.Pass):
                    self.issues.append(CodeIssue(
                        category=IssueCategory.SMELL,
                        severity=IssueSeverity.LOW,
                        file=filepath,
                        line=node.lineno,
                        message="Empty except block silently ignores errors",
                        suggestion="Log the error or add a comment explaining why it's ignored"
                    ))

    def _check_mutable_defaults(self, functions: List, filepath: str):
        """Mutable default arguments are a classic Python gotcha."""
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
        """High cyclomatic complexity makes code hard to understand."""
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
        """Too many arguments suggest the function does too much."""
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
        """Public functions and classes should have docstrings."""
        for func in functions:
            if not func.name.startswith("_"):
                if not ast.get_docstring(func):
                    self.issues.append(CodeIssue(
                        category=IssueCategory.DOCUMENTATION,
                        severity=IssueSeverity.LOW,
                        file=filepath,
                        line=func.lineno,
                        message=f"Missing docstring for {func.name}"
                    ))
        for cls in classes:
            if not cls.name.startswith("_"):
                if not ast.get_docstring(cls):
                    self.issues.append(CodeIssue(
                        category=IssueCategory.DOCUMENTATION,
                        severity=IssueSeverity.LOW,
                        file=filepath,
                        line=cls.lineno,
                        message=f"Missing docstring for class {cls.name}"
                    ))

    def _check_hardcoded_values(self, tree: ast.AST, filepath: str):
        """Magic numbers and hardcoded strings should be constants."""
        for node in ast.walk(tree):
            if isinstance(node, ast.Constant):
                if isinstance(node.value, (int, float)):
                    # Skip common safe values
                    if node.value in (0, 1, -1, 2, 100, 1000, 0.0, 1.0, 0.5):
                        continue
                    # This is a heuristic - many false positives
                    # Only flag if in a comparison or calculation context
                    pass  # Too noisy for now

    def _calc_complexity(self, func) -> int:
        """Calculate cyclomatic complexity."""
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
        total = sum(self._calc_complexity(f) for f in functions)
        return total / len(functions)

    def _calc_type_coverage(self, functions: List) -> float:
        """Calculate what fraction of functions have type hints."""
        if not functions:
            return 0.0
        typed = 0
        for func in functions:
            has_return = func.returns is not None
            has_args = any(arg.annotation for arg in func.args.args)
            if has_return or has_args:
                typed += 1
        return typed / len(functions)

    def _calc_docstring_coverage(self, functions: List, classes: List) -> float:
        """Calculate what fraction have docstrings."""
        items = functions + classes
        if not items:
            return 0.0
        documented = sum(1 for item in items if ast.get_docstring(item))
        return documented / len(items)


class BeliefChecker:
    """Check code against soul beliefs."""

    def __init__(self):
        from .wisdom import get_all_wisdom, WisdomType
        self.beliefs = []
        for w in get_all_wisdom():
            if w.wisdom_type == WisdomType.BELIEF:
                self.beliefs.append(w)

    def check_violations(self, scanner: CodeScanner) -> List[CodeIssue]:
        """Check for belief violations in the issues found."""
        violations = []

        # Map beliefs to issue patterns
        belief_patterns = {
            "Simplicity over cleverness": [
                "High complexity",
                "Too many arguments",
            ],
            "Question every assumption": [],
            "Record learnings in the moment": [],
        }

        for issue in scanner.issues:
            for belief, patterns in belief_patterns.items():
                for pattern in patterns:
                    if pattern.lower() in issue.message.lower():
                        issue.related_belief = belief
                        issue.category = IssueCategory.COHERENCE
                        violations.append(issue)
                        break

        return violations


class SoulIntrospector:
    """
    The main introspection engine.

    Uses multiple perspectives (voices) to analyze the codebase:
    1. Manas (Quick): Surface-level scan
    2. Buddhi (Deep): Architectural analysis
    3. Ahamkara (Critical): What could break?
    4. Vikalpa (Creative): What's missing?
    5. Sakshi (Essential): What really matters?
    """

    def __init__(self, root_dir: Path = None):
        if root_dir is None:
            root_dir = Path(__file__).parent
        self.root_dir = root_dir
        self.scanner = CodeScanner(root_dir)
        self.belief_checker = BeliefChecker()

    def introspect(self, depth: str = "standard") -> IntrospectionReport:
        """
        Run introspection at specified depth.

        Depths:
        - quick: Surface scan only (Manas)
        - standard: Basic + architectural (Manas + Buddhi)
        - deep: All perspectives (all voices)
        - ultrathink: Deep + cross-referencing with wisdom/intentions
        """
        # Phase 1: Scan (Manas - quick perception)
        self.scanner.scan_all()

        # Phase 2: Analyze (Buddhi - deeper understanding)
        if depth in ("standard", "deep", "ultrathink"):
            self._analyze_architecture()

        # Phase 3: Critique (Ahamkara - find flaws)
        if depth in ("deep", "ultrathink"):
            self._critical_analysis()

        # Phase 4: Vision (Vikalpa - what's missing)
        missing_features = []
        optimization_opportunities = []
        if depth in ("deep", "ultrathink"):
            missing_features = self._identify_missing_features()
            optimization_opportunities = self._identify_optimizations()

        # Phase 5: Synthesis (Sakshi - essential truth)
        belief_violations = self.belief_checker.check_violations(self.scanner)

        # Generate evolution proposals
        evolution_proposals = []
        if depth == "ultrathink":
            evolution_proposals = self._generate_evolution_proposals()

        # Build report
        issues_by_cat = defaultdict(int)
        issues_by_sev = defaultdict(int)
        for issue in self.scanner.issues:
            issues_by_cat[issue.category.value] += 1
            issues_by_sev[issue.severity.value] += 1

        return IntrospectionReport(
            timestamp=datetime.now().isoformat(),
            modules_scanned=len(self.scanner.metrics),
            total_lines=sum(m.lines for m in self.scanner.metrics),
            total_issues=len(self.scanner.issues),
            issues_by_category=dict(issues_by_cat),
            issues_by_severity=dict(issues_by_sev),
            issues=self.scanner.issues,
            metrics=self.scanner.metrics,
            belief_violations=belief_violations,
            missing_features=missing_features,
            optimization_opportunities=optimization_opportunities,
            evolution_proposals=evolution_proposals,
        )

    def _analyze_architecture(self):
        """Architectural analysis (Buddhi perspective)."""
        # Check for circular imports (simplified)
        # Check for module cohesion
        # Check for proper separation of concerns
        pass  # TODO: Implement

    def _critical_analysis(self):
        """Critical analysis - what could break? (Ahamkara perspective)."""
        # Security issues
        # Race conditions
        # Resource leaks
        pass  # TODO: Implement

    def _identify_missing_features(self) -> List[str]:
        """Identify missing features based on intentions/aspirations."""
        missing = []

        try:
            from .intentions import get_active_intentions
            from .aspirations import get_active_aspirations

            # Check if intentions have corresponding code
            for intention in get_active_intentions():
                # This is a placeholder - would need NLP to match
                pass

            # Check aspirations
            for aspiration in get_active_aspirations():
                pass

        except Exception:
            pass

        return missing

    def _identify_optimizations(self) -> List[str]:
        """Identify optimization opportunities."""
        optimizations = []

        # Find slow patterns
        for metrics in self.scanner.metrics:
            if metrics.avg_complexity > 8:
                optimizations.append(
                    f"{metrics.file}: High average complexity ({metrics.avg_complexity:.1f}) - consider refactoring"
                )
            if metrics.lines > 500:
                optimizations.append(
                    f"{metrics.file}: Large file ({metrics.lines} lines) - consider splitting"
                )

        return optimizations

    def _generate_evolution_proposals(self) -> List[Dict]:
        """Generate concrete evolution proposals from findings."""
        proposals = []

        # Group issues by file for batch fixes
        by_file = defaultdict(list)
        for issue in self.scanner.issues:
            if issue.severity in (IssueSeverity.HIGH, IssueSeverity.CRITICAL):
                by_file[issue.file].append(issue)

        for filepath, issues in by_file.items():
            if len(issues) >= 2:
                proposals.append({
                    "category": "bug_fix",
                    "title": f"Fix {len(issues)} issues in {filepath}",
                    "description": "\n".join(f"- {i.message}" for i in issues),
                    "priority": "high",
                    "files": [filepath],
                })

        return proposals


def run_introspection(depth: str = "standard", target: str = None) -> IntrospectionReport:
    """
    Run soul self-introspection.

    Args:
        depth: quick, standard, deep, or ultrathink
        target: Specific module to focus on (None = all)

    Returns:
        IntrospectionReport with findings
    """
    root = Path(__file__).parent
    if target:
        root = root / target

    introspector = SoulIntrospector(root)
    return introspector.introspect(depth)


def format_report(report: IntrospectionReport, verbose: bool = False) -> str:
    """Format report for display."""
    lines = [
        "=" * 60,
        "SOUL SELF-INTROSPECTION REPORT",
        "=" * 60,
        f"Timestamp: {report.timestamp}",
        f"Modules scanned: {report.modules_scanned}",
        f"Total lines: {report.total_lines:,}",
        f"Total issues: {report.total_issues}",
        "",
        "ISSUES BY SEVERITY",
        "-" * 40,
    ]

    for sev in ["critical", "high", "medium", "low"]:
        count = report.issues_by_severity.get(sev, 0)
        if count > 0:
            icon = {"critical": "🔴", "high": "🟠", "medium": "🟡", "low": "🟢"}[sev]
            lines.append(f"  {icon} {sev.upper()}: {count}")

    lines.extend([
        "",
        "ISSUES BY CATEGORY",
        "-" * 40,
    ])

    for cat, count in sorted(report.issues_by_category.items(), key=lambda x: -x[1]):
        lines.append(f"  {cat}: {count}")

    if report.belief_violations:
        lines.extend([
            "",
            "⚠️  BELIEF VIOLATIONS",
            "-" * 40,
        ])
        for v in report.belief_violations[:5]:
            lines.append(f"  {v.file}:{v.line} - {v.message}")
            if v.related_belief:
                lines.append(f"    Violates: \"{v.related_belief}\"")

    if verbose:
        lines.extend([
            "",
            "ALL ISSUES",
            "-" * 40,
        ])
        for issue in sorted(report.issues, key=lambda x: (x.severity.value, x.file)):
            lines.append(f"  [{issue.severity.value}] {issue.file}:{issue.line}")
            lines.append(f"    {issue.message}")
            if issue.suggestion:
                lines.append(f"    → {issue.suggestion}")

    if report.optimization_opportunities:
        lines.extend([
            "",
            "OPTIMIZATION OPPORTUNITIES",
            "-" * 40,
        ])
        for opt in report.optimization_opportunities[:5]:
            lines.append(f"  • {opt}")

    if report.evolution_proposals:
        lines.extend([
            "",
            "EVOLUTION PROPOSALS",
            "-" * 40,
        ])
        for prop in report.evolution_proposals[:3]:
            lines.append(f"  📋 {prop['title']}")
            lines.append(f"     Priority: {prop['priority']}")

    lines.append("")
    lines.append("=" * 60)

    return "\n".join(lines)
