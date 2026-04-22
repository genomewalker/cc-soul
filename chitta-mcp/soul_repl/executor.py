"""
SoulREPL - Sandboxed Python executor for RLM-style exploration

Provides a safe execution environment with soul.* methods exposed.
Tracks all code executed for trajectory recording.
"""

import ast
import sys
import traceback
from io import StringIO
from typing import Any
from dataclasses import dataclass, field

from .sandbox import SoulAPI, Memory, Triplet


# Safe builtins - no file I/O, no exec/eval, no imports
SAFE_BUILTINS = {
    # Types
    "True": True,
    "False": False,
    "None": None,
    "int": int,
    "float": float,
    "str": str,
    "bool": bool,
    "list": list,
    "dict": dict,
    "set": set,
    "tuple": tuple,
    "type": type,
    "object": object,
    "bytes": bytes,

    # Functions
    "len": len,
    "range": range,
    "enumerate": enumerate,
    "zip": zip,
    "map": map,
    "filter": filter,
    "sorted": sorted,
    "reversed": reversed,
    "min": min,
    "max": max,
    "sum": sum,
    "abs": abs,
    "round": round,
    "all": all,
    "any": any,
    "isinstance": isinstance,
    "issubclass": issubclass,
    "hasattr": hasattr,
    "getattr": getattr,
    "setattr": setattr,
    "repr": repr,
    "str": str,
    "print": print,  # Will be redirected to capture output
    "format": format,
    "chr": chr,
    "ord": ord,

    # Iteration
    "iter": iter,
    "next": next,

    # Exceptions (for try/except)
    "Exception": Exception,
    "ValueError": ValueError,
    "TypeError": TypeError,
    "KeyError": KeyError,
    "IndexError": IndexError,
    "AttributeError": AttributeError,
    "RuntimeError": RuntimeError,
    "StopIteration": StopIteration,
}

# Forbidden AST nodes that could escape sandbox
FORBIDDEN_NODES = {
    ast.Import,
    ast.ImportFrom,
    ast.Global,
    ast.Nonlocal,
}


def validate_ast(code: str) -> tuple[bool, str]:
    """Validate code AST for safety."""
    try:
        tree = ast.parse(code)
    except SyntaxError as e:
        return False, f"Syntax error: {e}"

    for node in ast.walk(tree):
        if type(node) in FORBIDDEN_NODES:
            return False, f"Forbidden construct: {type(node).__name__}"

        # Check for dangerous attribute access
        if isinstance(node, ast.Attribute):
            if node.attr.startswith("_"):
                return False, f"Cannot access private attributes: {node.attr}"

        # Check for dangerous function calls
        if isinstance(node, ast.Call):
            if isinstance(node.func, ast.Name):
                if node.func.id in ("exec", "eval", "compile", "__import__",
                                    "open", "input", "breakpoint"):
                    return False, f"Forbidden function: {node.func.id}"

    return True, ""


@dataclass
class ExecutionResult:
    """Result of code execution."""
    success: bool
    output: str
    result: Any = None
    error: str = ""
    trajectory: list = field(default_factory=list)

    def __repr__(self):
        if self.success:
            return f"ExecutionResult(success=True, output={len(self.output)} chars)"
        return f"ExecutionResult(success=False, error='{self.error[:50]}')"


