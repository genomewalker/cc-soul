"""Edit strategies — mirror the semantics of the real tools so the
benchmark is hermetic (no daemon required).

Each strategy applies an edit and returns
  (new_content, output_payload, input_context)

- output_payload: what the caller LLM emits as the tool call args (size
  we bill as output tokens)
- input_context: what the caller LLM had to have read from the file in
  order to craft the call (billed as input tokens). Reflects the honest
  caller-side cost, not just tool-call output.
"""

from __future__ import annotations
import re


def _symbol_slice(original: str, args: dict) -> str:
    """Best-effort: return just the containing symbol body (what a caller
    would get from read_symbol / smart_context). Falls back to old_str plus
    2 lines of surrounding context."""
    sym = args.get("symbol")
    if sym:
        name = sym.split(".", 1)[-1]
        m = re.search(
            rf"^(?:@\w+\s*\n)*(?:[ \t]*)(?:def|class)\s+{re.escape(name)}\b.*?(?=^\S|\Z)",
            original, re.M | re.S,
        )
        if m:
            return m.group(0)
    old = args.get("old_str", "")
    idx = original.find(old) if old else -1
    if idx < 0:
        return old
    before = original.rfind("\n", 0, idx)
    lead = original.rfind("\n", 0, before) if before > 0 else -1
    after_end = original.find("\n", idx + len(old))
    tail = original.find("\n", after_end + 1) if after_end >= 0 else -1
    start = lead + 1 if lead >= 0 else 0
    end = tail if tail >= 0 else len(original)
    return original[start:end]


def baseline_read_write(original: str, args: dict) -> tuple[str, str, str]:
    """Read whole file, emit whole new file via Write."""
    new = original.replace(args["old_str"], args["new_str"], 1)
    return new, new, original


def baseline_edit(original: str, args: dict) -> tuple[str, str, str]:
    """Claude's default Edit tool: old_string + new_string, but old_string
    is typically verbose (3-5 lines of context). Simulate by padding old_str
    to include a larger surrounding context window."""
    old = args["old_str"]
    new = args["new_str"]
    # Expand old_str to a wider context block (~2 lines before/after).
    idx = original.find(old)
    if idx < 0:
        return original, f"{old}\n---\n{new}", original
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
    return new_content, payload, original


def file_patch(original: str, args: dict) -> tuple[str, str, str]:
    """Our file_patch: minimal old_str/new_str, uniqueness required.
    Input context = local slice the caller needed to inspect to craft the edit."""
    old = args["old_str"]
    new = args["new_str"]
    ctx = _symbol_slice(original, args)
    if original.count(old) != 1:
        return original, f"{old}\n---\n{new}", ctx
    payload = f"{old}\n---\n{new}"
    return original.replace(old, new, 1), payload, ctx


def symbol_patch(original: str, args: dict) -> tuple[str, str, str]:
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
            return original, f"{symbol}\n---\n{body}", _symbol_slice(original, args)
        cls_block = m.group(0)
        meth_re = re.compile(
            rf"^(?P<indent> +)def {re.escape(method)}\b.*?(?=^\1def |^\S|\Z)",
            re.M | re.S,
        )
        mm = meth_re.search(cls_block)
        if not mm:
            return original, f"{symbol}\n---\n{body}", _symbol_slice(original, args)
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
                return original, f"{symbol}\n---\n{body}", _symbol_slice(original, args)
            new_content = original[:ma.start()] + body + original[ma.end():]
        else:
            new_content = original[:m.start()] + body.rstrip() + "\n" + original[m.end():]
    payload = f"{symbol}\n---\n{body}"
    ctx = _symbol_slice(original, args)
    return new_content, payload, ctx


STRATEGIES = {
    "baseline_read_write": baseline_read_write,
    "baseline_edit":       baseline_edit,
    "file_patch":          file_patch,
    "symbol_patch":        symbol_patch,
}
