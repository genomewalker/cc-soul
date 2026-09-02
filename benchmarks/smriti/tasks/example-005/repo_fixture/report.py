import argparse


def build_output(rows, fmt):
    """Render rows (list of (name, count) tuples) as fmt ('csv' or 'json')."""
    raise NotImplementedError


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", default="json")
    args = parser.parse_args()
    rows = [("widgets", 3)]
    print(build_output(rows, args.format))


if __name__ == "__main__":
    main()
