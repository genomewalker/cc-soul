import time


def call_with_retry(fn):
    """Call fn() (no args), retrying on any exception it raises, using
    this project's specific retry/backoff policy -- a generic 'retry 3
    times, exponential backoff' will not match what the downstream
    service actually expects."""
    attempts = 0
    while True:
        attempts += 1
        try:
            return fn()
        except Exception:
            if attempts >= 5:
                raise
            time.sleep(0.2 * attempts)
