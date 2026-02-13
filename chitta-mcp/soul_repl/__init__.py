"""
Soul REPL - RLM-style programmatic memory exploration

Provides a sandboxed Python REPL with soul.* methods for
dynamic memory exploration, following the Recursive Language
Model paradigm (Zhang, Kraska, Khattab 2025).

Instead of stuffing memories into context, Claude writes
code to explore them programmatically.
"""

from .executor import SoulREPL, execute_soul_code
from .sandbox import SoulAPI

__all__ = ["SoulREPL", "SoulAPI", "execute_soul_code"]
