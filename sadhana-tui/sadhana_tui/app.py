"""Sadhana TUI — Terminal UI for autonomous agents."""

import json as _json
import time as _time
from datetime import datetime

from textual.app import App, ComposeResult
from textual.widgets import Static, Input, Label, Select, Button
from textual.containers import Container, Horizontal, Vertical, ScrollableContainer
from textual.screen import ModalScreen
from textual.binding import Binding
from textual.reactive import reactive
from textual.message import Message
from rich.text import Text
from rich.console import RenderableType

from .client import ChittaClient


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# AGENT ROW — vertical sidebar entry
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class AgentRow(Static, can_focus=True):
    """One agent in the sidebar list."""

    selected = reactive(False)

    class Clicked(Message):
        def __init__(self, index: int) -> None:
            self.index = index
            super().__init__()

    DEFAULT_CSS = """
    AgentRow {
        width: 100%;
        height: 5;
        padding: 0 1;
        border-left: tall #252525;
        border-bottom: solid #111111;
    }
    AgentRow:hover  { background: #131313; }
    AgentRow.selected           { background: #181818; }
    AgentRow.running            { border-left: tall #3a7a3a; }
    AgentRow.paused             { border-left: tall #7a7a3a; }
    AgentRow.done               { border-left: tall #444444; }
    AgentRow.failed             { border-left: tall #7a3a3a; }
    AgentRow.selected.running   { border-left: tall #55bb55; }
    AgentRow.selected.paused    { border-left: tall #bbbb55; }
    AgentRow.selected.done      { border-left: tall #5599aa; }
    AgentRow.selected.failed    { border-left: tall #bb5555; }
    """

    _STATE_FG = {
        "running": "#55bb55", "paused": "#bbbb55",
        "done":    "#5599aa", "failed": "#bb5555",
    }

    def __init__(self, sadhana: dict, index: int, **kwargs):
        super().__init__(**kwargs)
        self.sadhana = sadhana
        self.index = index
        self.add_class(sadhana.get("state", "unknown"))

    @property
    def sadhana_id(self) -> int | None:
        return self.sadhana.get("id")

    def update_data(self, sadhana: dict, index: int) -> None:
        old_state = self.sadhana.get("state", "unknown")
        new_state = sadhana.get("state", "unknown")
        if old_state != new_state:
            self.remove_class(old_state)
            self.add_class(new_state)
        self.sadhana = sadhana
        self.index = index
        self.refresh()

    def watch_selected(self, selected: bool) -> None:
        self.set_class(selected, "selected")

    def on_click(self) -> None:
        self.post_message(self.Clicked(self.index))

    def render(self) -> Text:
        s = self.sadhana
        state = s.get("state", "unknown")
        sel = self.selected
        sc = self._STATE_FG.get(state, "#555555")

        t = Text()
        # Row 1: id + state pill
        t.append(f"#{s.get('id', '?'):02d}", style="#cccccc" if sel else "#888888")
        t.append("  ")
        t.append(state, style=f"bold {sc}")
        t.append("\n")
        # Row 2: goal (truncated)
        goal = s.get("goal", "")
        short = goal[:22] + ("…" if len(goal) > 22 else "")
        t.append(short, style="#aaaaaa" if sel else "#666666")
        t.append("\n")
        # Row 3: cycles + interval
        t.append(f"{s.get('iterations', 0)} cy", style="#554433" if not sel else "#887755")
        t.append("  ")
        t.append(f"{s.get('interval_seconds', '?')}s", style="#444444")
        return t


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# DETAIL HEADER
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class DetailHeader(Static):
    """Agent summary pane above the event stream."""

    DEFAULT_CSS = """
    DetailHeader {
        width: 100%;
        height: auto;
        min-height: 7;
        padding: 1 2;
        border-bottom: solid #1a1a1a;
    }
    """

    _STATE_FG = {
        "running": "#55bb55", "paused": "#bbbb55",
        "done":    "#5599aa", "failed": "#bb5555",
    }

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._status: dict = {}
        self._countdown: str = "—"

    def update_status(self, status: dict) -> None:
        self._status = status
        self.refresh()

    def update_countdown(self, text: str) -> None:
        self._countdown = text
        self.refresh()

    def render(self) -> RenderableType:
        if not self._status or "error" in self._status:
            t = Text()
            t.append("\n  Select an agent", style="#333333")
            t.append("\n  j / k  or click", style="#222222")
            return t

        s = self._status
        state = s.get("state", "unknown")
        sc = self._STATE_FG.get(state, "#666666")

        t = Text()
        # Line 1: id · model · provider
        t.append(f"#{s.get('id', '?'):02d}", style="#777777")
        t.append("  ")
        t.append(f"{s.get('brain_model', '?')}", style="#555555")
        t.append(" · ")
        t.append(f"{s.get('brain_provider', '?')}", style="#3a3a3a")
        t.append("\n")
        # Line 2: goal
        goal = s.get("goal", "")
        short_goal = goal[:80] + ("…" if len(goal) > 80 else "")
        t.append(short_goal, style="#999999")
        t.append("\n")
        # Separator
        t.append("─" * 48, style="#1e1e1e")
        t.append("\n")
        # Meta row: cycles · interval · next tick
        t.append("cycles ", style="#3a3a3a")
        t.append(f"{s.get('iterations', 0)}", style=f"bold {sc}")
        t.append("   interval ", style="#3a3a3a")
        t.append(f"{s.get('interval_seconds', 0)}s", style="#555555")
        t.append("   next ", style="#3a3a3a")
        t.append(self._countdown, style="#666666")
        return t


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# EVENT ROW + EVENT STREAM
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def _extract_event_content(raw) -> str:
    if isinstance(raw, str):
        return raw
    if isinstance(raw, dict):
        if "summary" in raw and raw["summary"]:
            status = raw.get("status", "")
            summary = raw["summary"]
            return f"[{status}] {summary}" if status else summary
        if "command" in raw:
            cmd = raw["command"].replace("```bash", "").replace("```", "").strip()
            return f"$ {cmd}"
        for key in ("decision", "reasoning", "action", "output"):
            if key in raw and raw[key]:
                return str(raw[key])
        return str(list(raw.keys()))
    return str(raw)


