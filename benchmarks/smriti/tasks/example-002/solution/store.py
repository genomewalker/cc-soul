import json


def save_data(obj, path):
    """Persist obj to path."""
    with open(path, "w") as f:
        json.dump(obj, f)


def load_data(path):
    """Load an object previously written by save_data."""
    with open(path) as f:
        return json.load(f)
