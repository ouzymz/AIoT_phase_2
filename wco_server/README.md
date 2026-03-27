# wco_server — WCO Quality Data Collection Server

FastAPI server for the TinyML Waste Cooking Oil (WCO) quality assessment project.
Receives JPEG images from an ESP32-CAM, auto-labels them using image metrics derived
from a calibration step on clean oil, and logs everything to CSV for model training.

---

## Project structure

```
wco_server/
├── main.py                        # App factory, CORS, lifespan, /health
├── requirements.txt
├── routers/
│   ├── upload.py                  # POST /upload, GET /stats
│   │                              # GET /apply-thresholds, POST /revert-names
│   ├── calibration.py             # POST /calibrate, POST /calibrate/image
│   │                              # GET /calibrate/compute, GET /calibrate/manual
│   │                              # GET /calibration
│   └── validation.py              # POST /validate, GET /validate/report
│                                  # DELETE /validate/reset
├── services/
│   ├── metrics.py                 # michelson_contrast, blob_count, darkening_score
│   ├── calibration.py             # threshold I/O, staging, apply_labels
│   ├── storage.py                 # file save, CSV log, path resolution
│   └── validation.py              # validation CSV log, report aggregation
└── data/
    ├── images/                    # labeled JPEGs saved by /upload
    ├── calibration_staging/       # temporary per-image upload buffer (ESP32 flow)
    ├── images-dotted/
    │   ├── images/                # input set for /apply-thresholds
    │   └── calibration_staging/   # input for /calibrate/manual
    ├── calibration_thresholds.json
    ├── log.csv
    └── validation_log.csv
```

---

## Setup

```bash
cd wco_server
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

On startup the server prints its LAN IP and all endpoint URLs, and reports
calibration status.

---

## API endpoints

### Data collection

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/upload` | Accept one JPEG from ESP32. Auto-label if calibrated, else save as `raw_<ts>.jpg`. |
| `GET`  | `/stats`  | Total count, per-combination counts, per-label positives, calibration status. |

**`POST /upload` response**
```json
{
  "filename": "t0_p0_c1_0007.jpg",
  "size_bytes": 14321,
  "calibrated": true,
  "labels": {"t": 0, "p": 0, "c": 1},
  "scores": {
    "contrast": 0.034,
    "blobs": 3.0,
    "darkening": 0.547,
    "quality": 0.498
  }
}
```

---

### Calibration

Calibration establishes baseline thresholds from **clean fresh oil** images.
Three flows are supported:

#### ESP32 flow (one image at a time)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/calibrate/image` | Stage one JPEG. Returns `{"staged": N, "filename": "..."}`. |
| `GET`  | `/calibrate/compute` | Compute thresholds from all staged images, persist, clear staging. |

Triggered automatically by `GET http://<ESP32-IP>/calibrate?n=20`.

#### Bulk flow (curl / script)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/calibrate` | Upload 10–50 JPEGs in one multipart request. |

```bash
curl -X POST http://localhost:8000/calibrate \
  -F "files=@clean1.jpg" -F "files=@clean2.jpg" ...
```

#### Manual flow (offline, no ESP32)

| Method | Path | Description |
|--------|------|-------------|
| `GET`  | `/calibrate/manual` | Compute thresholds from images in `data/images-dotted/calibration_staging/`. |

Place clean-oil JPEGs in `data/images-dotted/calibration_staging/` then call this endpoint.

#### Status

| Method | Path | Description |
|--------|------|-------------|
| `GET`  | `/calibration` | `{"calibrated": bool, "staged_pending": N, "thresholds": {...}}` |

---

### Offline re-labeling

Apply the current calibration thresholds to an existing image set without the ESP32.

| Method | Path | Description |
|--------|------|-------------|
| `GET`  | `/apply-thresholds` | Re-label all JPEGs in `data/images-dotted/images/`, rename files, replace `log.csv`. |
| `POST` | `/revert-names`     | Rename files back to their original names. |

**`GET /apply-thresholds` response**
```json
{
  "processed": 120,
  "calibrated": true,
  "results": [
    {
      "original": "t1_p1_c1_0121.jpg",
      "new_name": "t0_p0_c1_0118.jpg",
      "labels": {"t": 0, "p": 0, "c": 1},
      "scores": {"contrast": 0.034, "blobs": 4.0, "darkening": 0.547, "quality": 0.498}
    }
  ]
}
```

**`POST /revert-names`** — send the `results` array from `/apply-thresholds` (extra fields ignored):
```json
[
  {"original": "t1_p1_c1_0121.jpg", "new_name": "t0_p0_c1_0118.jpg"}
]
```

Each entry is skipped if `new_name` does not exist or `original` is already taken.

---

### Validation

Validates labeling accuracy against known oil samples. Requires calibration first.

#### ESP32 flow

Triggered automatically by `GET http://<ESP32-IP>/validate?group=<group>&n=3`.

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/validate?group=<g>` | Submit one JPEG with known group. Returns per-label match and scores. |
| `GET`  | `/validate/report` | Aggregated accuracy: overall, per-label (t/p/c), per-group. |
| `DELETE` | `/validate/reset` | Clear `validation_log.csv` and start fresh. |

**Groups and default expectations:**

| group | expected_t | expected_p | expected_c |
|-------|-----------|-----------|-----------|
| `clean` | 0 | 0 | 0 |
| `turbid` | 1 | — | — |
| `turbid_particle` | 1 | 1 | — |

`—` means the label is not evaluated for that group.

**`POST /validate` response**
```json
{
  "group": "turbid",
  "expected": {"t": 1, "p": null, "c": null},
  "actual":   {"t": 1, "p": 0,   "c": 0},
  "scores":   {"contrast": 0.21, "blobs": 2.0, "darkening": 0.43, "quality": 0.61},
  "match":    {"t": true, "p": null, "c": null},
  "correct":  true
}
```

---

### Meta

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/health` | `{"status": "ok", "uptime_seconds": float}` |

---

## Image metrics

All metrics operate on images decoded and resized to **800×600**.
ROI anchor points are derived at runtime by detecting the **red alignment dot** in the image.

| Metric | Function | ROI | Label |
|--------|----------|-----|-------|
| Edge contrast | `michelson_contrast()` | Grid zone: cy±100 rows, cx±100 cols (Sobel + CLAHE + illum. correction) | **t** — turbidity |
| Blob count | `blob_count()` | Circular container mask (r=250), grid rows excluded (SimpleBlobDetector + CLAHE) | **p** — particles |
| Darkening score | `darkening_score()` | Circular container mask (r=250), grid rows excluded (HSV) | **c** — color change |

**ROI detection:** `compute_roi()` finds the red dot centroid and computes:
- Circle centre: `cx = red_x − 310`, `cy = red_y`
- Grid zone: rows `[cy−100, cy+100]`, cols `[cx−100, cx+100]`

**Threshold logic (after calibration on clean oil):**

| Label | Condition | Threshold |
|-------|-----------|-----------|
| `t=1` (turbid) | `relative_contrast > 0.205` | Fixed |
| `p=1` (particles) | `blob_count > 5.0` | Fixed |
| `c=1` (color change) | `darkening > mean + 4σ` | Computed from calibration images |

---

## Filename convention

Labeled images are saved as:

```
t{0|1}_p{0|1}_c{0|1}_{index:04d}.jpg
```

Example: `t1_p0_c1_0042.jpg` → turbid, no particles, color-changed, 42nd image.

Uncalibrated images are saved as `raw_<UTC-timestamp>.jpg` and logged with empty label columns.

---

## log.csv columns

```
timestamp, filename, t, p, c, contrast, blobs, darkening, quality
```
