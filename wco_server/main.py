import socket
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response

from routers import calibration, upload, validation
from services.calibration import ensure_staging, is_calibrated, staged_count
from services.storage import ensure_dirs
from services.validation import ensure_validation_csv

START_TIME = time.time()


def _local_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


@asynccontextmanager
async def lifespan(_app: FastAPI):
    ensure_dirs()
    ensure_staging()
    ensure_validation_csv()
    ip = _local_ip()
    print(f"\n  WCO Collection Server")
    print(f"  Local    : http://{ip}:8000")
    print(f"  Upload   : http://{ip}:8000/upload")
    print(f"  Stats    : http://{ip}:8000/stats")
    print(f"  Calibrate: http://{ip}:8000/calibrate/image  (ESP32 per-image)")
    print(f"  Compute  : http://{ip}:8000/calibrate/compute")
    print(f"  Calibrate: http://{ip}:8000/calibrate/manual  (Manual calibration)")
    print(f"  Validate : http://{ip}:8000/validate")
    if is_calibrated():
        print("  [OK] Calibration loaded")
    else:
        pending = staged_count()
        if pending:
            print(f"  [!!] Not calibrated — {pending} images staged, GET /calibrate/compute when ready")
        else:
            print("  [!!] Not calibrated — send clean oil images via GET /calibrate?n=20 on ESP32")
    print()
    yield


app = FastAPI(title="WCO Data Collection Server", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(upload.router)
app.include_router(calibration.router)
app.include_router(validation.router)


@app.get("/favicon.ico", include_in_schema=False)
def favicon():
    return Response(status_code=204)


@app.get("/health", tags=["meta"])
def health():
    return {"status": "ok", "uptime_seconds": round(time.time() - START_TIME, 2)}
