"""Sadhana TUI - Minimal control interface for autonomous agents."""

from textual.app import App, ComposeResult
from textual.widgets import Static, Input, Label, Select, RichLog, Button, Footer
from textual.containers import Container, Horizontal, Vertical, ScrollableContainer, HorizontalScroll
from textual.screen import ModalScreen
from textual.binding import Binding
from textual.reactive import reactive
from textual.message import Message
from rich.text import Text
from rich.console import RenderableType

from .client import ChittaClient


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# AGENT CARD - Clickable minimal cards
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class AgentCard(Static, can_focus=True):
    """Clickable agent card."""

    selected = reactive(False)

    class Clicked(Message):
        """Emitted when card is clicked."""
        def __init__(self, index: int) -> None:
            self.index = index
            super().__init__()

    DEFAULT_CSS = """
    AgentCard {
        width: auto;
        min-width: 28;
        height: 5;
        margin: 0 1 0 0;
        padding: 0 1;
        background: #111111;
        border: none;
        border-left: tall #333333;
    }
    AgentCard:hover {
        background: #1a1a1a;
    }
    AgentCard:focus {
        background: #1a1a1a;
    }
    AgentCard.selected {
        background: #181818;
        border-left: tall #555555;
    }
    AgentCard.running { border-left: tall #44aa44; }
    AgentCard.paused { border-left: tall #aaaa44; }
    AgentCard.done { border-left: tall #555555; }
    AgentCard.failed { border-left: tall #aa4444; }
    """

    def __init__(self, sadhana: dict, index: int, **kwargs):
        super().__init__(**kwargs)
        self.sadhana = sadhana
        self.index = index
        state = sadhana.get("state", "unknown")
        self.add_class(state)

    def watch_selected(self, selected: bool) -> None:
        self.set_class(selected, "selected")

    def on_click(self) -> None:
        self.post_message(self.Clicked(self.index))

    def render(self) -> Text:
        s = self.sadhana
        state = s.get("state", "unknown")

        dot = {"running": "●", "paused": "◑", "done": "○", "failed": "×"}.get(state, "·")
        state_color = {"running": "#66aa66", "paused": "#aaaa66", "done": "#666666", "failed": "#aa6666"}.get(state, "#444444")

        text = Text()
        text.append(f"{dot} ", style=state_color)
        text.append(f"#{s.get('id', '?'):02d}", style="#cccccc" if self.selected else "#888888")
        text.append(f" {state}", style=state_color)
        text.append("\n")

        goal = s.get("goal", "")[:22]
        text.append(goal, style="#999999" if self.selected else "#666666")
        if len(s.get("goal", "")) > 22:
            text.append("…", style="#444444")

        text.append("\n")
        text.append(f"{s.get('iterations', 0)}", style="#aa8866" if self.selected else "#666644")
        text.append(" cycles ", style="#444444")
        text.append(f"{s.get('brain_model', '?')}", style="#555555")

        return text


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# DETAIL PANEL
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class DetailPanel(Static):
    """Agent detail display."""

    DEFAULT_CSS = """
    DetailPanel {
        width: 100%;
        height: auto;
        min-height: 100%;
        padding: 1 1;
    }
    """

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._status: dict = {}

    def update_status(self, status: dict) -> None:
        self._status = status
        self.refresh()

    def render(self) -> RenderableType:
        if not self._status or "error" in self._status:
            text = Text()
            text.append("\n\n    ", style="")
            text.append("Select an agent", style="#444444")
            text.append("\n    ", style="")
            text.append("j/k or click", style="#333333")
            return text

        s = self._status
        state = s.get("state", "unknown")
        state_color = {"running": "#66aa66", "paused": "#aaaa66", "done": "#666666", "failed": "#aa6666"}.get(state, "#444444")

        text = Text()
        text.append(f"#{s.get('id', '?'):02d}", style="#888888")
        text.append(f"  {state}", style=state_color)
        text.append("\n")
        text.append("─" * 40, style="#222222")
        text.append("\n\n")

        goal = s.get("goal", "")
        text.append(goal, style="#aaaaaa")
        text.append("\n\n")

        # Stats
        text.append("model ", style="#555555")
        text.append(f"{s.get('brain_model', '?')}", style="#888888")
        text.append("   brain ", style="#555555")
        text.append(f"{s.get('brain_provider', '?')}", style="#888888")
        text.append("\n")

        text.append("cycles ", style="#555555")
        text.append(f"{s.get('iterations', 0)}", style="#aa8866")
        text.append("   interval ", style="#555555")
        text.append(f"{s.get('interval_seconds', 60)}s", style="#888888")

        return text


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# EVENT STREAM
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class EventStream(RichLog):
    """Live event feed."""

    DEFAULT_CSS = """
    EventStream {
        height: 1fr;
        background: #0a0a0a;
        border: none;
        padding: 0 1;
        scrollbar-size: 0 0;
    }
    """

    def __init__(self, **kwargs):
        super().__init__(highlight=True, markup=True, wrap=True, **kwargs)
        self._current_id: int | None = None
        self._seen_count: int = 0

    def set_sadhana(self, sadhana_id: int | None) -> None:
        if sadhana_id != self._current_id:
            self._current_id = sadhana_id
            self._seen_count = 0
            self.clear()

    def refresh_events(self, client: ChittaClient) -> None:
        if self._current_id is None:
            return

        try:
            status = client.sadhana_status(self._current_id, history_limit=30)
        except Exception:
            return  # Skip on connection error

        if "error" in status:
            return

        history = status.get("history", [])
        if len(history) > self._seen_count:
            # History comes newest-first, reverse to show chronologically (latest at bottom)
            new_events = list(reversed(history[self._seen_count:]))
            for event in new_events:
                self._write_event(event)
            self._seen_count = len(history)

    def _write_event(self, event: dict) -> None:
        event_type = event.get("event_type", "unknown")
        raw_content = event.get("content", "")
        timestamp = event.get("timestamp", 0)
        duration_ms = event.get("duration_ms", 0)

        colors = {
            "sense": "#6688aa",
            "think": "#aa8866",
            "act": "#66aa66",
            "learn": "#8866aa",
            "error": "#aa6666",
        }
        color = colors.get(event_type, "#555555")

        time_str = ""
        if timestamp:
            try:
                from datetime import datetime
                dt = datetime.fromtimestamp(timestamp / 1000)
                time_str = dt.strftime("%H:%M")
            except:
                pass

        content = self._extract_content(raw_content)
        if len(content) > 70:
            content = content[:70] + "…"

        dur = f" {duration_ms}ms" if duration_ms else ""

        self.write(f"[#333333]{time_str}[/] [{color}]{event_type[:5]}[/] [#777777]{content}[/][#444444]{dur}[/]")

    def _extract_content(self, raw) -> str:
        if isinstance(raw, str):
            return raw
        if isinstance(raw, dict):
            if "command" in raw:
                cmd = raw["command"].replace("```bash", "").replace("```", "").strip()
                return f"$ {cmd}"
            for key in ("decision", "reasoning", "action", "output"):
                if key in raw and raw[key]:
                    return str(raw[key])
            return str(list(raw.keys()))
        return str(raw)


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
    #modal-header {
        color: #888888;
        padding-bottom: 1;
    }
    .field-row {
        height: auto;
        margin-bottom: 1;
    }
    .field-label {
        color: #555555;
        padding-bottom: 0;
    }
    Input {
        background: #0a0a0a;
        border: solid #222222;
        padding: 0 1;
    }
    Input:focus {
        border: solid #444444;
    }
    Select {
        background: #0a0a0a;
        border: solid #222222;
    }
    #btn-row {
        height: auto;
        align: right middle;
        padding-top: 1;
    }
    Button {
        margin-left: 1;
        min-width: 10;
        background: #222222;
        color: #888888;
        border: none;
    }
    Button:hover {
        background: #333333;
        color: #aaaaaa;
    }
    Button#create {
        background: #333333;
        color: #aaaaaa;
    }
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
                    yield Input(value="60", id="interval")

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
    #summary-header {
        color: #888888;
        padding-bottom: 1;
        height: auto;
    }
    #summary-content {
        height: 1fr;
        max-height: 40;
        overflow-y: auto;
        padding: 1 0;
    }
    .summary-section {
        margin-bottom: 1;
    }
    .summary-label {
        color: #666666;
    }
    .summary-value {
        color: #aaaaaa;
    }
    #summary-learnings {
        background: #0a0a0a;
        padding: 1;
        margin-top: 1;
    }
    #footer-row {
        height: 3;
        width: 100%;
        layout: horizontal;
        align: center middle;
        margin-top: 1;
        padding: 0 1;
        dock: bottom;
    }
    #keys-hint {
        color: #555555;
        width: 1fr;
    }
    #close-btn {
        width: auto;
        min-width: 12;
        background: #333333;
        color: #cccccc;
        border: solid #444444;
    }
    #close-btn:hover {
        background: #444444;
        color: #ffffff;
    }
    #close-btn:focus {
        background: #555555;
    }
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
                # Goal
                yield Static(f"[#666666]goal[/] [#aaaaaa]{s.get('goal', '')}[/]", classes="summary-section")

                # Stats
                state = s.get("state", "unknown")
                state_color = {"done": "#66aa66", "failed": "#aa6666"}.get(state, "#888888")
                yield Static(
                    f"[#666666]state[/] [{state_color}]{state}[/]  "
                    f"[#666666]cycles[/] [#aa8866]{s.get('iterations', 0)}[/]  "
                    f"[#666666]brain[/] [#888888]{s.get('brain_model', '?')}[/]",
                    classes="summary-section"
                )

                # Learnings from history
                learnings = self._extract_learnings()
                if learnings:
                    yield Static("[#666666]learnings[/]", classes="summary-label")
                    yield Static(learnings, id="summary-learnings")

            with Horizontal(id="footer-row"):
                yield Static("[#555555]esc[/] [#444444]or[/] [#555555]enter[/]", id="keys-hint")
                yield Button("close", id="close-btn")

    def _extract_learnings(self) -> str:
        """Extract key learnings from history."""
        learnings = []

        for event in self.history:
            event_type = event.get("event_type", "")
            content = event.get("content", {})

            if event_type == "learn" and isinstance(content, dict):
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

        # Return last 10 learnings
        return "\n".join(learnings[-10:]) if learnings else "[#444444]No learnings recorded[/]"

    def on_button_pressed(self, event) -> None:
        self.dismiss(None)

    def action_close(self) -> None:
        self.dismiss(None)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# MAIN APPLICATION
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class SadhanaApp(App):
    """Minimal agent control interface."""

    TITLE = "sadhana"

    BINDINGS = [
        Binding("q", "quit", "quit"),
        Binding("n", "new", "new"),
        Binding("p", "pause", "pause"),
        Binding("r", "resume", "resume"),
        Binding("s", "stop", "stop"),
        Binding("j", "next", "next", show=False),
        Binding("k", "prev", "prev", show=False),
        Binding("R", "refresh", "refresh"),
        Binding("enter", "summary", "summary"),
        Binding("/", "focus_search", "search"),
        Binding("escape", "clear_search", "clear", show=False),
    ]

    CSS = """
    Screen {
        background: #0a0a0a;
        layout: vertical;
    }

    /* Header */
    #header {
        height: 2;
        background: #0a0a0a;
        layout: horizontal;
        padding: 0 1;
    }
    #brand {
        width: auto;
        color: #555555;
        content-align: left middle;
    }
    #filter-indicator {
        width: auto;
        color: #aa8866;
        margin-left: 2;
        content-align: left middle;
    }
    #status-bar {
        width: 1fr;
        content-align: right middle;
    }

    /* Vim-style search bar */
    #search-bar {
        height: 1;
        dock: bottom;
        background: #111111;
        padding: 0 1;
        display: none;
    }
    #search-bar.visible {
        display: block;
    }
    #search-prompt {
        width: auto;
        color: #aa8866;
    }
    #search-input {
        width: 1fr;
        height: 1;
        background: #111111;
        border: none;
        padding: 0;
    }
    #search-input:focus {
        background: #111111;
    }

    /* Agent row - fixed height, horizontal scroll only */
    #agent-row {
        height: 7;
        max-height: 7;
        background: #0a0a0a;
        padding: 1 0;
        border-bottom: solid #1a1a1a;
    }
    HorizontalScroll {
        height: 5;
        width: 100%;
        scrollbar-size: 0 0;
    }
    #agent-list {
        height: 5;
        width: auto;
        layout: horizontal;
    }

    /* Main - takes remaining space minus footer */
    #main {
        height: 1fr;
        layout: horizontal;
    }
    #detail-section {
        width: 40%;
        height: 100%;
        background: #0a0a0a;
        overflow-y: auto;
        scrollbar-size: 0 0;
        padding: 0;
    }
    #events-section {
        width: 60%;
        height: 100%;
        background: #0a0a0a;
        border-left: solid #1a1a1a;
        overflow-y: auto;
        scrollbar-size: 0 0;
    }
    #events-header {
        height: 2;
        padding: 0 1;
        color: #333333;
        content-align: left middle;
    }

    /* Footer */
    Footer {
        height: 1;
        dock: bottom;
        background: #111111;
        color: #666666;
    }
    FooterKey {
        background: #222222;
        color: #888888;
    }
    FooterKey > .footer-key--key {
        background: #333333;
        color: #aaaaaa;
    }
    FooterKey > .footer-key--description {
        color: #666666;
    }
    """

    def __init__(self):
        super().__init__()
        self.client = ChittaClient()
        self._sadhanas: list[dict] = []
        self._filtered: list[dict] = []
        self._selected_idx: int = 0
        self._filter_text: str = ""

    def compose(self) -> ComposeResult:
        with Horizontal(id="header"):
            yield Static("sadhana", id="brand")
            yield Static("", id="filter-indicator")
            yield Static("", id="status-bar")

        with Container(id="agent-row"):
            with HorizontalScroll():
                yield Horizontal(id="agent-list")

        with Horizontal(id="main"):
            with ScrollableContainer(id="detail-section"):
                yield DetailPanel(id="detail")
            with Vertical(id="events-section"):
                yield Static("events", id="events-header")
                yield EventStream(id="events")

        # Vim-style search bar (hidden by default)
        with Horizontal(id="search-bar"):
            yield Static("/", id="search-prompt")
            yield Input(id="search-input")

        yield Footer()

    def on_mount(self) -> None:
        self.refresh_all()
        self.set_interval(2.0, self.refresh_list)
        self.set_interval(1.0, self.refresh_events)
        # Focus first card on start
        self.set_timer(0.1, self._focus_first_card)

    def _focus_first_card(self) -> None:
        """Focus first agent card after mount."""
        cards = list(self.query(AgentCard))
        if cards:
            cards[0].focus()

    def on_agent_card_clicked(self, event: AgentCard.Clicked) -> None:
        """Handle card click."""
        self._selected_idx = event.index
        self._update_selection()
        self.refresh_events()

    def on_input_changed(self, event: Input.Changed) -> None:
        """Handle search input changes - live filtering."""
        if event.input.id == "search-input":
            self._filter_text = event.value.lower().strip()
            self._apply_filter(reset_selection=False)
            self._update_filter_indicator()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Handle Enter in search - hide search bar."""
        if event.input.id == "search-input":
            self._hide_search_bar()

    def _show_search_bar(self) -> None:
        """Show vim-style search bar."""
        search_bar = self.query_one("#search-bar")
        search_bar.add_class("visible")
        search_input = self.query_one("#search-input", Input)
        search_input.value = self._filter_text
        search_input.focus()

    def _hide_search_bar(self) -> None:
        """Hide search bar."""
        search_bar = self.query_one("#search-bar")
        search_bar.remove_class("visible")
        # Focus first card
        cards = list(self.query(AgentCard))
        if cards and self._selected_idx < len(cards):
            cards[self._selected_idx].focus()

    def _update_filter_indicator(self) -> None:
        """Update filter indicator in header."""
        indicator = self.query_one("#filter-indicator", Static)
        if self._filter_text:
            indicator.update(f"/{self._filter_text}")
        else:
            indicator.update("")

    def _fuzzy_match(self, needle: str, haystack: str, threshold: int = 2) -> bool:
        """Fuzzy match with Levenshtein distance threshold."""
        if needle in haystack:
            return True
        if len(needle) <= 2:
            return needle in haystack
        # Check if needle is subsequence (for short queries)
        if len(needle) <= 4:
            it = iter(haystack)
            return all(c in it for c in needle)
        # Simple Levenshtein for longer terms
        if abs(len(needle) - len(haystack)) > threshold:
            # Check substring windows
            for i in range(max(0, len(haystack) - len(needle) - threshold)):
                window = haystack[i:i + len(needle) + threshold]
                if self._levenshtein(needle, window) <= threshold:
                    return True
            return False
        return self._levenshtein(needle, haystack) <= threshold

    def _levenshtein(self, s1: str, s2: str) -> int:
        """Calculate Levenshtein distance."""
        if len(s1) < len(s2):
            return self._levenshtein(s2, s1)
        if len(s2) == 0:
            return len(s1)
        prev_row = range(len(s2) + 1)
        for i, c1 in enumerate(s1):
            curr_row = [i + 1]
            for j, c2 in enumerate(s2):
                insertions = prev_row[j + 1] + 1
                deletions = curr_row[j] + 1
                substitutions = prev_row[j] + (c1 != c2)
                curr_row.append(min(insertions, deletions, substitutions))
            prev_row = curr_row
        return prev_row[-1]

    def _match_sadhana(self, sadhana: dict, terms: list[str]) -> bool:
        """Check if sadhana matches all search terms."""
        goal = sadhana.get("goal", "").lower()
        state = sadhana.get("state", "").lower()
        sid = str(sadhana.get("id", ""))

        for term in terms:
            # Field-specific filters
            if term.startswith("state:"):
                val = term[6:]
                if val not in state:
                    return False
            elif term.startswith("id:"):
                val = term[3:]
                if val != sid:
                    return False
            elif term.startswith("goal:"):
                val = term[5:]
                if not self._fuzzy_match(val, goal):
                    return False
            else:
                # General search: fuzzy match against any field
                if not (self._fuzzy_match(term, goal) or
                        self._fuzzy_match(term, state) or
                        term in sid):
                    return False
        return True

    def _apply_filter(self, reset_selection: bool = True) -> None:
        """Apply current filter and refresh cards."""
        if self._filter_text:
            # Split into terms (space-separated, AND logic)
            terms = [t for t in self._filter_text.split() if t]
            self._filtered = [s for s in self._sadhanas if self._match_sadhana(s, terms)]
        else:
            self._filtered = self._sadhanas

        if reset_selection:
            self._selected_idx = 0
        elif self._selected_idx >= len(self._filtered):
            self._selected_idx = max(0, len(self._filtered) - 1)
        self._rebuild_cards()

    def _rebuild_cards(self) -> None:
        """Rebuild card list from filtered sadhanas."""
        agent_list = self.query_one("#agent-list", Horizontal)

        # Remove all existing cards
        for card in list(self.query(AgentCard)):
            card.remove()

        # Add filtered cards
        for i, s in enumerate(self._filtered):
            card = AgentCard(s, index=i)
            card.selected = (i == self._selected_idx)
            agent_list.mount(card)

        # Force container refresh
        agent_list.refresh(layout=True)
        self.query_one(HorizontalScroll).refresh(layout=True)

    def refresh_all(self) -> None:
        self.refresh_list()
        self.refresh_events()

    def refresh_list(self) -> None:
        try:
            new_sadhanas = self.client.sadhana_list()
        except Exception:
            return  # Skip on connection error

        self._sadhanas = new_sadhanas
        self._apply_filter(reset_selection=False)

        total = len(self._sadhanas)
        running = sum(1 for s in self._sadhanas if s.get("state") == "running")

        try:
            health = self.client.health_check()
            dot = "[#66aa66]●[/]" if "error" not in health else "[#aa6666]●[/]"
        except Exception:
            dot = "[#aa6666]●[/]"

        status = self.query_one("#status-bar", Static)
        filtered_count = len(self._filtered)
        if self._filter_text:
            status.update(f"[#444444]{filtered_count}[/][#333333]/[/][#444444]{total}[/] {dot}")
        else:
            status.update(f"[#444444]{running}[/][#333333]/[/][#444444]{total}[/] {dot}")

    def refresh_events(self) -> None:
        if not self._filtered or self._selected_idx >= len(self._filtered):
            return

        selected = self._filtered[self._selected_idx]
        sadhana_id = selected.get("id")

        try:
            detail = self.query_one("#detail", DetailPanel)
            detail.update_status(self.client.sadhana_status(sadhana_id, history_limit=0))

            events = self.query_one("#events", EventStream)
            events.set_sadhana(sadhana_id)
            events.refresh_events(self.client)
        except Exception:
            pass  # Skip on connection error

    def _update_selection(self) -> None:
        for i, card in enumerate(self.query(AgentCard)):
            card.selected = (i == self._selected_idx)

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
        """Manual refresh of all data."""
        self.refresh_all()
        self.notify("refreshed")

    def action_focus_search(self) -> None:
        """Show vim-style search bar."""
        self._show_search_bar()

    def action_clear_search(self) -> None:
        """Clear search or hide search bar."""
        search_bar = self.query_one("#search-bar")
        search_input = self.query_one("#search-input", Input)

        if search_bar.has_class("visible"):
            if search_input.value:
                # First escape: clear input
                search_input.value = ""
                self._filter_text = ""
                self._apply_filter(reset_selection=False)
                self._update_filter_indicator()
            else:
                # Second escape: hide bar
                self._hide_search_bar()
        elif self._filter_text:
            # Not in search mode but have filter - clear it
            self._filter_text = ""
            self._apply_filter(reset_selection=False)
            self._update_filter_indicator()

    def action_summary(self) -> None:
        """Show summary modal for selected sadhana."""
        if not self._filtered or self._selected_idx >= len(self._filtered):
            return

        selected = self._filtered[self._selected_idx]
        sadhana_id = selected.get("id")

        try:
            status = self.client.sadhana_status(sadhana_id, history_limit=50)
            if "error" not in status:
                history = status.get("history", [])
                self.push_screen(SummaryModal(status, history))
        except Exception:
            self.notify("failed to load summary", severity="error")


def main():
    app = SadhanaApp()
    app.run()


if __name__ == "__main__":
    main()
