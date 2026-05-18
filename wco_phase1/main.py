from dotenv import load_dotenv
load_dotenv()  # bunu her şeyden önce çağır

import socket
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response

from routers import  upload



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
    ip = _local_ip()
    print(f"\n  WCO Collection Server")
    print(f"  Local    : http://{ip}:8000")
    print(f"  Upload   : http://{ip}:8000/uploadGoogleDrive")
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



@app.get("/favicon.ico", include_in_schema=False)
def favicon():
    return Response(status_code=204)


@app.get("/health", tags=["meta"])
def health():
    return {"status": "ok", "uptime_seconds": round(time.time() - START_TIME, 2)}
