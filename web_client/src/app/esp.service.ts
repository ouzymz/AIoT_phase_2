import { HttpClient, HttpResponse } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable, map } from 'rxjs';

export interface EdgeResult {
  imageUrl: string;
  fillPercentage: number | null;
  inputSize: number | null;
  turbidity: number | null;
  particle: number | null;
  colour: number | null;
}

export interface CloudResult {
  imageUrl: string;
  fillPercentage: number | null;
  model: string | null;
  inputSize: string | null;
  inferenceMs: number | null;
  turbidity: number | null;
  particle: number | null;
  colour: number | null;
}

export interface BenchmarkModelResult {
  model: string;
  input_size: string;
  inference_ms: number;
  ram_before_mb: number;
  ram_after_mb: number;
  ram_delta_mb: number;
  predictions: { turbidity: number; particle: number; color: number };
}

export interface BenchmarkResult {
  imageUrl: string;
  fillPercentage: number | null;
  imageSizeBytes: number | null;
  results: BenchmarkModelResult[];
}

@Injectable({ providedIn: 'root' })
export class EspService {
  constructor(private http: HttpClient) {}

  private baseUrl(esp32Ip: string): string {
    const trimmed = esp32Ip.trim().replace(/\/$/, '');
    if (/^https?:\/\//.test(trimmed)) return trimmed;
    return `http://${trimmed}`;
  }

  private toNum(v: string | null): number | null {
    if (v === null || v === '') return null;
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  }

  private blobUrl(body: Blob | null): string {
    if (!body) return '';
    return URL.createObjectURL(body);
  }

  compute(esp32Ip: string): Observable<EdgeResult> {
    const url = `${this.baseUrl(esp32Ip)}/compute`;
    return this.http
      .get(url, { observe: 'response', responseType: 'blob' })
      .pipe(
        map((res: HttpResponse<Blob>) => ({
          imageUrl: this.blobUrl(res.body),
          fillPercentage: this.toNum(res.headers.get('X-Fill-Percentage')),
          inputSize: this.toNum(res.headers.get('X-Input-Size')),
          turbidity: this.toNum(res.headers.get('X-Turbidity')),
          particle: this.toNum(res.headers.get('X-Particle')),
          colour: this.toNum(res.headers.get('X-Colour')),
        })),
      );
  }

  computeCloud(esp32Ip: string, model: string, size = 480): Observable<CloudResult> {
    const url = `${this.baseUrl(esp32Ip)}/computeCloud?model=${encodeURIComponent(model)}&size=${size}`;
    return this.http
      .get(url, { observe: 'response', responseType: 'blob' })
      .pipe(
        map((res: HttpResponse<Blob>) => ({
          imageUrl: this.blobUrl(res.body),
          fillPercentage: this.toNum(res.headers.get('X-Fill-Percentage')),
          model: res.headers.get('X-Model'),
          inputSize: res.headers.get('X-Input-Size'),
          inferenceMs: this.toNum(res.headers.get('X-Inference-Ms')),
          turbidity: this.toNum(res.headers.get('X-Turbidity')),
          particle: this.toNum(res.headers.get('X-Particle')),
          colour: this.toNum(res.headers.get('X-Colour')),
        })),
      );
  }

  computeBenchmark(esp32Ip: string, size = 480): Observable<BenchmarkResult> {
    const url = `${this.baseUrl(esp32Ip)}/computeBenchmark?size=${size}`;
    return this.http
      .get(url, { observe: 'response', responseType: 'blob' })
      .pipe(
        map((res: HttpResponse<Blob>) => {
          const raw = res.headers.get('X-Benchmark-Results');
          let imageSizeBytes: number | null = null;
          let results: BenchmarkModelResult[] = [];
          if (raw) {
            try {
              const parsed = JSON.parse(raw);
              imageSizeBytes = parsed.image_size_bytes ?? null;
              results = parsed.results ?? [];
            } catch (e) {
              console.error('Failed to parse X-Benchmark-Results header', e, raw);
            }
          }
          return {
            imageUrl: this.blobUrl(res.body),
            fillPercentage: this.toNum(res.headers.get('X-Fill-Percentage')),
            imageSizeBytes,
            results,
          };
        }),
      );
  }
}
