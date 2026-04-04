import logging
import socket
import time
import traceback
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response

logging.basicConfig(level=logging.INFO)

from routers import benchmark
from services.model_runner import load_all_models


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
    print("\n  WCO Model Benchmark API")
    print("  Loading models ...")
    load_all_models()
    ip = _local_ip()
    print(f"  Local    : http://{ip}:8001")
    print(f"  Benchmark: POST http://{ip}:8001/benchmark")
    print(f"  Docs     : http://{ip}:8001/docs")
    print()
    yield


app = FastAPI(title="WCO Model Benchmark API", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(benchmark.router)


@app.exception_handler(Exception)
async def unhandled_exception_handler(request: Request, exc: Exception):
    tb = traceback.format_exc()
    logging.error("Unhandled exception:\n%s", tb)
    return JSONResponse(status_code=500, content={"detail": str(exc), "traceback": tb})


@app.get("/favicon.ico", include_in_schema=False)
def favicon():
    return Response(status_code=204)


@app.get("/health", tags=["meta"])
def health():
    return {"status": "ok", "uptime_seconds": round(time.time() - START_TIME, 2)}
