"""Edit strategies — mirror the semantics of the real tools so the
benchmark is hermetic (no daemon required).

Each strategy applies an edit and returns (new_content, args_payload).
args_payload is what the model would have had to emit — its size is
what we measure.
"""

from __future__ import annotations
import re


def baseline_read_write(original: str, args: dict) -> tuple[str, str]:
    """Read whole file, emit whole new file via Write."""
    # Worst-case: we compute the expected by applying file_patch semantics,
    # then pretend the model had to emit the full replacement.
    new = original.replace(args["old_str"], args["new_str"], 1)
    # Payload the model emits: full new file content.
    return new, new


def baseline_edit(original: str, args: dict) -> tuple[str, str]:
    """Claude's default Edit tool: old_string + new_string, but old_string
    is typically verbose (3-5 lines of context). Simulate by padding old_str
    to include a larger surrounding context window."""
    old = args["old_str"]
    new = args["new_str"]
    # Expand old_str to a wider context block (~2 lines before/after).
    idx = original.find(old)
    if idx < 0:
        return original, f"{old}\n---\n{new}"
    before = original[:idx].rsplit("\n", 3)
    after = original[idx + len(old):].split("\n", 3)
    pad_before = "\n".join(before[-2:]) if len(before) > 1 else ""
    pad_after = "\n".join(after[:2])
    padded_old = (pad_before + ("\n" if pad_before else "") + old +
                  ("\n" if pad_after else "") + pad_after)
    padded_new = (pad_before + ("\n" if pad_before else "") + new +
                  ("\n" if pad_after else "") + pad_after)
    new_content = original.replace(padded_old, padded_new, 1)
    if new_content == original:
        new_content = original.replace(old, new, 1)
    payload = f"{padded_old}\n---\n{padded_new}"
    return new_content, payload


def file_patch(original: str, args: dict) -> tuple[str, str]:
    """Our file_patch: minimal old_str/new_str, uniqueness required."""
    old = args["old_str"]
    new = args["new_str"]
    if original.count(old) != 1:
        return original, f"{old}\n---\n{new}"  # would fail uniqueness
    payload = f"{old}\n---\n{new}"
    return original.replace(old, new, 1), payload


def symbol_patch(original: str, args: dict) -> tuple[str, str]:
    """Our symbol_patch: replace a named symbol's body.

    Simplified re-implementation: finds `def <name>` or `class <name>`
    (with optional dotted Class.method) and replaces the full block.
    Good enough for Python benchmark cases; real tool uses tree-sitter.
    """
    symbol = args["symbol"]
    body = args["body"]
    if "." in symbol:
        cls, method = symbol.split(".", 1)
        # Locate class, then method inside
        cls_re = re.compile(rf"^class {re.escape(cls)}\b.*?(?=^\S|\Z)", re.M | re.S)
        m = cls_re.search(original)
        if not m:
            return original, f"{symbol}\n---\n{body}"
        cls_block = m.group(0)
        meth_re = re.compile(
            rf"^(?P<indent> +)def {re.escape(method)}\b.*?(?=^\1def |^\S|\Z)",
            re.M | re.S,
        )
        mm = meth_re.search(cls_block)
        if not mm:
            return original, f"{symbol}\n---\n{body}"
        # Ensure replacement ends with newline, and preserve blank line after
        new_method = body.rstrip() + "\n"
        # Preserve trailing blank line if original had one
        original_block = mm.group(0)
        if original_block.endswith("\n\n"):
            new_method += "\n"
        new_cls_block = cls_block[:mm.start()] + new_method + cls_block[mm.end():]
        new_content = original[:m.start()] + new_cls_block + original[m.end():]
    else:
        sym_re = re.compile(
            rf"^(?:@\w+\s*\n)*(?:def|class) {re.escape(symbol)}\b.*?(?=^\S|\Z)",
            re.M | re.S,
        )
        m = sym_re.search(original)
        if not m:
            # Module-level assignment (e.g. ALLOWED = [...])
            assign_re = re.compile(rf"^{re.escape(symbol)}\s*=.*?$", re.M)
            ma = assign_re.search(original)
            if not ma:
                return original, f"{symbol}\n---\n{body}"
            new_content = original[:ma.start()] + body + original[ma.end():]
        else:
            new_content = original[:m.start()] + body.rstrip() + "\n" + original[m.end():]
    payload = f"{symbol}\n---\n{body}"
    return new_content, payload


STRATEGIES = {
    "baseline_read_write": baseline_read_write,
    "baseline_edit":       baseline_edit,
    "file_patch":          file_patch,
    "symbol_patch":        symbol_patch,
}
