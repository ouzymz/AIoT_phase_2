import csv
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Optional

DATA_DIR = Path("data")
IMAGES_DIR = DATA_DIR / "images"
LOG_CSV = DATA_DIR / "log.csv"

_CSV_HEADER = ["timestamp", "filename", "t", "p", "c"]

# Matches: t1_p0_c1_0042.jpg  (labels are single digits; index is 1-4 digits)
_NAME_RE = re.compile(r"^t(\d+)_p(\d+)_c(\d+)_(\d+)\.jpg$", re.IGNORECASE)


def ensure_dirs() -> None:
    IMAGES_DIR.mkdir(parents=True, exist_ok=True)
    if not LOG_CSV.exists():
        with open(LOG_CSV, "w", newline="") as f:
            csv.writer(f).writerow(_CSV_HEADER)


def parse_labels(filename: str) -> Optional[Dict[str, int]]:
    """Return {"t": int, "p": int, "c": int} or None if name doesn't match."""
    m = _NAME_RE.match(filename)
    if not m:
        return None
    return {"t": int(m.group(1)), "p": int(m.group(2)), "c": int(m.group(3))}


def resolve_path(original: str) -> Path:
    """Return destination path; if file already exists append a UTC timestamp."""
    candidate = IMAGES_DIR / original
    if not candidate.exists():
        return candidate
    stem = Path(original).stem
    ext = Path(original).suffix
    ts = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")
    return IMAGES_DIR / f"{stem}_{ts}{ext}"


def append_log(filename: str, labels: Dict[str, int], scores: Optional[Dict[str, int]] = None) -> None:
    ts = datetime.now(timezone.utc).isoformat()
    s = scores or {}
    with open(LOG_CSV, "a", newline="") as f:
        csv.writer(f).writerow([
            ts,
            filename,
            labels["t"],
            labels["p"],
            labels["c"],
            s.get("contrast",  ""),
            s.get("blobs",     ""),
            s.get("darkening", ""),
            s.get("quality",   ""),
        ])

def reset_log() -> None:
    """Overwrite log.csv with just the header row."""
    with open(LOG_CSV, "w", newline="") as f:
        csv.writer(f).writerow(_CSV_HEADER)


def read_stats() -> dict:
    if not LOG_CSV.exists():
        return {
            "total": 0,
            "per_combination": {},
            "per_label_true": {"t": 0, "p": 0, "c": 0},
        }

    rows: list[dict] = []
    with open(LOG_CSV, newline="") as f:
        rows = list(csv.DictReader(f))

    per_combination: Dict[str, int] = {}
    per_label_true = {"t": 0, "p": 0, "c": 0}

    for row in rows:
        t, p, c = row.get("t", "0"), row.get("p", "0"), row.get("c", "0")
        key = f"t{t}_p{p}_c{c}"
        per_combination[key] = per_combination.get(key, 0) + 1
        if t == "1":
            per_label_true["t"] += 1
        if p == "1":
            per_label_true["p"] += 1
        if c == "1":
            per_label_true["c"] += 1

    return {
        "total": len(rows),
        "per_combination": per_combination,
        "per_label_true": per_label_true,
    }
