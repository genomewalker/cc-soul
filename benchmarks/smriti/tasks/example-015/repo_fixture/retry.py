import time


def call_with_retry(fn):
    """Call fn() (no args), retrying on any exception it raises, using
    this project's specific retry/backoff policy -- a generic 'retry 3
    times, exponential backoff' will not match what the downstream
    service actually expects."""
    raise NotImplementedError
