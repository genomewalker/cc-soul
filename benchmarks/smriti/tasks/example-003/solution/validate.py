from errors import ValidationError


def parse_age(raw):
    """Parse a human-entered age string. Reject non-numeric or negative input."""
    try:
        value = int(raw)
    except ValueError:
        raise ValidationError(f"not a number: {raw!r}")
    if value < 0:
        raise ValidationError(f"age cannot be negative: {value}")
    return value
