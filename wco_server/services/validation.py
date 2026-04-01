import csv
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

VALIDATION_CSV = Path("data/validation_log.csv")

_CSV_HEADER = [
    "timestamp", "filename", "group",
    "expected_t", "expected_p", "expected_c",
    "actual_t",   "actual_p",   "actual_c",
    "match_t",    "match_p",    "match_c",
    "contrast", "laplacian", "darkening", "quality",
]

_GROUPS = ("clean", "turbid", "turbid_particle")


def ensure_validation_csv() -> None:
    if not VALIDATION_CSV.exists():
        with open(VALIDATION_CSV, "w", newline="") as f:
            csv.writer(f).writerow(_CSV_HEADER)


def append_validation_row(row: dict) -> None:
    with open(VALIDATION_CSV, "a", newline="") as f:
        csv.writer(f).writerow([row.get(col, "") for col in _CSV_HEADER])


def read_validation_report() -> dict:
    if not VALIDATION_CSV.exists():
        return {"total": 0, "message": "No validation data yet"}

    with open(VALIDATION_CSV, newline="") as f:
        rows = list(csv.DictReader(f))

    if not rows:
        return {"total": 0, "message": "No validation data yet"}

    total = len(rows)
    evaluated = correct = 0

    per_label = {
        k: {"evaluated": 0, "correct": 0, "accuracy": None}
        for k in ("t", "p", "c")
    }
    per_group: dict[str, dict] = {g: {"total": 0, "correct": 0} for g in _GROUPS}

    for row in rows:
        group = row.get("group", "")
        if group in per_group:
            per_group[group]["total"] += 1

        # Determine which labels were evaluated (expected not empty)
        row_evaluated = False
        row_all_correct = True

        for label in ("t", "p", "c"):
            exp_raw = row.get(f"expected_{label}", "")
            if exp_raw == "" or exp_raw is None:
                continue
            row_evaluated = True
            per_label[label]["evaluated"] += 1
            match_raw = row.get(f"match_{label}", "")
            if match_raw == "True":
                per_label[label]["correct"] += 1
            else:
                row_all_correct = False

        if row_evaluated:
            evaluated += 1
            if row_all_correct:
                correct += 1
                if group in per_group:
                    per_group[group]["correct"] += 1

    # Compute per-label accuracy
    for label in ("t", "p", "c"):
        ev = per_label[label]["evaluated"]
        per_label[label]["accuracy"] = (
            round(per_label[label]["correct"] / ev, 4) if ev else None
        )

    return {
        "total":      total,
        "evaluated":  evaluated,
        "correct":    correct,
        "accuracy":   round(correct / evaluated, 4) if evaluated else None,
        "per_label":  per_label,
        "per_group":  per_group,
        "rows":       rows,
    }