_EV_COLORS: dict[str, str] = {
    "cycle":      "#7bb3f5",
    "checkpoint": "#d4a76a",
    "started":    "#7bb3f5",
    "paused":     "#d4d46a",
    "done":       "#6ad4d4",
    "failed":     "#d46a6a",
    "sense":      "#7bb3f5",
    "think":      "#d4a76a",
    "act":        "#80cc80",
    "learn":      "#b08ad4",
}


class EventRow(Static, can_focus=True):
    """One event in the stream — click to expand."""

    expanded = reactive(False)

    DEFAULT_CSS = """
    EventRow {
        height: 1;
        padding: 0 0;
    }
    EventRow.expanded {
        height: auto;
        min-height: 4;
        padding-bottom: 1;
        border-bottom: solid #181818;
    }
    EventRow:hover { background: #111111; }
    EventRow:focus { background: #111111; }
    """

    def __init__(self, event: dict, **kwargs):
        super().__init__(**kwargs)
        self._event = event

    def on_click(self) -> None:
        self.expanded = not self.expanded

    def watch_expanded(self, expanded: bool) -> None:
        self.styles.height = "auto" if expanded else 1
        self.set_class(expanded, "expanded")

    def render(self) -> Text:
        e = self._event
        event_type  = e.get("event_type", "unknown")
        timestamp   = e.get("timestamp", 0)
        duration_ms = e.get("duration_ms", 0)
        raw_content = e.get("content", "")
        fg  = _EV_COLORS.get(event_type, "#555555")
        tag = event_type[:5]

        time_short = time_long = ""
        if timestamp:
            try:
                dt = datetime.fromtimestamp(timestamp / 1000)
                time_short = dt.strftime("%H:%M")
                time_long  = dt.strftime("%H:%M:%S")
            except Exception:
                pass

        t = Text()

        if not self.expanded:
            content = _extract_event_content(raw_content)
            if len(content) > 68:
                content = content[:68] + "…"
            t.append(f"{time_short}  ", style="#383838")
            t.append(f"{tag}  ", style=f"bold {fg}")
            t.append(content, style="#666666")
        else:
            # Header
            t.append(f"{time_long}  ", style="#555555")
            t.append(event_type, style=f"bold {fg}")
            if duration_ms:
                t.append(f"  {duration_ms}ms", style="#444444")
            t.append("\n")
            t.append("─" * 50, style="#1e1e1e")
            t.append("\n")
            # Full content
            if isinstance(raw_content, dict):
                try:
                    lines = _json.dumps(raw_content, indent=2).split("\n")
                    for line in lines[:30]:
                        t.append(line + "\n", style="#555555")
                    if len(lines) > 30:
                        t.append(f"  … ({len(lines) - 30} more lines)\n", style="#333333")
                except Exception:
                    t.append(str(raw_content) + "\n", style="#555555")
            else:
                full = str(raw_content)
                for i in range(0, min(len(full), 700), 70):
                    t.append(full[i:i + 70] + "\n", style="#777777")
                if len(full) > 700:
                    t.append("…\n", style="#333333")
            t.append("[dim]click to collapse[/dim]", style="#222222")

        return t


