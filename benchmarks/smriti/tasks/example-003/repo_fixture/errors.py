class ValidationError(Exception):
    """Raised when input fails this project's validation rules.
    Caught centrally by the CLI entrypoint's error handler."""