class SoulREPL:
    """
    RLM-style REPL for programmatic soul exploration.

    Provides a sandboxed Python environment with soul.* methods.
    Tracks execution history and exploration trajectory.

    Example:
        repl = SoulREPL()
        result = repl.execute('''
            memories = soul.search("authentication", limit=10)
            relevant = [m for m in memories if m.score > 0.7]
            for m in relevant[:3]:
                print(f"{m.id}: {m.content[:50]}")
        ''')
        print(result.output)
        print(repl.trajectory)  # All soul.* calls made
    """

    def __init__(self, max_output: int = 10000, max_iterations: int = 100):
        self.max_output = max_output
        self.max_iterations = max_iterations
        self.soul = SoulAPI()
        self.namespace = {}
        self.history = []
        self._reset_namespace()

    def _reset_namespace(self):
        """Reset execution namespace to safe defaults."""
        self.namespace = {
            "__builtins__": SAFE_BUILTINS,
            "soul": self.soul,
            "Memory": Memory,
            "Triplet": Triplet,
        }

    def execute(self, code: str, reset: bool = False) -> ExecutionResult:
        """
        Execute code in the sandbox.

        Args:
            code: Python code to execute
            reset: If True, reset namespace before execution

        Returns:
            ExecutionResult with output, result, and trajectory
        """
        if reset:
            self._reset_namespace()
            self.soul.clear_trajectory()

        # Validate AST
        valid, error = validate_ast(code)
        if not valid:
            return ExecutionResult(
                success=False,
                output="",
                error=f"Code validation failed: {error}"
            )

        # Capture stdout
        old_stdout = sys.stdout
        captured = StringIO()
        sys.stdout = captured

        result = None
        error = ""

        try:
            # Execute code
            tree = ast.parse(code)

            # If the last statement is an expression, capture its value
            last_expr = None
            if tree.body and isinstance(tree.body[-1], ast.Expr):
                last_expr = tree.body.pop()

            # Execute all but last expression
            if tree.body:
                exec(compile(tree, "<repl>", "exec"), self.namespace)

            # Evaluate last expression if present
            if last_expr:
                result = eval(
                    compile(ast.Expression(last_expr.value), "<repl>", "eval"),
                    self.namespace
                )
                if result is not None:
                    print(repr(result))

        except Exception as e:
            error = f"{type(e).__name__}: {e}\n{traceback.format_exc()}"

        finally:
            sys.stdout = old_stdout

        output = captured.getvalue()
        if len(output) > self.max_output:
            output = output[:self.max_output] + f"\n... (truncated, {len(output)} total chars)"

        # Record history
        self.history.append({
            "code": code,
            "output": output[:500],
            "success": not error,
            "trajectory_count": len(self.soul.trajectory())
        })

        return ExecutionResult(
            success=not error,
            output=output,
            result=result,
            error=error,
            trajectory=self.soul.trajectory()
        )

    def trajectory(self) -> list[dict]:
        """Get the exploration trajectory."""
        return self.soul.trajectory()

    def clear(self):
        """Clear namespace and trajectory."""
        self._reset_namespace()
        self.soul.clear_trajectory()
        self.history = []

    _SERIALIZABLE = (str, int, float, bool, type(None), list, dict, tuple)

    def get_serializable_namespace(self) -> dict:
        """Return namespace subset safe to JSON-serialize (skip builtins/callables)."""
        out = {}
        for k, v in self.namespace.items():
            if k.startswith('_') or k in ('soul', 'Memory', 'Triplet'):
                continue
            if not isinstance(v, self._SERIALIZABLE):
                continue
            try:
                import json as _json
                _json.dumps(v)
                out[k] = v
            except (TypeError, ValueError):
                pass
        return out

    def restore_namespace(self, data: dict) -> None:
        """Restore serializable variables into the sandbox namespace."""
        for k, v in data.items():
            if not k.startswith('_') and k not in ('soul', 'Memory', 'Triplet'):
                self.namespace[k] = v


def execute_soul_code(
    code: str,
    max_output: int = 10000,
    initial_namespace: dict | None = None,
) -> tuple["ExecutionResult", dict]:
    """Execute code in a fresh REPL, optionally seeded with a prior namespace.

    Returns (result, serializable_namespace) so callers can persist session state.
    """
    repl = SoulREPL(max_output=max_output)
    if initial_namespace:
        repl.restore_namespace(initial_namespace)
    result = repl.execute(code)
    return result, repl.get_serializable_namespace()
