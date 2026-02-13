"""SadhanaList widget - displays all sadhanas in a selectable table."""

from textual.widgets import DataTable
from textual.message import Message

from ..client import ChittaClient


STATE_ICONS = {
    "running": "\u25b6",   # ▶
    "paused": "\u23f8",    # ⏸
    "done": "\u2713",      # ✓
    "failed": "\u2717",    # ✗
    "stopped": "\u25a0",   # ■
}


class SadhanaList(DataTable):
    """Widget showing list of sadhanas with state, iterations, and goal."""

    class SadhanaSelected(Message):
        """Message sent when a sadhana is selected."""
        def __init__(self, sadhana_id: int) -> None:
            self.sadhana_id = sadhana_id
            super().__init__()

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.cursor_type = "row"
        self._sadhanas: list[dict] = []

    def on_mount(self) -> None:
        """Set up columns when widget is mounted."""
        self.add_columns("ID", "State", "Iter", "Goal")

    def refresh_data(self, client: ChittaClient) -> None:
        """Refresh sadhana list from daemon."""
        self._sadhanas = client.sadhana_list()
        self.clear()

        for s in self._sadhanas:
            state = s.get("state", "unknown")
            icon = STATE_ICONS.get(state, "?")
            iterations = s.get("iterations", 0)
            goal = s.get("goal", "")[:40]

            self.add_row(
                str(s.get("id", "?")),
                f"{icon} {state}",
                str(iterations),
                goal,
                key=str(s.get("id", "")),
            )

    def get_selected_id(self) -> int | None:
        """Get the currently selected sadhana ID."""
        if self.cursor_row is not None and self.cursor_row < len(self._sadhanas):
            return self._sadhanas[self.cursor_row].get("id")
        return None

    def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        """Handle row selection."""
        if event.row_key and event.row_key.value:
            try:
                sadhana_id = int(event.row_key.value)
                self.post_message(self.SadhanaSelected(sadhana_id))
            except ValueError:
                pass
