import time
from pathlib import Path
from typing import Any

import numpy as np
import psutil
import tensorflow as tf
from PIL import Image
import io

# Model input sizes (height, width)
MODEL_CONFIGS = {
    "EfficientNetB0": {
        "file": "EfficientNetB0.keras",
        "input_size": (224, 224),
    },
    "MobileNetV2": {
        "file": "MobileNetV2_a035.keras",
        "input_size": (160, 160),
    },
    "MobileNetV1": {
        "file": "MobileNetV1_a025.keras",
        "input_size": (128, 128),
    },
}

MODELS_DIR = Path(__file__).parent.parent / "ml_models"

_loaded_models: dict[str, Any] = {}


def load_all_models() -> None:
    for name, cfg in MODEL_CONFIGS.items():
        model_path = MODELS_DIR / cfg["file"]
        if not model_path.exists():
            raise FileNotFoundError(f"Model file not found: {model_path}")
        print(f"  Loading {name} from {model_path.name} ...")
        _loaded_models[name] = tf.keras.models.load_model(str(model_path))
    print(f"  All {len(_loaded_models)} models loaded.")


def _preprocess_image(image_bytes: bytes, target_size: tuple[int, int]) -> np.ndarray:
    """Decode JPEG bytes, resize to target_size, normalize to [0, 1]."""
    img = Image.open(io.BytesIO(image_bytes)).convert("RGB")
    img = img.resize(target_size, Image.BILINEAR)
    arr = np.array(img, dtype=np.float32) / 255.0
    return np.expand_dims(arr, axis=0)  # (1, H, W, 3)


def _ram_mb() -> float:
    process = psutil.Process()
    return process.memory_info().rss / (1024 * 1024)


def _extract_preds(predictions) -> tuple[float, float, float]:
    """
    Handle both output formats:
    - dict  (named outputs): {"turbidity": [[v]], "particle": [[v]], "color": [[v]]}
    - array (single output): shape (1, 3)
    """
    if isinstance(predictions, dict):
        keys = list(predictions.keys())
        turbidity = float(predictions[keys[0]][0][0])
        particle  = float(predictions[keys[1]][0][0])
        color     = float(predictions[keys[2]][0][0])
    else:
        preds = predictions[0].tolist()
        turbidity, particle, color = float(preds[0]), float(preds[1]), float(preds[2])
    return round(turbidity, 4), round(particle, 4), round(color, 4)


def available_models() -> list[str]:
    return list(MODEL_CONFIGS.keys())


def run_single(model_name: str, image_bytes: bytes) -> dict:
    """Run the image through a single model and return metrics + predictions."""
    if model_name not in MODEL_CONFIGS:
        raise ValueError(f"Unknown model '{model_name}'. Available: {list(MODEL_CONFIGS.keys())}")

    model = _loaded_models.get(model_name)
    if model is None:
        raise RuntimeError(f"Model '{model_name}' is not loaded. Call load_all_models() first.")

    cfg = MODEL_CONFIGS[model_name]
    input_size = cfg["input_size"]
    input_tensor = _preprocess_image(image_bytes, input_size)

    ram_before = _ram_mb()
    t_start = time.perf_counter()
    predictions = model.predict(input_tensor, verbose=0)
    inference_ms = (time.perf_counter() - t_start) * 1000
    ram_after = _ram_mb()

    turbidity, particle, color = _extract_preds(predictions)
    return {
        "model": model_name,
        "input_size": f"{input_size[0]}x{input_size[1]}",
        "inference_ms": round(inference_ms, 3),
        "ram_before_mb": round(ram_before, 2),
        "ram_after_mb": round(ram_after, 2),
        "ram_delta_mb": round(ram_after - ram_before, 2),
        "predictions": {
            "turbidity": turbidity,
            "particle":  particle,
            "color":     color,
        },
    }


def run_benchmark(image_bytes: bytes) -> list[dict]:
    """
    Run the image through each model sequentially.
    Returns a list of result dicts, one per model.
    """
    results = []

    for name, cfg in MODEL_CONFIGS.items():
        model = _loaded_models.get(name)
        if model is None:
            raise RuntimeError(f"Model '{name}' is not loaded. Call load_all_models() first.")

        input_size = cfg["input_size"]
        input_tensor = _preprocess_image(image_bytes, input_size)

        # Warm-up: avoid first-call overhead skewing the benchmark
        _ = model.predict(input_tensor, verbose=0)

        ram_before = _ram_mb()
        t_start = time.perf_counter()
        predictions = model.predict(input_tensor, verbose=0)
        inference_ms = (time.perf_counter() - t_start) * 1000
        ram_after = _ram_mb()

        turbidity, particle, color = _extract_preds(predictions)

        results.append({
            "model": name,
            "input_size": f"{input_size[0]}x{input_size[1]}",
            "inference_ms": round(inference_ms, 3),
            "ram_before_mb": round(ram_before, 2),
            "ram_after_mb": round(ram_after, 2),
            "ram_delta_mb": round(ram_after - ram_before, 2),
            "predictions": {
                "turbidity": turbidity,
                "particle": particle,
                "color": color,
            },
        })

    return results