class EventStream(ScrollableContainer):
    """Live event feed with clickable/expandable rows."""

    DEFAULT_CSS = """
    EventStream {
        height: 1fr;
        background: #080808;
        padding: 0 1;
        overflow-y: auto;
        scrollbar-size: 0 0;
    }
    """

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._current_id: int | None = None
        self._seen_count: int = 0
        self._seen_timestamps: set = set()

    def set_sadhana(self, sadhana_id: int | None) -> None:
        if sadhana_id != self._current_id:
            self._current_id = sadhana_id
            self._seen_count = 0
            self._seen_timestamps = set()
            for row in list(self.query(EventRow)):
                row.remove()

    def push_event(self, event: dict) -> None:
        ts = event.get("timestamp", 0)
        if ts and ts in self._seen_timestamps:
            return
        if ts:
            self._seen_timestamps.add(ts)
        self._add_row(event)

    def refresh_events(self, client: ChittaClient) -> None:
        if self._current_id is None:
            return
        history_limit = max(50, self._seen_count + 50)
        try:
            status = client.sadhana_status(self._current_id, history_limit=history_limit)
        except Exception:
            return
        if "error" in status:
            return
        history = status.get("history", [])
        new_count = len(history) - self._seen_count
        if new_count > 0:
            new_events = list(reversed(history[:new_count]))
            for event in new_events:
                ts = event.get("timestamp", 0)
                if ts not in self._seen_timestamps:
                    if ts:
                        self._seen_timestamps.add(ts)
                    self._add_row(event)
            self._seen_count = len(history)

    def _add_row(self, event: dict) -> None:
        self.mount(EventRow(event))
        # Cap at 100 rows to avoid memory growth
        rows = list(self.query(EventRow))
        if len(rows) > 100:
            rows[0].remove()
        self.scroll_end(animate=False)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# STATUS BAR
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class StatusBar(Horizontal):
    """Bottom vim-style status line."""

    DEFAULT_CSS = """
    StatusBar {
        height: 1;
        dock: bottom;
        background: #1a2240;
        padding: 0 1;
    }
    StatusBar > #sb-left  { width: auto; content-align: left middle; }
    StatusBar > #sb-space { width: 1fr; }
    StatusBar > #sb-right { width: auto; content-align: right middle; }
    """

    def compose(self) -> ComposeResult:
        yield Static("NORMAL", id="sb-left")
        yield Static("", id="sb-space")
        yield Static("", id="sb-right")

    def set_info(self, mode: str, agent: str, counts: str) -> None:
        self.query_one("#sb-left", Static).update(
            f"[bold #c5c8f5]{mode}[/]  [#777777]{agent}[/]"
        )
        self.query_one("#sb-right", Static).update(f"[#445566]{counts}[/]")


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# KEYBIND BAR
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

_KEYS = [
    ("n", "new"), ("p", "pause"), ("r", "resume"), ("s", "stop"),
    ("j/k", "nav"), ("enter", "summary"), ("/", "search"), ("q", "quit"),
]


