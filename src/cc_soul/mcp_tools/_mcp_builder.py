#!/usr/bin/env python3
"""
MCP Server Code Generator

Concatenates modular tool definitions into a single mcp_server.py.
This enables maintainable source files while preserving FastMCP's
requirement for a single `mcp` instance.

Usage:
    python -m cc_soul.mcp_tools._mcp_builder

The generated file should not be edited directly - edit files in
mcp_tools/ instead.
"""

from pathlib import Path
import re


def get_tools_dir() -> Path:
    """Get the mcp_tools directory."""
    return Path(__file__).parent


def get_output_path(test_mode: bool = False) -> Path:
    """Get the output path.

    Args:
        test_mode: If True, write to _mcp_server_generated.py for comparison.
                   If False, overwrite mcp_server.py.
    """
    parent = Path(__file__).parent.parent
    if test_mode:
        return parent / "_mcp_server_generated.py"
    return parent / "mcp_server.py"


def read_header() -> str:
    """Read the header template."""
    header_path = get_tools_dir() / "_header.py"
    content = header_path.read_text()
    # Strip the === HEADER === marker comment
    lines = content.split("\n")
    if lines and lines[0].startswith("# === HEADER ==="):
        lines = lines[1:]
    return "\n".join(lines).strip()


def get_tool_files() -> list[Path]:
    """Get all tool module files in sorted order."""
    tools_dir = get_tools_dir()
    files = []
    for f in sorted(tools_dir.glob("*.py")):
        # Skip private/special files
        if f.name.startswith("_"):
            continue
        files.append(f)
    return files


def transform_imports(content: str) -> str:
    """Transform relative imports for the generated file.

    Tool files in mcp_tools/ use 'from ..' to import from cc_soul.
    The generated mcp_server.py is in cc_soul/, so these become 'from .'.
    """
    # Transform 'from ..' to 'from .' (double dot to single dot)
    # This handles imports like 'from ..svadhyaya' -> 'from .svadhyaya'
    content = re.sub(r'from \.\.([a-zA-Z_])', r'from .\1', content)
    return content


def extract_tool_content(path: Path) -> str:
    """Extract tool definitions from a module file.

    Strips any module-level imports that would conflict with the
    generated file's structure (tool functions use inline imports).
    Also transforms relative imports for the generated file's location.
    """
    content = path.read_text()
    content = transform_imports(content)
    return content.strip()


def generate_footer() -> str:
    """Generate the server entry point."""
    return '''

# =============================================================================
# Server Entry Point
# =============================================================================


def main():
    """Run the soul MCP server."""
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
'''


def build_mcp_server() -> str:
    """Build the complete mcp_server.py content."""
    parts = []

    # Header with imports and FastMCP initialization
    parts.append(read_header())

    # All tool modules
    for tool_file in get_tool_files():
        parts.append(f"\n\n{extract_tool_content(tool_file)}")

    # Footer with main()
    parts.append(generate_footer())

    return "\n".join(parts)


def main():
    """Generate mcp_server.py from tool modules."""
    import sys

    test_mode = "--test" in sys.argv
    output_path = get_output_path(test_mode)
    content = build_mcp_server()

    # Write the generated file
    output_path.write_text(content)

    # Count tools
    tool_count = len(re.findall(r"@mcp\.tool\(\)", content))

    print(f"Generated {output_path}")
    print(f"  Tools: {tool_count}")
    print(f"  Lines: {len(content.splitlines())}")
    if test_mode:
        print("  (test mode - original mcp_server.py preserved)")


if __name__ == "__main__":
    main()
