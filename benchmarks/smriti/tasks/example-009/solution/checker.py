import sys
from pathlib import Path


def main(argv):
    """Print the line count of the file at argv[1]. Return this repo's
    documented exit code for the outcome."""
    if len(argv) < 2:
        return 1
    path = Path(argv[1])
    if not path.exists():
        return 3
    print(len(path.read_text().splitlines()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
