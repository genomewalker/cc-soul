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
# MAIN APPLICATION
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class SadhanaApp(App):
    """Minimal agent control interface."""

    TITLE = "sadhana"

    BINDINGS = [
        Binding("n", "new", "new"),
        Binding("p", "pause", "pause"),
        Binding("r", "resume", "resume"),
        Binding("s", "stop", "stop"),
        Binding("j", "next", "↓"),
        Binding("k", "prev", "↑"),
        Binding("q", "quit", "quit"),
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
    #status-bar {
        width: 1fr;
        content-align: right middle;
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
        self._selected_idx: int = 0

    def compose(self) -> ComposeResult:
        with Horizontal(id="header"):
            yield Static("sadhana", id="brand")
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

        yield Footer()

    def on_mount(self) -> None:
        self.refresh_all()
        self.set_interval(2.0, self.refresh_list)
        self.set_interval(1.0, self.refresh_events)

    def on_agent_card_clicked(self, event: AgentCard.Clicked) -> None:
        """Handle card click."""
        self._selected_idx = event.index
        self._update_selection()
        self.refresh_events()

    def refresh_all(self) -> None:
        self.refresh_list()
        self.refresh_events()

    def refresh_list(self) -> None:
        try:
            new_sadhanas = self.client.sadhana_list()
        except Exception:
            return  # Skip on connection error

        self._sadhanas = new_sadhanas

        total = len(self._sadhanas)
        running = sum(1 for s in self._sadhanas if s.get("state") == "running")

        try:
            health = self.client.health_check()
            dot = "[#66aa66]●[/]" if "error" not in health else "[#aa6666]●[/]"
        except Exception:
            dot = "[#aa6666]●[/]"

        status = self.query_one("#status-bar", Static)
        status.update(f"[#444444]{running}[/][#333333]/[/][#444444]{total}[/] {dot}")

        agent_list = self.query_one("#agent-list", Horizontal)

        # Get existing cards and compare
        old_cards = list(self.query(AgentCard))
        old_ids = {c.sadhana.get("id") for c in old_cards}
        new_ids = {s.get("id") for s in self._sadhanas}

        # Remove cards that no longer exist
        for card in old_cards:
            if card.sadhana.get("id") not in new_ids:
                card.remove()

        # Update existing cards or add new ones
        existing_ids = {c.sadhana.get("id"): c for c in self.query(AgentCard)}
        for i, s in enumerate(self._sadhanas):
            sid = s.get("id")
            if sid in existing_ids:
                # Update existing card
                card = existing_ids[sid]
                card.sadhana = s
                card.index = i
                card.selected = (i == self._selected_idx)
                # Update state class
                for state in ["running", "paused", "done", "failed"]:
                    card.remove_class(state)
                card.add_class(s.get("state", "unknown"))
                card.refresh()
            else:
                # Add new card
                card = AgentCard(s, index=i)
                card.selected = (i == self._selected_idx)
                agent_list.mount(card)

        # Force container refresh
        agent_list.refresh(layout=True)
        self.query_one(HorizontalScroll).refresh(layout=True)

    def refresh_events(self) -> None:
        if not self._sadhanas or self._selected_idx >= len(self._sadhanas):
            return

        selected = self._sadhanas[self._selected_idx]
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
        if self._sadhanas:
            self._selected_idx = (self._selected_idx + 1) % len(self._sadhanas)
            self._update_selection()
            self.refresh_events()

    def action_prev(self) -> None:
        if self._sadhanas:
            self._selected_idx = (self._selected_idx - 1) % len(self._sadhanas)
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
        if self._sadhanas and self._selected_idx < len(self._sadhanas):
            sid = self._sadhanas[self._selected_idx].get("id")
            if "error" not in self.client.sadhana_pause(sid):
                self.notify(f"#{sid} paused")
                self.refresh_list()

    def action_resume(self) -> None:
        if self._sadhanas and self._selected_idx < len(self._sadhanas):
            sid = self._sadhanas[self._selected_idx].get("id")
            if "error" not in self.client.sadhana_resume(sid):
                self.notify(f"#{sid} resumed")
                self.refresh_list()

    def action_stop(self) -> None:
        if self._sadhanas and self._selected_idx < len(self._sadhanas):
            sid = self._sadhanas[self._selected_idx].get("id")
            if "error" not in self.client.sadhana_stop(sid):
                self.notify(f"#{sid} stopped")
                self.refresh_list()


def main():
    app = SadhanaApp()
    app.run()


if __name__ == "__main__":
    main()
