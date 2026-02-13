"""SadhanaDetail screen - detailed view of a single sadhana."""

from textual.widgets import Static
from textual.containers import Container

from ..client import ChittaClient


class SadhanaDetail(Static):
    """Widget showing detailed information about a selected sadhana."""

    DEFAULT_CSS = """
    SadhanaDetail {
        height: auto;
        padding: 1;
        background: #1f2335;
        border: round #3d59a1;
        margin: 1 0;
    }
    """

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._sadhana_id: int | None = None
        self._status: dict = {}

    def set_sadhana(self, sadhana_id: int | None) -> None:
        """Set the sadhana to display."""
        self._sadhana_id = sadhana_id
        if sadhana_id is None:
            self._status = {}
        self.refresh()

    def refresh_status(self, client: ChittaClient) -> None:
        """Refresh status from daemon."""
        if self._sadhana_id is None:
            return

        self._status = client.sadhana_status(self._sadhana_id, history_limit=0)
        self.refresh()

    def render(self) -> str:
        """Render the detail view."""
        if self._sadhana_id is None:
            return "[dim]Select a sadhana to view details[/dim]"

        if "error" in self._status:
            return f"[red]Error: {self._status['error']}[/red]"

        # Extract fields
        sadhana_id = self._status.get("id", self._sadhana_id)
        state = self._status.get("state", "unknown")
        goal = self._status.get("goal", "")
        brain = self._status.get("brain", "")
        model = self._status.get("model", "")
        interval = self._status.get("interval", 0)
        iterations = self._status.get("iterations", 0)
        last_tick = self._status.get("last_tick", "")
        created = self._status.get("created", "")

        # State icon
        state_icons = {
            "running": "\u25b6",   # ▶
            "paused": "\u23f8",    # ⏸
            "done": "\u2713",      # ✓
            "failed": "\u2717",    # ✗
            "stopped": "\u25a0",   # ■
        }
        state_icon = state_icons.get(state, "?")

        # Format timestamps
        if last_tick and "T" in last_tick:
            last_tick = last_tick.replace("T", " ")[:19]
        if created and "T" in created:
            created = created.replace("T", " ")[:19]

        # State color
        state_color = {
            "running": "#9ece6a",  # green
            "paused": "#e0af68",   # yellow
            "done": "#7aa2f7",     # blue
            "failed": "#f7768e",   # red
        }.get(state, "#a9b1d6")

        lines = [
            f"[bold #7aa2f7]Sadhana #{sadhana_id}[/bold #7aa2f7]",
            "",
            f"[#565f89]State:[/#565f89]      [{state_color}]{state_icon} {state}[/{state_color}]",
            f"[#565f89]Goal:[/#565f89]       [#c0caf5]{goal}[/#c0caf5]",
            f"[#565f89]Brain:[/#565f89]      [#bb9af7]{brain}[/#bb9af7]",
            f"[#565f89]Model:[/#565f89]      [#bb9af7]{model}[/#bb9af7]",
            f"[#565f89]Interval:[/#565f89]   [#a9b1d6]{interval}s[/#a9b1d6]",
            f"[#565f89]Iterations:[/#565f89] [#ff9e64]{iterations}[/#ff9e64]",
            f"[#565f89]Last Tick:[/#565f89]  [#a9b1d6]{last_tick}[/#a9b1d6]",
            f"[#565f89]Created:[/#565f89]    [#a9b1d6]{created}[/#a9b1d6]",
        ]

        return "\n".join(lines)