class KeybindBar(Static):
    """Bottom keybinding hint bar."""

    DEFAULT_CSS = """
    KeybindBar {
        height: 1;
        dock: bottom;
        background: #0f0f0f;
        padding: 0 1;
    }
    """

    def render(self) -> Text:
        t = Text()
        for key, label in _KEYS:
            t.append(f" {key}", style="#999999 on #222222")
            t.append(f" {label} ", style="#555555")
        return t


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# NEW AGENT MODAL
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class NewAgentModal(ModalScreen[dict | None]):
    """Modal for creating agents."""

    BINDINGS = [("escape", "cancel", "Cancel")]

    DEFAULT_CSS = """
    NewAgentModal {
        align: center middle;
        background: rgba(0, 0, 0, 0.8);
    }
    #modal-box {
        width: 55;
        height: auto;
        background: #111111;
        border: solid #333333;
        padding: 1 2;
    }
    #modal-header { color: #888888; padding-bottom: 1; }
    .field-row    { height: auto; margin-bottom: 1; }
    .field-label  { color: #555555; padding-bottom: 0; }
    Input         { background: #0a0a0a; border: solid #222222; padding: 0 1; }
    Input:focus   { border: solid #444444; }
    Select        { background: #0a0a0a; border: solid #222222; }
    #btn-row      { height: auto; align: right middle; padding-top: 1; }
    Button        { margin-left: 1; min-width: 10; background: #222222; color: #888888; border: none; }
    Button:hover  { background: #333333; color: #aaaaaa; }
    Button#create { background: #333333; color: #aaaaaa; }
    """

    def compose(self) -> ComposeResult:
        with Container(id="modal-box"):
            yield Label("new agent", id="modal-header")
            with Vertical(classes="field-row"):
                yield Label("goal", classes="field-label")
                yield Input(placeholder="what should this agent do?", id="goal")
            with Horizontal():
                with Vertical(classes="field-row"):
                    yield Label("brain", classes="field-label")
                    yield Select([("claude", "claude"), ("opencode", "opencode")], value="claude", id="brain")
                with Vertical(classes="field-row"):
                    yield Label("model", classes="field-label")
                    yield Select([("haiku", "haiku"), ("sonnet", "sonnet"), ("opus", "opus")], value="haiku", id="model")
                with Vertical(classes="field-row"):
                    yield Label("interval", classes="field-label")
                    yield Input(value="300", id="interval")
            with Horizontal(id="btn-row"):
                yield Button("cancel", id="cancel")
                yield Button("create", id="create")

    def on_button_pressed(self, event) -> None:
        if event.button.id == "cancel":
            self.dismiss(None)
        elif event.button.id == "create":
            goal = self.query_one("#goal", Input).value
            if not goal:
                self.notify("goal required", severity="error")
                return
            try:
                interval = int(self.query_one("#interval", Input).value)
            except ValueError:
                self.notify("invalid interval", severity="error")
                return
            self.dismiss({
                "goal": goal,
                "brain": self.query_one("#brain", Select).value,
                "model": self.query_one("#model", Select).value,
                "interval": interval,
            })

    def action_cancel(self) -> None:
        self.dismiss(None)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# SUMMARY MODAL
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class SummaryModal(ModalScreen[None]):
    """Modal showing sadhana summary with learnings."""

    BINDINGS = [("escape", "close", "Close"), ("enter", "close", "Close")]

    DEFAULT_CSS = """
    SummaryModal {
        align: center middle;
        background: rgba(0, 0, 0, 0.85);
    }
    #summary-box {
        width: 70;
        height: auto;
        max-height: 80%;
        background: #111111;
        border: solid #333333;
        padding: 1 2;
        layout: vertical;
    }
    #summary-header  { color: #888888; padding-bottom: 1; height: auto; }
    #summary-content { height: 1fr; max-height: 40; overflow-y: auto; padding: 1 0; }
    .summary-section { margin-bottom: 1; }
    .summary-label   { color: #666666; }
    .summary-value   { color: #aaaaaa; }
    #summary-learnings { background: #0a0a0a; padding: 1; margin-top: 1; }
    #footer-row {
        height: 3; width: 100%; layout: horizontal;
        align: center middle; margin-top: 1; padding: 0 1; dock: bottom;
    }
    #keys-hint  { color: #555555; width: 1fr; }
    #close-btn  {
        width: auto; min-width: 12; background: #333333;
        color: #cccccc; border: solid #444444;
    }
    #close-btn:hover  { background: #444444; color: #ffffff; }
    #close-btn:focus  { background: #555555; }
    """

    def __init__(self, sadhana: dict, history: list, **kwargs):
        super().__init__(**kwargs)
        self.sadhana = sadhana
        self.history = history

    def compose(self) -> ComposeResult:
        s = self.sadhana
        with Container(id="summary-box"):
            yield Label(f"#{s.get('id', '?')} summary", id="summary-header")
            with ScrollableContainer(id="summary-content"):
                yield Static(f"[#666666]goal[/] [#aaaaaa]{s.get('goal', '')}[/]", classes="summary-section")
                state = s.get("state", "unknown")
                state_color = {"done": "#66aa66", "failed": "#aa6666"}.get(state, "#888888")
                yield Static(
                    f"[#666666]state[/] [{state_color}]{state}[/]  "
                    f"[#666666]cycles[/] [#aa8866]{s.get('iterations', 0)}[/]  "
                    f"[#666666]brain[/] [#888888]{s.get('brain_model', '?')}[/]",
                    classes="summary-section"
                )
                learnings = self._extract_learnings()
                if learnings:
                    yield Static("[#666666]learnings[/]", classes="summary-label")
                    yield Static(learnings, id="summary-learnings")
            with Horizontal(id="footer-row"):
                yield Static("[#555555]esc[/] [#444444]or[/] [#555555]enter[/]", id="keys-hint")
                yield Button("close", id="close-btn")

    def _extract_learnings(self) -> str:
        learnings = []
        for event in self.history:
            event_type = event.get("event_type", "")
            content = event.get("content", {})
            if event_type in ("cycle", "checkpoint") and isinstance(content, dict):
                summary = content.get("summary", "")
                status = content.get("status", "progressed")
                if summary:
                    if status == "achieved":
                        learnings.append(f"[#66aaaa]◆[/] {summary[:80]}")
                    elif status == "blocked":
                        learnings.append(f"[#aaaa66]◑[/] {summary[:80]}")
                    else:
                        learnings.append(f"[#66aa66]→[/] {summary[:80]}")
            elif event_type == "learn" and isinstance(content, dict):
                outcome = content.get("outcome", "")
                context = content.get("context", "")
                error = content.get("error", "")
                if outcome == "success" and context:
                    learnings.append(f"[#66aa66]✓[/] {context[:60]}")
                elif outcome == "failure" and error:
                    learnings.append(f"[#aa6666]✗[/] {error[:60]}")
            elif event_type == "think" and isinstance(content, dict):
                if content.get("goal_achieved"):
                    analysis = content.get("analysis", "")
                    if analysis:
                        learnings.append(f"[#6688aa]◆[/] {analysis[:80]}")
        return "\n".join(learnings[-10:]) if learnings else "[#444444]No cycle summaries yet[/]"

    def on_button_pressed(self, event) -> None:
        self.dismiss(None)

    def action_close(self) -> None:
        self.dismiss(None)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# MAIN APPLICATION
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class SadhanaApp(App):
    """Sadhana agent control interface."""

    TITLE = "sadhana-tui"

    BINDINGS = [
        Binding("q", "quit", "quit"),
        Binding("n", "new", "new"),
        Binding("p", "pause", "pause"),
        Binding("r", "resume", "resume"),
        Binding("s", "stop", "stop"),
        Binding("j", "next", "next", show=False),
        Binding("k", "prev", "prev", show=False),
        Binding("R", "refresh", "refresh", show=False),
        Binding("enter", "summary", "summary"),
        Binding("/", "focus_search", "search"),
        Binding("escape", "clear_search", "clear", show=False),
    ]

    CSS = """
    Screen { background: #0a0a0a; layout: vertical; }

    /* ── Header ──────────────────────────────── */
    #header {
        height: 2;
        background: #0a0a0a;
        layout: horizontal;
        padding: 0 1;
        border-bottom: solid #181818;
    }
    #brand            { width: auto; color: #444444; content-align: left middle; }
    #filter-indicator { width: auto; color: #aa8866; margin-left: 2; content-align: left middle; }
    #header-status    { width: 1fr; content-align: right middle; }

    /* ── Body ────────────────────────────────── */
    #body { height: 1fr; layout: horizontal; }

    /* ── Sidebar ─────────────────────────────── */
    #sidebar {
        width: 26;
        height: 100%;
        background: #0d0d0d;
        border-right: solid #181818;
        overflow-y: auto;
        scrollbar-size: 0 0;
    }
    #sidebar-title {
        height: 2;
        padding: 0 1;
        color: #333333;
        content-align: left middle;
        border-bottom: solid #181818;
    }

    /* ── Main panel ──────────────────────────── */
    #main-panel { width: 1fr; height: 100%; layout: vertical; }
    #stream-label {
        height: 1;
        padding: 0 2;
        color: #2a2a2a;
        content-align: left middle;
        border-bottom: solid #141414;
    }

    /* ── Search bar ──────────────────────────── */
    #search-bar {
        height: 1;
        dock: bottom;
        background: #111111;
        layout: horizontal;
        padding: 0 1;
        display: none;
    }
    #search-bar.visible { display: block; }
    #search-prompt { width: auto; color: #aa8866; }
    #search-input  { width: 1fr; height: 1; background: #111111; border: none; padding: 0; }
    #search-input:focus { background: #111111; }
    """

    def __init__(self):
        super().__init__()
        self.client = ChittaClient()
        self._sadhanas: list[dict] = []
        self._filtered: list[dict] = []
        self._selected_idx: int = 0
        self._filter_text: str = ""
        self._detail_status: dict = {}

    def compose(self) -> ComposeResult:
        with Horizontal(id="header"):
            yield Static("sadhana", id="brand")
            yield Static("", id="filter-indicator")
            yield Static("", id="header-status")

        with Horizontal(id="body"):
            with Vertical(id="sidebar"):
                yield Static("Agents", id="sidebar-title")
            with Vertical(id="main-panel"):
                yield DetailHeader(id="detail-header")
                yield Static("Event Stream", id="stream-label")
                yield EventStream(id="events")

        # Search bar (hidden, docked bottom — appears above status bar)
        with Horizontal(id="search-bar"):
            yield Static("/", id="search-prompt")
            yield Input(id="search-input")

        # StatusBar above KeybindBar (last-yielded = lowest)
        yield StatusBar()
        yield KeybindBar()

    def on_mount(self) -> None:
        self.refresh_all()
        self.set_interval(2.0, self.refresh_list)
        self.set_interval(1.0, self._tick)
        self.run_worker(self._event_stream_worker, thread=True)
        self.set_timer(0.1, self._focus_first_row)

    # ── Timers ──────────────────────────────────────────────────────────────

    def _tick(self) -> None:
        """1-second tick: refresh events + countdown."""
        self.refresh_events()
        self._update_countdown()

    def _update_countdown(self) -> None:
        s = self._detail_status
        if not s or "error" in s:
            return
        interval_ms = s.get("interval_seconds", 0) * 1000
        if not interval_ms:
            return
        history = s.get("history", [])
        last_ts = history[0].get("timestamp", 0) if history else 0
        if not last_ts:
            return
        now_ms = int(_time.time() * 1000)
        remaining_ms = last_ts + interval_ms - now_ms
        if remaining_ms <= 0:
            text = "now"
        else:
            secs = remaining_ms // 1000
            text = f"{secs // 60}:{secs % 60:02d}"
        try:
            self.query_one("#detail-header", DetailHeader).update_countdown(text)
        except Exception:
            pass

    # ── Push stream worker ──────────────────────────────────────────────────

    def _event_stream_worker(self) -> None:
        import time
        while self.is_running:
            try:
                for line in self.client.watch_events(sadhana_id=0):
                    if not self.is_running:
                        return
                    if line is None:
                        continue
                    try:
                        data = _json.loads(line)
                        if "jsonrpc" in data:
                            continue
                        self.call_from_thread(self._on_stream_event, data)
                    except Exception:
                        pass
            except Exception:
                pass
            if self.is_running:
                time.sleep(2.0)

    def _on_stream_event(self, event: dict) -> None:
        sadhana_id = event.get("sadhana_id")
        event_type = event.get("event_type", "")
        if self._filtered and self._selected_idx < len(self._filtered):
            selected_id = self._filtered[self._selected_idx].get("id")
            if sadhana_id == selected_id:
                try:
                    self.query_one("#events", EventStream).push_event(event)
                except Exception:
                    pass
        if event_type in ("started", "done", "failed", "paused", "resumed"):
            self.refresh_list()

    # ── Focus ───────────────────────────────────────────────────────────────

    def _focus_first_row(self) -> None:
        rows = list(self.query(AgentRow))
        if rows:
            rows[0].focus()

    # ── Selection ───────────────────────────────────────────────────────────

    def on_agent_row_clicked(self, event: AgentRow.Clicked) -> None:
        self._selected_idx = event.index
        self._update_selection()
        self.refresh_events()

    def _update_selection(self) -> None:
        for i, row in enumerate(self.query(AgentRow)):
            row.selected = (i == self._selected_idx)

    # ── Search / filter ─────────────────────────────────────────────────────

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search-input":
            self._filter_text = event.value.lower().strip()
            self._apply_filter(reset_selection=False)
            self._update_filter_indicator()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id == "search-input":
            self._hide_search_bar()

    def _show_search_bar(self) -> None:
        bar = self.query_one("#search-bar")
        bar.add_class("visible")
        inp = self.query_one("#search-input", Input)
        inp.value = self._filter_text
        inp.focus()

    def _hide_search_bar(self) -> None:
        self.query_one("#search-bar").remove_class("visible")
        rows = list(self.query(AgentRow))
        if rows and self._selected_idx < len(rows):
            rows[self._selected_idx].focus()

    def _update_filter_indicator(self) -> None:
        ind = self.query_one("#filter-indicator", Static)
        ind.update(f"/{self._filter_text}" if self._filter_text else "")

    def _fuzzy_match(self, needle: str, haystack: str, threshold: int = 2) -> bool:
        if needle in haystack:
            return True
        if len(needle) <= 2:
            return needle in haystack
        if len(needle) <= 4:
            it = iter(haystack)
            return all(c in it for c in needle)
        if abs(len(needle) - len(haystack)) > threshold:
            for i in range(max(0, len(haystack) - len(needle) - threshold)):
                window = haystack[i:i + len(needle) + threshold]
                if self._levenshtein(needle, window) <= threshold:
                    return True
            return False
        return self._levenshtein(needle, haystack) <= threshold

    def _levenshtein(self, s1: str, s2: str) -> int:
        if len(s1) < len(s2):
            return self._levenshtein(s2, s1)
        if len(s2) == 0:
            return len(s1)
        prev_row = range(len(s2) + 1)
        for i, c1 in enumerate(s1):
            curr_row = [i + 1]
            for j, c2 in enumerate(s2):
                curr_row.append(min(prev_row[j + 1] + 1, curr_row[j] + 1, prev_row[j] + (c1 != c2)))
            prev_row = curr_row
        return prev_row[-1]

    def _match_sadhana(self, sadhana: dict, terms: list[str]) -> bool:
        goal  = sadhana.get("goal", "").lower()
        state = sadhana.get("state", "").lower()
        sid   = str(sadhana.get("id", ""))
        for term in terms:
            if term.startswith("state:"):
                if term[6:] not in state:
                    return False
            elif term.startswith("id:"):
                if term[3:] != sid:
                    return False
            elif term.startswith("goal:"):
                if not self._fuzzy_match(term[5:], goal):
                    return False
            else:
                if not (self._fuzzy_match(term, goal) or
                        self._fuzzy_match(term, state) or
                        term in sid):
                    return False
        return True

    def _apply_filter(self, reset_selection: bool = True) -> None:
        if self._filter_text:
            terms = [t for t in self._filter_text.split() if t]
            self._filtered = [s for s in self._sadhanas if self._match_sadhana(s, terms)]
        else:
            self._filtered = self._sadhanas
        if reset_selection:
            self._selected_idx = 0
        elif self._selected_idx >= len(self._filtered):
            self._selected_idx = max(0, len(self._filtered) - 1)
        self._rebuild_rows()

    def _rebuild_rows(self) -> None:
        sidebar = self.query_one("#sidebar", Vertical)
        existing = {row.sadhana_id: row for row in self.query(AgentRow)}
        new_ids = {s.get("id") for s in self._filtered}

        for sid, row in existing.items():
            if sid not in new_ids:
                row.remove()

        for i, s in enumerate(self._filtered):
            sid = s.get("id")
            if sid in existing:
                row = existing[sid]
                row.update_data(s, i)
                row.selected = (i == self._selected_idx)
            else:
                row = AgentRow(s, index=i)
                row.selected = (i == self._selected_idx)
                sidebar.mount(row)

        sidebar.refresh(layout=True)

    # ── Data refresh ────────────────────────────────────────────────────────

    def refresh_all(self) -> None:
        self.refresh_list()
        self.refresh_events()

    def refresh_list(self) -> None:
        try:
            new_sadhanas = self.client.sadhana_list()
        except Exception:
            self.client.close()
            return

        if not new_sadhanas and self._sadhanas:
            return

        self._sadhanas = new_sadhanas
        self._apply_filter(reset_selection=False)

        total   = len(self._sadhanas)
        running = sum(1 for s in self._sadhanas if s.get("state") == "running")
        done    = sum(1 for s in self._sadhanas if s.get("state") == "done")

        try:
            health = self.client.health_check()
            dot = "[#55bb55]●[/]" if "error" not in health else "[#bb5555]●[/]"
        except Exception:
            dot = "[#bb5555]●[/]"

        try:
            self.query_one("#header-status", Static).update(
                f"[#335533]{running} running[/] [#444444]{total} total[/]  {dot}"
            )
        except Exception:
            pass

        # Update sidebar title
        try:
            self.query_one("#sidebar-title", Static).update(
                f"Agents  [#444444]{total}[/]"
            )
        except Exception:
            pass

        self._update_status_bar()

    def refresh_events(self) -> None:
        if not self._filtered or self._selected_idx >= len(self._filtered):
            return
        selected    = self._filtered[self._selected_idx]
        sadhana_id  = selected.get("id")
        try:
            status = self.client.sadhana_status(sadhana_id, history_limit=0)
            self._detail_status = status
            self.query_one("#detail-header", DetailHeader).update_status(status)
            events = self.query_one("#events", EventStream)
            events.set_sadhana(sadhana_id)
            events.refresh_events(self.client)
        except Exception:
            pass
        self._update_status_bar()

    def _update_status_bar(self) -> None:
        total   = len(self._sadhanas)
        running = sum(1 for s in self._sadhanas if s.get("state") == "running")
        if self._filtered and self._selected_idx < len(self._filtered):
            sel   = self._filtered[self._selected_idx]
            agent = f"#{sel.get('id', '?')} · {sel.get('state', '?')}"
        else:
            agent = "—"
        counts = f"{total} agents  {running} active"
        try:
            self.query_one(StatusBar).set_info("NORMAL", agent, counts)
        except Exception:
            pass

    # ── Actions ─────────────────────────────────────────────────────────────

    def action_next(self) -> None:
        if self._filtered:
            self._selected_idx = (self._selected_idx + 1) % len(self._filtered)
            self._update_selection()
            self.refresh_events()

    def action_prev(self) -> None:
        if self._filtered:
            self._selected_idx = (self._selected_idx - 1) % len(self._filtered)
            self._update_selection()
            self.refresh_events()

    def action_new(self) -> None:
        def on_result(result: dict | None) -> None:
            if result:
                resp = self.client.sadhana_start(**result)
                if "error" in resp:
                    self.notify(f"error: {resp['error']}", severity="error")
                else:
                    self.notify(f"agent #{resp.get('id', '?')} created")
                    self.refresh_list()
        self.push_screen(NewAgentModal(), on_result)

    def action_pause(self) -> None:
        if self._filtered and self._selected_idx < len(self._filtered):
            sid = self._filtered[self._selected_idx].get("id")
            if "error" not in self.client.sadhana_pause(sid):
                self.notify(f"#{sid} paused")
                self.refresh_list()

    def action_resume(self) -> None:
        if self._filtered and self._selected_idx < len(self._filtered):
            sid = self._filtered[self._selected_idx].get("id")
            if "error" not in self.client.sadhana_resume(sid):
                self.notify(f"#{sid} resumed")
                self.refresh_list()

    def action_stop(self) -> None:
        if self._filtered and self._selected_idx < len(self._filtered):
            sid = self._filtered[self._selected_idx].get("id")
            if "error" not in self.client.sadhana_stop(sid):
                self.notify(f"#{sid} stopped")
                self.refresh_list()

    def action_refresh(self) -> None:
        self.refresh_all()
        self.notify("refreshed")

    def action_focus_search(self) -> None:
        self._show_search_bar()

    def action_clear_search(self) -> None:
        bar = self.query_one("#search-bar")
        inp = self.query_one("#search-input", Input)
        if bar.has_class("visible"):
            if inp.value:
                inp.value = ""
                self._filter_text = ""
                self._apply_filter(reset_selection=False)
                self._update_filter_indicator()
            else:
                self._hide_search_bar()
        elif self._filter_text:
            self._filter_text = ""
            self._apply_filter(reset_selection=False)
            self._update_filter_indicator()

    def action_summary(self) -> None:
        if not self._filtered or self._selected_idx >= len(self._filtered):
            return
        selected   = self._filtered[self._selected_idx]
        sadhana_id = selected.get("id")
        try:
            status = self.client.sadhana_status(sadhana_id, history_limit=50)
            if "error" not in status:
                self.push_screen(SummaryModal(status, status.get("history", [])))
        except Exception:
            self.notify("failed to load summary", severity="error")


def main():
    app = SadhanaApp()
    app.run()


if __name__ == "__main__":
    main()
