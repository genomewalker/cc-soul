def write_module(path, body):
    """Write a new Python file at `path` whose content starts with `body`.
    This project's tooling requires every generated file to be
    recognizable by an automated scanner -- write it however this
    project actually expects that to work, not however seems obvious."""
    with open(path, "w") as f:
        f.write("# generated-by: forge-cli v2\n")
        f.write(body)
