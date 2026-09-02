def log_event(name, status):
    """Print one audit line for (name, status) to stdout, in the exact
    format this project's downstream log-shipper expects -- a generic
    'name: status' or JSON line will be silently dropped by the shipper's
    parser, not shipped."""
    raise NotImplementedError
