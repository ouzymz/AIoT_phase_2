# WCO Model Benchmark API

FastAPI tabanlı su kalitesi (WCO) model benchmark API'si. 480x480 JPEG görüntüyü alır, üç farklı `.keras` modeline uygun boyuta resize ederek çalıştırır; inference süresi ve RAM kullanımı ölçer.

## Modeller

| Model | Input Size | Dosya |
|---|---|---|
| EfficientNetB0 | 224×224 | `ml_models/EfficientNetB0.keras` |
| MobileNetV2 | 160×160 | `ml_models/MobileNetV2_a035.keras` |
| MobileNetV1 | 128×128 | `ml_models/MobileNetV1_a025.keras` |

Her model çıktısı üç sigmoid olasılığı döner: **turbidity**, **particle**, **color**.

## Kurulum

```bash
cd wco_api
pip install -r requirements.txt
```

## Çalıştırma

```bash
uvicorn main:app --host 0.0.0.0 --port 8001
```

Swagger UI: `http://localhost:8001/docs`

## Endpointler

### `POST /predict/{model_name}`

Tek bir modeli çalıştırır.

**Path parametresi:** `EfficientNetB0` | `MobileNetV2` | `MobileNetV1`

**Body:** `multipart/form-data` — `file`: JPEG görüntü

**Response:**
```json
{
  "model": "EfficientNetB0",
  "input_size": "224x224",
  "inference_ms": 38.452,
  "ram_before_mb": 312.5,
  "ram_after_mb": 315.1,
  "ram_delta_mb": 2.6,
  "predictions": {
    "turbidity": 0.8213,
    "particle": 0.1047,
    "color": 0.6731
  }
}
```

---

### `POST /benchmark`

Üç modeli sırayla çalıştırır; her biri için metrik döner. Warm-up predict çalıştırılır.

**Body:** `multipart/form-data` — `file`: JPEG görüntü

**Response:**
```json
{
  "image_size_bytes": 57432,
  "results": [
    {
      "model": "EfficientNetB0",
      "input_size": "224x224",
      "inference_ms": 42.317,
      "ram_before_mb": 312.5,
      "ram_after_mb": 315.2,
      "ram_delta_mb": 2.7,
      "predictions": {
        "turbidity": 0.8213,
        "particle": 0.1047,
        "color": 0.6731
      }
    },
    { "model": "MobileNetV2", "..." : "..." },
    { "model": "MobileNetV1", "..." : "..." }
  ]
}
```

---

### `GET /health`

```json
{ "status": "ok", "uptime_seconds": 124.3 }
```

## Proje Yapısı

```
wco_api/
├── main.py                  # FastAPI app, lifespan, port 8001
├── requirements.txt
├── ml_models/
│   ├── EfficientNetB0.keras
│   ├── MobileNetV2_a035.keras
│   └── MobileNetV1_a025.keras
├── routers/
│   └── benchmark.py         # /predict/{model_name}, /benchmark
└── services/
    └── model_runner.py      # Model yükleme, resize, ölçüm
```

## Metrikler

| Metrik | Açıklama |
|---|---|
| `inference_ms` | Model `.predict()` süresi (milisaniye) |
| `ram_before_mb` | Predict öncesi process RSS belleği (MB) |
| `ram_after_mb` | Predict sonrası process RSS belleği (MB) |
| `ram_delta_mb` | Predict sırasında oluşan bellek artışı (MB) |
