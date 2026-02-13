"""Dashboard screen - main overview of all sadhanas."""

from textual.screen import Screen
from textual.widgets import Header, Footer, Static
from textual.containers import Container, Horizontal, Vertical

from ..client import ChittaClient


class DashboardStats(Static):
    """Widget showing aggregate statistics."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._stats = {
            "total": 0,
            "running": 0,
            "paused": 0,
            "done": 0,
            "failed": 0,
        }

    def update_stats(self, sadhanas: list[dict]) -> None:
        """Update stats from sadhana list."""
        self._stats = {
            "total": len(sadhanas),
            "running": sum(1 for s in sadhanas if s.get("state") == "running"),
            "paused": sum(1 for s in sadhanas if s.get("state") == "paused"),
            "done": sum(1 for s in sadhanas if s.get("state") == "done"),
            "failed": sum(1 for s in sadhanas if s.get("state") == "failed"),
        }
        self.refresh()

    def render(self) -> str:
        """Render the stats display."""
        return (
            f"Total: {self._stats['total']}  "
            f"\u25b6 Running: {self._stats['running']}  "
            f"\u23f8 Paused: {self._stats['paused']}  "
            f"\u2713 Done: {self._stats['done']}  "
            f"\u2717 Failed: {self._stats['failed']}"
        )


class HealthStatus(Static):
    """Widget showing daemon health status."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._healthy = False
        self._message = "Checking..."

    def update_health(self, client: ChittaClient) -> None:
        """Update health from daemon."""
        result = client.health_check()
        if "error" in result:
            self._healthy = False
            self._message = f"Disconnected: {result['error']}"
        else:
            self._healthy = result.get("healthy", False)
            self._message = result.get("status", "OK")
        self.refresh()

    def render(self) -> str:
        """Render health status."""
        icon = "\u2705" if self._healthy else "\u274c"  # ✅ or ❌
        return f"{icon} Daemon: {self._message}"


class DashboardScreen(Screen):
    """Main dashboard screen showing all sadhanas."""

    CSS = """
    DashboardScreen {
        layout: vertical;
    }

    #stats-bar {
        height: 3;
        padding: 1;
        background: $surface;
        border-bottom: solid $primary;
    }

    #health-status {
        dock: right;
        width: auto;
        padding-right: 2;
    }

    #dashboard-stats {
        width: 1fr;
    }
    """

    def compose(self):
        """Compose the dashboard layout."""
        yield Header()
        with Horizontal(id="stats-bar"):
            yield DashboardStats(id="dashboard-stats")
            yield HealthStatus(id="health-status")
        yield Footer()

    def refresh_stats(self, client: ChittaClient, sadhanas: list[dict]) -> None:
        """Refresh dashboard statistics."""
        stats = self.query_one("#dashboard-stats", DashboardStats)
        stats.update_stats(sadhanas)

        health = self.query_one("#health-status", HealthStatus)
        health.update_health(client)
