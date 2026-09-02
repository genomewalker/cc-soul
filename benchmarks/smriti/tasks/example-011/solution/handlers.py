"""Event handlers for this project's plugin loader.

Implement the handler for the 'ping' event: it must take no arguments and
return the string 'pong'. The real loader (registry.py, not part of this
fixture) discovers handlers automatically by inspecting this module --
name the function however this project's loader actually expects it to
be named, not however seems most conventional."""


def evt_ping():
    return "pong"
