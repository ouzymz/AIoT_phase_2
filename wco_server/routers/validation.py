from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, File, Form, HTTPException, UploadFile

from services.calibration import apply_labels, is_calibrated, load_thresholds
from services.validation import (
    append_validation_row,
    ensure_validation_csv,
    read_validation_report,
    VALIDATION_CSV,
)

router = APIRouter(tags=["validation"])

# Default expectations per group — None means "don't evaluate this label"
_GROUP_DEFAULTS: dict[str, dict[str, Optional[int]]] = {
    "clean":           {"t": 0, "p": 0,    "c": 0},
    "turbid":          {"t": 1, "p": None,  "c": None},
    "turbid_particle": {"t": 1, "p": 1,    "c": None},
}


@router.post("/validate")
async def validate(
    file: UploadFile = File(...),
    group: str = Form(...),
    expected_t: Optional[int] = Form(default=None),
    expected_p: Optional[int] = Form(default=None),
    expected_c: Optional[int] = Form(default=None),
):
    if group not in _GROUP_DEFAULTS:
        raise HTTPException(
            status_code=422,
            detail=f"group must be one of: {', '.join(_GROUP_DEFAULTS)}",
        )
    if not is_calibrated():
        raise HTTPException(status_code=422, detail="Calibrate before validating.")

    # Fill expected from group defaults where not explicitly supplied
    defaults = _GROUP_DEFAULTS[group]
    exp_t = expected_t if expected_t is not None else defaults["t"]
    exp_p = expected_p if expected_p is not None else defaults["p"]
    exp_c = expected_c if expected_c is not None else defaults["c"]

    content = await file.read()
    thresholds = load_thresholds()
    label_result = apply_labels(content, thresholds)

    act_t = label_result["t"]
    act_p = label_result["p"]
    act_c = label_result["c"]
    scores = label_result["scores"]

    # Per-label match (None if label was not expected / not evaluated)
    def _match(exp: Optional[int], act: int) -> Optional[bool]:
        return None if exp is None else (exp == act)

    match_t = _match(exp_t, act_t)
    match_p = _match(exp_p, act_p)
    match_c = _match(exp_c, act_c)

    evaluated_matches = [m for m in (match_t, match_p, match_c) if m is not None]
    correct = bool(evaluated_matches) and all(evaluated_matches)

    filename = file.filename or f"val_{datetime.now(timezone.utc).strftime('%Y%m%d%H%M%S%f')}.jpg"
    ts = datetime.now(timezone.utc).isoformat()

    append_validation_row({
        "timestamp":  ts,
        "filename":   filename,
        "group":      group,
        "expected_t": "" if exp_t is None else exp_t,
        "expected_p": "" if exp_p is None else exp_p,
        "expected_c": "" if exp_c is None else exp_c,
        "actual_t":   act_t,
        "actual_p":   act_p,
        "actual_c":   act_c,
        "match_t":    "" if match_t is None else match_t,
        "match_p":    "" if match_p is None else match_p,
        "match_c":    "" if match_c is None else match_c,
        "contrast":   scores["contrast"],
        "laplacian":  scores["laplacian"],
        "darkening":  scores["darkening"],
        "quality":    scores["quality"],
    })

    return {
        "group":    group,
        "expected": {"t": exp_t,   "p": exp_p,   "c": exp_c},
        "actual":   {"t": act_t,   "p": act_p,   "c": act_c},
        "scores":   scores,
        "match":    {"t": match_t, "p": match_p, "c": match_c},
        "correct":  correct,
    }


@router.get("/validate/report")
def validate_report():
    return read_validation_report()


@router.delete("/validate/reset")
def validate_reset():
    if VALIDATION_CSV.exists():
        VALIDATION_CSV.unlink()
    ensure_validation_csv()
    return {"message": "Validation log cleared"}
