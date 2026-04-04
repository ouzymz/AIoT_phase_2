import logging
import traceback

from fastapi import APIRouter, File, HTTPException, UploadFile
from pydantic import BaseModel

from services.model_runner import available_models, run_benchmark, run_single

router = APIRouter(tags=["benchmark"])

ALLOWED_CONTENT_TYPES = {"image/jpeg", "image/jpg"}


def _validate_image(file: UploadFile, content: bytes) -> None:
    if not content:
        raise HTTPException(status_code=400, detail="Empty file")
    if file.content_type not in ALLOWED_CONTENT_TYPES:
        raise HTTPException(
            status_code=415,
            detail=f"Unsupported file type '{file.content_type}'. Only JPEG/JPG accepted.",
        )


class ModelResult(BaseModel):
    model: str
    input_size: str
    inference_ms: float
    ram_before_mb: float
    ram_after_mb: float
    ram_delta_mb: float
    predictions: dict


class BenchmarkResponse(BaseModel):
    image_size_bytes: int
    results: list[ModelResult]


@router.post("/benchmark", response_model=BenchmarkResponse)
async def benchmark(file: UploadFile = File(...)):
    """
    Accept a JPEG image (ideally 480x480) and run it through all three models
    sequentially. Returns inference time and RAM usage per model along with
    turbidity, particle, and color predictions.
    """
    content = await file.read()
    _validate_image(file, content)

    try:
        results = run_benchmark(content)
    except Exception as exc:
        logging.error("benchmark error:\n%s", traceback.format_exc())
        raise HTTPException(status_code=500, detail=str(exc))

    return BenchmarkResponse(
        image_size_bytes=len(content),
        results=results,
    )


@router.post("/predict/{model_name}", response_model=ModelResult)
async def predict(model_name: str, file: UploadFile = File(...)):
    """
    Run the image through a single model.
    model_name: EfficientNetB0 | MobileNetV2 | MobileNetV1
    """
    content = await file.read()
    _validate_image(file, content)

    try:
        result = run_single(model_name, content)
    except ValueError as exc:
        raise HTTPException(
            status_code=404,
            detail=str(exc) + f" Available: {available_models()}",
        )
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc))

    return result
