"""EventLog widget - displays sense/think/act events in real-time."""

from textual.widgets import RichLog

from ..client import ChittaClient


EVENT_ICONS = {
    "sense": "\U0001F441",    # 👁
    "think": "\U0001F9E0",    # 🧠
    "act": "\u26A1",          # ⚡
    "learn": "\U0001F4DA",    # 📚
    "start": "\U0001F680",    # 🚀
    "stop": "\U0001F6D1",     # 🛑
    "pause": "\u23F8",        # ⏸
    "resume": "\u25B6",       # ▶
    "error": "\u274C",        # ❌
}


class EventLog(RichLog):
    """Widget showing sense/think/act events for a sadhana."""

    def __init__(self, **kwargs):
        super().__init__(highlight=True, markup=True, **kwargs)
        self._current_sadhana_id: int | None = None
        self._last_event_count: int = 0

    def set_sadhana(self, sadhana_id: int | None) -> None:
        """Set the sadhana to display events for."""
        if sadhana_id != self._current_sadhana_id:
            self._current_sadhana_id = sadhana_id
            self._last_event_count = 0
            self.clear()

    def refresh_events(self, client: ChittaClient) -> None:
        """Refresh events from daemon."""
        if self._current_sadhana_id is None:
            return

        status = client.sadhana_status(self._current_sadhana_id, history_limit=50)
        if "error" in status:
            self.write(f"[red]Error: {status['error']}[/red]")
            return

        history = status.get("history", [])

        # Only add new events
        if len(history) > self._last_event_count:
            new_events = history[self._last_event_count:]
            for event in new_events:
                self._write_event(event)
            self._last_event_count = len(history)

    def _write_event(self, event: dict) -> None:
        """Write a single event to the log."""
        event_type = event.get("event_type", "unknown")
        raw_content = event.get("content", "")
        timestamp = event.get("timestamp", 0)
        duration_ms = event.get("duration_ms", 0)

        icon = EVENT_ICONS.get(event_type, "\u2022")  # • as default

        # Color based on event type (Tokyo Night palette)
        color = {
            "sense": "#7dcfff",    # cyan
            "think": "#e0af68",    # yellow/orange
            "act": "#9ece6a",      # green
            "learn": "#bb9af7",    # purple
            "error": "#f7768e",    # red
            "start": "#7aa2f7",    # blue
            "stop": "#f7768e",     # red
            "pause": "#e0af68",    # yellow
            "resume": "#9ece6a",   # green
        }.get(event_type, "#a9b1d6")

        # Format timestamp (unix ms -> HH:MM:SS)
        time_str = ""
        if timestamp:
            try:
                from datetime import datetime
                dt = datetime.fromtimestamp(timestamp / 1000)
                time_str = dt.strftime("%H:%M:%S")
            except (ValueError, OSError):
                time_str = ""

        # Extract content string from nested structure
        content = ""
        if isinstance(raw_content, dict):
            # Sense events have command/output
            if "command" in raw_content:
                cmd = raw_content.get("command", "")
                # Strip markdown code blocks
                cmd = cmd.replace("```bash", "").replace("```", "").strip()
                success = raw_content.get("success", False)
                output = raw_content.get("output", "")[:100]
                status = "\u2713" if success else "\u2717"
                content = f"{status} {cmd}"
                if output:
                    content += f" -> {output}"
            # Think events have decision/reasoning
            elif "decision" in raw_content:
                content = raw_content.get("decision", "")
            elif "reasoning" in raw_content:
                content = raw_content.get("reasoning", "")
            # Act events
            elif "action" in raw_content:
                content = raw_content.get("action", "")
            else:
                # Fallback: show keys or first string value
                for v in raw_content.values():
                    if isinstance(v, str) and len(v) > 5:
                        content = v
                        break
                if not content:
                    content = str(list(raw_content.keys()))
        elif isinstance(raw_content, str):
            content = raw_content

        # Truncate long content
        if len(content) > 200:
            content = content[:200] + "..."

        # Duration info
        duration_str = ""
        if duration_ms > 0:
            duration_str = f" ({duration_ms}ms)"

        self.write(f"[#565f89]{time_str}[/#565f89] {icon} [{color}]{event_type}[/{color}][#565f89]{duration_str}[/#565f89]: [#c0caf5]{content}[/#c0caf5]")
