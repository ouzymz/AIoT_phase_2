from datetime import datetime, timezone
from pathlib import Path
from typing import List

from fastapi import APIRouter, File, HTTPException, UploadFile
from pydantic import BaseModel

from services.calibration import apply_labels, is_calibrated, load_thresholds
from services.storage import append_log, read_stats, reset_log, resolve_path

router = APIRouter(tags=["upload"])

DOTTED_IMAGES_DIR = Path("./data/images-dotted/images")


class RenameEntry(BaseModel):
    original: str
    new_name: str
    labels: dict
    scores: dict | None = None


@router.post("/upload")
async def upload_image(file: UploadFile = File(...)):
    """
    Accept any JPEG. If calibrated, auto-label and name the file;
    otherwise save as raw_{timestamp}.jpg with empty labels.
    """
    content = await file.read()
    calibrated = is_calibrated()

    if calibrated:
        thresholds = load_thresholds()
        label_result = apply_labels(content, thresholds)
        t, p, c = label_result["t"], label_result["p"], label_result["c"]
        scores = label_result["scores"]

        index = read_stats()["total"] + 1
        generated_name = f"t{t}_p{p}_c{c}_{index:04d}.jpg"
        labels = {"t": t, "p": p, "c": c}
    else:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")
        generated_name = f"raw_{ts}.jpg"
        labels = {"t": "", "p": "", "c": ""}
        scores = None

    dest = resolve_path(generated_name)
    dest.write_bytes(content)
    append_log(dest.name, labels, scores) 

    return {
        "filename": dest.name,
        "size_bytes": len(content),
        "calibrated": calibrated,
        "labels": {"t": labels["t"], "p": labels["p"], "c": labels["c"]} if calibrated else None,
        "scores": scores,
    }


@router.get("/apply-thresholds")
async def apply_thresholds():
    """
    Read all images from 'data/images-dotted/images', apply current thresholds
    to each, rename files with new labels, and replace the log file.
    """
    if not DOTTED_IMAGES_DIR.exists():
        return {"error": "Directory not found", "path": str(DOTTED_IMAGES_DIR)}

    image_files = sorted(DOTTED_IMAGES_DIR.glob("*.jpg"))
    print(f"Found {len(image_files)} images in {DOTTED_IMAGES_DIR}")
    if not image_files:
        return {"processed": 0, "message": "No images found"}

    calibrated = is_calibrated()
    thresholds = load_thresholds() if calibrated else None

    reset_log()

    results = []
    for i, img_path in enumerate(image_files, start=1):
        content = img_path.read_bytes()

        if calibrated:
            label_result = apply_labels(content, thresholds)
            t, p, c = label_result["t"], label_result["p"], label_result["c"]
            scores = label_result["scores"]
            new_name = f"t{t}_p{p}_c{c}_{i:04d}.jpg"
            labels = {"t": t, "p": p, "c": c}
        else:
            ts = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")
            new_name = f"raw_{ts}.jpg"
            labels = {"t": "", "p": "", "c": ""}
            scores = None

        new_path = img_path.parent / new_name
        if new_path != img_path:
            img_path.rename(new_path)

        append_log(new_name, labels, scores)
        results.append({
            "original": img_path.name,
            "new_name": new_name,
            "labels": labels,
            "scores": scores,
        })

    return {
        "processed": len(results),
        "calibrated": calibrated,
        "results": results,
    }


@router.post("/revert-names")
async def revert_names(entries: List[RenameEntry]):
    """
    Rename new_name → original for each entry in the posted JSON array.
    Only operates on files inside 'data/images-dotted/images'.
    """
    reverted, skipped = [], []

    for entry in entries:
        src = DOTTED_IMAGES_DIR / entry.new_name
        dst = DOTTED_IMAGES_DIR / entry.original

        if not src.exists():
            skipped.append({"new_name": entry.new_name, "reason": "file not found"})
            continue
        if dst.exists():
            skipped.append({"new_name": entry.new_name, "reason": f"target '{entry.original}' already exists"})
            continue

        src.rename(dst)
        reverted.append({"new_name": entry.new_name, "restored": entry.original})

    return {
        "reverted": len(reverted),
        "skipped": len(skipped),
        "details": reverted,
        "skipped_details": skipped,
    }


@router.get("/stats")
def stats():
    """
    Total image count, per-(t,p,c) combination counts, per-label positives,
    and calibration status.
    """
    data = read_stats()
    data["calibrated"] = is_calibrated()
    return data
