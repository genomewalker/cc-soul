import configparser
from pathlib import Path


def load_setting(key, section="app"):
    """Read a setting from this project's config file."""
    parser = configparser.ConfigParser()
    parser.read(Path(__file__).parent / "config" / "app.ini")
    return parser[section][key]
