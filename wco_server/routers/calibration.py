from typing import List

from fastapi import APIRouter, File, HTTPException, UploadFile

from services.calibration import (
    clear_staging,
    compute_thresholds,
    is_calibrated,
    load_thresholds,
    save_thresholds,
    stage_image,
    staged_count,
    staged_images,
    staged_images_test,
)

router = APIRouter(tags=["calibration"])


# ─── Bulk endpoint (programmatic / curl) ─────────────────────────────────────

@router.post("/calibrate")
async def calibrate_bulk(files: List[UploadFile] = File(...)):
    """POST 10–50 clean oil JPEGs in one request to compute and store thresholds."""
    if not (10 <= len(files) <= 50):
        raise HTTPException(
            status_code=422,
            detail=f"Expected 10–50 clean oil images, got {len(files)}.",
        )
    images = [await f.read() for f in files]
    thresholds = compute_thresholds(images)
    save_thresholds(thresholds)
    return thresholds


# ─── One-image-at-a-time endpoints (ESP32 flow) ──────────────────────────────

@router.post("/calibrate/image")
async def calibrate_image(file: UploadFile = File(...)):
    """
    Stage a single calibration JPEG sent by the ESP32.
    Returns the running staged count.
    """
    content = await file.read()
    filename = file.filename or f"calib_{staged_count():03d}.jpg"
    count = stage_image(content, filename)
    return {"staged": count, "filename": filename}


@router.get("/calibrate/compute")
def calibrate_compute():
    """
    Compute thresholds from all staged images, persist them, clear staging.
    Called by the ESP32 after all images have been uploaded.
    """
    images = staged_images()
    if len(images) < 1:
        raise HTTPException(
            status_code=422,
            detail="No staged images found. POST images to /calibrate/image first.",
        )
    thresholds = compute_thresholds(images)
    save_thresholds(thresholds)
    clear_staging()
    return thresholds


# ___ Manual calibration endpoint (for testing without ESP32) ─────────────────────
@router.get("/calibrate/manual")
def calibrate_manual():
    """
    Compute thresholds from all staged images, persist them, clear staging.
    For manual testing without ESP32: POST images to /calibrate/image first.
    """
    image_byte_list = staged_images_test()
    if not image_byte_list:
        raise HTTPException(
            status_code=422,
            detail="No staged images found. POST images to /calibrate/image first.",
        )
    thresholds = compute_thresholds(image_byte_list)
    save_thresholds(thresholds)
    return thresholds

# ─── Status ──────────────────────────────────────────────────────────────────

@router.get("/calibration")
def calibration_status():
    """Return current calibration state and thresholds if available."""
    pending = staged_count()
    if not is_calibrated():
        return {"calibrated": False, "staged_pending": pending}
    return {"calibrated": True, "staged_pending": pending, "thresholds": load_thresholds()}
