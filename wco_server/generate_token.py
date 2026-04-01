from pathlib import Path
from google_auth_oauthlib.flow import InstalledAppFlow # type: ignore[import-untyped]
import pickle
import os
from dotenv import load_dotenv

load_dotenv(Path(__file__).parent / ".env")

SCOPES = ["https://www.googleapis.com/auth/drive.file"]
CREDENTIALS_PATH = os.environ["GOOGLE_OAUTH_CREDENTIALS_JSON"]
TOKEN_PATH = Path(__file__).parent / "token.pickle"

flow = InstalledAppFlow.from_client_secrets_file(CREDENTIALS_PATH, SCOPES)
creds = flow.run_local_server(port=0)

with open(TOKEN_PATH, "wb") as f:
    pickle.dump(creds, f)

print(f"Token saved to {TOKEN_PATH}")