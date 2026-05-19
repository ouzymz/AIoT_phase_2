# WCO Web Client

Angular 18 single-page dashboard for the WCO contamination-detection system. Drives the ESP32-S3 over HTTP and visualises three deployment scenarios side by side: edge inference, cloud inference, and a multi-model benchmark.

The UI is built around 3 cards, one per scenario. The ESP32's IP address is typed once at the top of the page and persisted in browser storage, so a device IP change does not require a rebuild.

---

## Run locally

```bash
cd web_client
npm install
ng serve
```

Dev server runs at `http://localhost:4200/` with hot reload on source edits.

## Build for production

```bash
ng build
```

Artifacts land in `dist/`.

---

## Project layout

```
web_client/
├── src/
│   ├── app/
│   │   ├── app.component.ts        # Per-card state (idle / loading / success / error)
│   │   ├── app.component.html      # 3-card layout + IP input
│   │   ├── app.component.scss
│   │   ├── app.config.ts
│   │   └── esp.service.ts          # HTTP calls + header parsing
│   ├── index.html
│   ├── main.ts
│   └── styles.scss
├── public/
└── package.json
```

---

## Endpoints consumed

The web client calls into the ESP32 firmware on port 80:

| Card | Endpoint | What it shows |
|---|---|---|
| Edge | `GET /compute` | On-device MobileNet scores (turbidity, particle, colour) + preprocessed JPEG |
| Cloud | `GET /computeCloud?model=<name>` | Single cloud-model prediction with inference latency |
| Benchmark | `GET /computeBenchmark` | All 3 cloud models compared in one shot |

Each response carries the JPEG body and numeric scores as `X-*` headers, so a single round-trip fills the card. `esp.service.ts` reads both the image blob and the headers from the same `HttpResponse<Blob>`.

---

## Notes

- CORS is enabled on every ESP32 handler. The firmware exposes `X-Fill-Percentage`, `X-Turbidity`, `X-Particle`, `X-Colour`, `X-Input-Size`, `X-Inference-Ms`, `X-Model`, and `X-Benchmark-Results` via `Access-Control-Expose-Headers`. Without that header, the browser silently suppresses custom headers from JavaScript.
- The previous image blob is freed before the next render to avoid blob URL leaks.
- Initially generated with Angular CLI 18.2.14. Use `ng help` or the [Angular CLI docs](https://angular.dev/tools/cli) for `ng generate`, `ng test`, and related commands.
