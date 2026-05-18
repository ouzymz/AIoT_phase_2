from pathlib import Path

from fastapi import APIRouter, File, HTTPException, UploadFile, Form
from pydantic import BaseModel

from services.drive import upload_jpeg

router = APIRouter(tags=["upload"])

DOTTED_IMAGES_DIR = Path("./data/images-dotted/images")


class RenameEntry(BaseModel):
    original: str
    new_name: str
    labels: dict
    scores: dict | None = None



@router.post("/uploadGoogleDrive")
async def upload_training_image(
    file: UploadFile = File(...),
    turbidity: int = Form(...),
    particle: int = Form(...),
    color: int = Form(...),
):
    if turbidity not in (0, 1) or particle not in (0, 1) or color not in (0, 1):
        raise HTTPException(status_code=422, detail="turbidity, particle and color must be 0 or 1")

    filename = f"t{turbidity}-p{particle}-c{color}.jpeg"
    upload_jpeg(await file.read(), filename)

    return {"status": "ok", "filename": filename}




