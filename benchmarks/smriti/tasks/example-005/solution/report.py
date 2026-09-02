import argparse
import json


def build_output(rows, fmt):
    """Render rows (list of (name, count) tuples) as fmt ('csv' or 'json')."""
    if fmt == "csv":
        lines = ["name,count"] + [f"{name},{count}" for name, count in rows]
        return "\n".join(lines)
    if fmt == "json":
        return json.dumps([{"name": n, "count": c} for n, c in rows])
    raise ValueError(f"unknown format: {fmt}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", default="csv")
    args = parser.parse_args()
    rows = [("widgets", 3)]
    print(build_output(rows, args.format))


if __name__ == "__main__":
    main()
