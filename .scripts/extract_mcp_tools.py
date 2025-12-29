#!/usr/bin/env python3
"""Extract tool sections from mcp_server.py into separate module files."""

import re
from pathlib import Path

def extract_sections(content: str) -> list[dict]:
    """Parse mcp_server.py and extract all tool sections."""
    sections = []

    # Pattern for section headers
    section_pattern = re.compile(
        r'^# =+\n# (.+?)\n# =+\n',
        re.MULTILINE
    )

    # Find all section matches with their positions
    matches = list(section_pattern.finditer(content))

    for i, match in enumerate(matches):
        title = match.group(1).strip()
        start = match.end()

        # End at next section or EOF
        end = matches[i + 1].start() if i + 1 < len(matches) else len(content)

        section_content = content[start:end].strip()

        # Skip header (before first section with tools) and Server Entry Point
        if not section_content or 'Server Entry Point' in title:
            continue

        # Only include sections with @mcp.tool()
        if '@mcp.tool()' not in section_content:
            continue

        # Generate filename from title
        filename = title.lower()
        filename = re.sub(r'[^a-z0-9]+', '_', filename)
        filename = re.sub(r'_+', '_', filename).strip('_')
        filename = filename[:40]  # Limit length

        sections.append({
            'title': title,
            'filename': filename,
            'content': section_content,
            'tool_count': section_content.count('@mcp.tool()')
        })

    return sections


def write_module(output_dir: Path, section: dict) -> None:
    """Write a section to a module file."""
    filepath = output_dir / f"{section['filename']}.py"

    header = f"# =============================================================================\n"
    header += f"# {section['title']}\n"
    header += f"# =============================================================================\n\n"

    content = header + section['content'] + "\n"

    filepath.write_text(content)
    print(f"  {section['filename']}.py ({section['tool_count']} tools)")


def main():
    src_dir = Path(__file__).parent.parent / "src" / "cc_soul"
    mcp_server = src_dir / "mcp_server.py"
    output_dir = src_dir / "mcp_tools"

    content = mcp_server.read_text()
    sections = extract_sections(content)

    print(f"Found {len(sections)} sections with {sum(s['tool_count'] for s in sections)} tools\n")

    for section in sections:
        write_module(output_dir, section)

    print(f"\nExtracted to {output_dir}")


if __name__ == "__main__":
    main()
