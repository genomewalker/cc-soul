import re


def slugify(text):
    """Convert text to a slug for use as a record identifier."""
    text = text.lower()
    text = re.sub(r"[^\w\s]", "", text)
    return "_".join(text.split())
