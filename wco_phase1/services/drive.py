import io
import os
from functools import lru_cache
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow # type: ignore[import-untyped]
from google.auth.transport.requests import Request
from googleapiclient.discovery import build  # type: ignore[import-untyped]
from googleapiclient.http import MediaIoBaseUpload  # type: ignore[import-untyped]
import pickle
from pathlib import Path

SCOPES = ["https://www.googleapis.com/auth/drive.file"]
TOKEN_PATH = Path(__file__).parent.parent / "token.pickle"
CREDENTIALS_PATH = Path(os.environ["GOOGLE_OAUTH_CREDENTIALS_JSON"])

@lru_cache(maxsize=1)
def _get_drive():
    creds = None
    if TOKEN_PATH.exists():
        with open(TOKEN_PATH, "rb") as f:
            creds = pickle.load(f)
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file(str(CREDENTIALS_PATH), SCOPES)
            creds = flow.run_local_server(port=0)
        with open(TOKEN_PATH, "wb") as f:
            pickle.dump(creds, f)
    return build("drive", "v3", credentials=creds)

def upload_jpeg(data: bytes, filename: str) -> str:
    folder_id = os.environ["GDRIVE_FOLDER_ID"]
    media = MediaIoBaseUpload(io.BytesIO(data), mimetype="image/jpeg")
    file = _get_drive().files().create(
        body={"name": filename, "parents": [folder_id]},
        media_body=media,
        fields="id",
    ).execute()
    return file["id"]
