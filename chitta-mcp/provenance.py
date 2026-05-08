#!/usr/bin/env python3
"""
Provenance extraction for task tracking.
Extracts inputs, outputs, params, job IDs from command + stdout + filesystem.
Pure stdlib — yaml/toml are attempted but not required.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


# ─── Git + environment ────────────────────────────────────────────────────────

def git_info(cwd: str) -> dict:
    def _run(args: list[str]) -> str:
        try:
            r = subprocess.run(args, capture_output=True, text=True, cwd=cwd, timeout=1)
            return r.stdout.strip() if r.returncode == 0 else ""
        except Exception:
            return ""
    return {
        "git_branch": _run(["git", "rev-parse", "--abbrev-ref", "HEAD"]),
        "git_commit": _run(["git", "rev-parse", "--short", "HEAD"]),
    }


def env_info(env: dict | None = None) -> dict:
    e = env or os.environ
    return {
        "conda_env":   e.get("CONDA_DEFAULT_ENV") or e.get("CONDA_ENV") or "",
        "virtual_env": e.get("VIRTUAL_ENV", ""),
    }


# ─── CLI flag parsing ─────────────────────────────────────────────────────────

_INPUT_FLAGS = re.compile(
    r"^(-i|--input|--in|--reads|--samples|--manifest|--samplesheet|"
    r"--sample-sheet|--bam|--vcf|--fasta|--fastq|--query)$",
    re.I,
)
_OUTPUT_FLAGS = re.compile(
    r"^(-o|--output|--out|--outdir|--out-dir|--output-dir|"
    r"--results|--results-dir|--prefix|--outprefix)$",
    re.I,
)
_CONFIG_FLAGS = re.compile(
    r"^(--config|--configfile|--config-file|--params|--parameters|"
    r"--settings|--profile|--workflow-profile|--snakefile|--nextflow-config)$",
    re.I,
)
_REF_FLAGS = re.compile(
    r"^(--db|--database|--reference|--ref|--model|--index|--taxonomy|"
    r"--gtdb|--diamond-db|--blast-db|--kraken-db)$",
    re.I,
)
_PARAM_FLAGS = re.compile(
    r"^(-[jt]|--threads|--cores|--jobs|--min[-_]|--max[-_]|--threshold|"
    r"--cutoff|--evalue|--identity|--coverage|--min-depth|--min-reads|"
    r"--metric|--mode|--strategy|--method|--algorithm|--seed|--memory|--mem).*",
    re.I,
)


def _parse_val(v: str) -> int | float | str:
    try:
        return int(v)
    except ValueError:
        pass
    try:
        return float(v)
    except ValueError:
        pass
    return v


def parse_cli_flags(command: str) -> dict:
    result: dict = {
        "inputs": [], "outputs": [], "params": {},
        "config_files": [], "references": [],
    }
    try:
        tokens = shlex.split(command, posix=True)
    except ValueError:
        tokens = command.split()
    if not tokens:
        return result

    i = 1
    while i < len(tokens):
        tok = tokens[i]
        val = tokens[i + 1] if i + 1 < len(tokens) else None

        if _INPUT_FLAGS.match(tok) and val and not val.startswith("-"):
            result["inputs"].append(val)
            i += 2
        elif _OUTPUT_FLAGS.match(tok) and val and not val.startswith("-"):
            result["outputs"].append(val)
            i += 2
        elif _CONFIG_FLAGS.match(tok) and val and not val.startswith("-"):
            result["config_files"].append(val)
            i += 2
        elif _REF_FLAGS.match(tok) and val and not val.startswith("-"):
            result["references"].append(val)
            i += 2
        elif _PARAM_FLAGS.match(tok):
            if val and not val.startswith("-"):
                key = tok.lstrip("-").replace("-", "_")
                result["params"][key] = _parse_val(val)
                i += 2
            elif "=" in tok:
                k, v = tok.split("=", 1)
                result["params"][k.lstrip("-").replace("-", "_")] = _parse_val(v)
                i += 1
            else:
                i += 1
        elif tok.startswith("--") and "=" in tok:
            k, v = tok.split("=", 1)
            if _PARAM_FLAGS.match(k):
                result["params"][k.lstrip("-").replace("-", "_")] = _parse_val(v)
            i += 1
        elif not tok.startswith("-") and i > 0 and Path(tok).exists():
            result["inputs"].append(tok)
            i += 1
        else:
            i += 1

    return result


def _parse_config_text(text: str, suffix: str) -> dict:
    suffix = suffix.lower()
    if suffix in (".yaml", ".yml"):
        try:
            import yaml  # type: ignore
            return yaml.safe_load(text) or {}
        except Exception:
            pass
    elif suffix == ".json":
        try:
            return json.loads(text)
        except Exception:
            pass
    elif suffix == ".toml":
        for loader in ("tomllib", "tomli"):
            try:
                mod = __import__(loader)
                return mod.loads(text)
            except Exception:
                pass
    # Fallback: JSON then simple key=value
    try:
        return json.loads(text)
    except Exception:
        pass
    result: dict = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        for sep in ("=", ":"):
            if sep in line:
                k, _, v = line.partition(sep)
                result[k.strip()] = v.strip().strip('"').strip("'")
                break
    return result


def ingest_config_files(config_paths: list[str]) -> dict:
    params: dict = {}
    extra_inputs: list[str] = []
    for cp in config_paths:
        p = Path(cp)
        if not p.exists():
            continue
        try:
            text = p.read_text(errors="replace")
            data = _parse_config_text(text, p.suffix)
            if isinstance(data, dict):
                for k, v in data.items():
                    if isinstance(v, (str, int, float, bool)):
                        params[k] = v
                    elif isinstance(v, str) and Path(v).exists():
                        extra_inputs.append(v)
        except Exception:
            pass
    return {"params": params, "extra_inputs": extra_inputs}


# ─── Stdout parsing ────────────────────────────────────────────────────────────

_JOB_ID_PATTERNS = [
    (re.compile(r"Submitted batch job (\d+)", re.I), "slurm"),
    (re.compile(r"Your job(?:-array)? (\d+)[.\s]", re.I), "sge"),
    (re.compile(r"Job <(\d+)> is submitted", re.I), "lsf"),
    (re.compile(r"Job ID:\s*(\d+)", re.I), "generic"),
    (re.compile(r"jobid[:\s]+(\d+)", re.I), "generic"),
]

_OUTPUT_PATH_PATTERNS = [
    re.compile(r"(?:writing|saving|output|wrote|creating)\s+(?:to\s+)?([/~][^\s,;]+)", re.I),
    re.compile(r"(?:output|results?)\s*(?:dir(?:ectory)?|path)[:\s]+([/~][^\s,;]+)", re.I),
    re.compile(r"(?:log(?:ging)?|logfile)[:\s]+([/~][^\s,;]+)", re.I),
]

_COMPLETION_PATTERNS = [
    re.compile(r"(\d+)/(\d+)\s+(?:tasks?|jobs?|samples?|reads?)\s+(?:completed?|done|finished|processed)", re.I),
    re.compile(r"(?:completed?|finished|done)[^\n]*?(\d+)\s+(?:tasks?|jobs?|samples?)", re.I),
    re.compile(r"snakemake.*?(\d+)\s+of\s+(\d+)", re.I),
    re.compile(r"nextflow.*?(\d+)\s+(?:task|process)", re.I),
]

_ERROR_RE = re.compile(
    r"(error|exception|traceback|failed|killed|oom|out of memory|segfault|sigkill|sigterm)",
    re.I,
)


def parse_stdout(text: str) -> dict:
    result: dict = {
        "job_id": None, "scheduler": None,
        "declared_outputs": [], "completion_digest": None, "has_error": False,
    }
    if not text:
        return result

    for pattern, scheduler in _JOB_ID_PATTERNS:
        m = pattern.search(text)
        if m:
            result["job_id"] = m.group(1)
            result["scheduler"] = scheduler
            break

    for pattern in _OUTPUT_PATH_PATTERNS:
        for m in pattern.finditer(text):
            path = m.group(1).rstrip(".,;)")
            if path not in result["declared_outputs"]:
                result["declared_outputs"].append(path)

    lines = text.strip().splitlines()
    tail = "\n".join(lines[-20:])
    for pattern in _COMPLETION_PATTERNS:
        if pattern.search(tail):
            result["completion_digest"] = tail[-500:]
            break
    if not result["completion_digest"] and len(lines) > 3:
        result["completion_digest"] = "\n".join(lines[-5:])

    result["has_error"] = bool(_ERROR_RE.search(text))
    return result


# ─── Filesystem snapshot + diff ───────────────────────────────────────────────

_SKIP_DIRS = {
    ".git", "__pycache__", ".venv", "venv", "node_modules",
    ".snakemake", ".nextflow", "work", "target", "dist", "build",
}


def snapshot(cwd: str, depth: int = 2) -> dict:
    """Record mtime of files under cwd (depth-limited) for pre/post diff."""
    result: dict = {}
    base = Path(cwd)
    if not base.is_dir():
        return result
    try:
        _scan(base, depth, result)
    except Exception:
        pass
    return result


def _scan(current: Path, depth: int, acc: dict) -> None:
    if depth < 0:
        return
    try:
        for entry in current.iterdir():
            if entry.name in _SKIP_DIRS or entry.name.startswith("."):
                continue
            if entry.is_file():
                try:
                    acc[str(entry)] = entry.stat().st_mtime
                except OSError:
                    pass
            elif entry.is_dir() and depth > 0:
                _scan(entry, depth - 1, acc)
    except PermissionError:
        pass


def filesystem_diff(before: dict, after: dict) -> list[str]:
    return [
        path for path, mtime in after.items()
        if path not in before or mtime > before[path] + 0.01
    ]


# ─── Status-check inference ───────────────────────────────────────────────────

def infer_status_check(job_id: str | None, scheduler: str | None) -> str:
    if not job_id:
        return ""
    if scheduler == "slurm":
        return f"squeue -j {job_id} -h -o '%T' 2>/dev/null || echo COMPLETED"
    if scheduler == "sge":
        return f"qstat -j {job_id} 2>/dev/null | grep -c job_number || echo 0"
    if scheduler == "lsf":
        return f"bjobs {job_id} 2>/dev/null | tail -1 | awk '{{print $3}}'"
    return (
        f"ps -p {job_id} -o pid= 2>/dev/null | grep -q . && echo RUNNING || echo COMPLETED"
    )


# ─── Full extraction ──────────────────────────────────────────────────────────

def extract(
    command: str,
    cwd: str,
    env: dict | None = None,
    stdout: str = "",
    before_snapshot: dict | None = None,
    after_snapshot: dict | None = None,
) -> dict:
    flags = parse_cli_flags(command)
    config_data = ingest_config_files(flags["config_files"])
    merged_params = {**flags["params"], **config_data["params"]}
    all_inputs = flags["inputs"] + config_data["extra_inputs"]

    stdout_data = parse_stdout(stdout)
    new_files: list[str] = []
    if before_snapshot is not None and after_snapshot is not None:
        new_files = filesystem_diff(before_snapshot, after_snapshot)

    all_outputs = list({*flags["outputs"], *stdout_data["declared_outputs"], *new_files})

    git = git_info(cwd)
    env_data = env_info(env)
    status_check = infer_status_check(stdout_data["job_id"], stdout_data["scheduler"])

    return {
        "cmd":               command,
        "cwd":               cwd,
        "git_branch":        git["git_branch"],
        "git_commit":        git["git_commit"],
        "conda_env":         env_data["conda_env"],
        "virtual_env":       env_data["virtual_env"],
        "inputs":            all_inputs,
        "outputs":           all_outputs,
        "params":            merged_params,
        "config_files":      flags["config_files"],
        "references":        flags["references"],
        "job_id":            stdout_data["job_id"],
        "scheduler":         stdout_data["scheduler"],
        "status_check_cmd":  status_check,
        "completion_digest": stdout_data["completion_digest"],
        "has_error":         stdout_data["has_error"],
        "declared_outputs":  stdout_data["declared_outputs"],
        "new_files":         new_files,
        "binary":            Path(command.split()[0]).name if command else "",
    }


# ─── CLI ──────────────────────────────────────────────────────────────────────

def _cli() -> None:
    p = argparse.ArgumentParser(prog="provenance")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("extract")
    s.add_argument("--cmd", required=True, dest="command")
    s.add_argument("--cwd", default=os.getcwd())
    s.add_argument("--stdout-from-stdin", action="store_true", dest="stdout_from_stdin")
    s.add_argument("--before-snapshot", dest="before_snapshot")
    s.add_argument("--after-snapshot", dest="after_snapshot")

    s = sub.add_parser("snapshot")
    s.add_argument("--cwd", default=os.getcwd())
    s.add_argument("--out", required=True)

    s = sub.add_parser("parse_stdout")
    s.add_argument("--stdin", action="store_true")

    args = p.parse_args()
    result: object = None

    if args.cmd == "extract":
        raw_stdout = ""
        if args.stdout_from_stdin:
            raw = sys.stdin.read()
            try:
                data = json.loads(raw)
                raw_stdout = (
                    data.get("tool_result", {}).get("stdout", "")
                    or data.get("stdout", "")
                    or ""
                )
            except Exception:
                raw_stdout = raw
        before = None
        after = None
        if args.before_snapshot and Path(args.before_snapshot).exists():
            before = json.loads(Path(args.before_snapshot).read_text())
        if args.after_snapshot and Path(args.after_snapshot).exists():
            after = json.loads(Path(args.after_snapshot).read_text())
        result = extract(args.command, args.cwd, stdout=raw_stdout,
                         before_snapshot=before, after_snapshot=after)

    elif args.cmd == "snapshot":
        snap = snapshot(args.cwd)
        Path(args.out).write_text(json.dumps(snap))
        result = {"written": args.out, "files": len(snap)}

    elif args.cmd == "parse_stdout":
        text = sys.stdin.read() if args.stdin else ""
        result = parse_stdout(text)

    print(json.dumps(result, default=str))


if __name__ == "__main__":
    _cli()
