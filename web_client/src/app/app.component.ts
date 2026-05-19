import { Component, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { HttpErrorResponse } from '@angular/common/http';

import { MatToolbarModule } from '@angular/material/toolbar';
import { MatCardModule } from '@angular/material/card';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { MatIconModule } from '@angular/material/icon';
import { MatDividerModule } from '@angular/material/divider';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';

import {
  EspService,
  EdgeResult,
  CloudResult,
  BenchmarkResult,
} from './esp.service';

const IP_STORAGE_KEY = 'esp32_ip';
const CLOUD_MODELS = ['EfficientNetB0', 'MobileNetV2', 'MobileNetV1'];

type Status = 'idle' | 'loading' | 'success' | 'error';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [
    CommonModule,
    FormsModule,
    MatToolbarModule,
    MatCardModule,
    MatButtonModule,
    MatFormFieldModule,
    MatInputModule,
    MatSelectModule,
    MatProgressSpinnerModule,
    MatIconModule,
    MatDividerModule,
    MatTableModule,
    MatTooltipModule,
  ],
  templateUrl: './app.component.html',
  styleUrl: './app.component.scss',
})
export class AppComponent {
  esp32Ip = signal<string>(
    localStorage.getItem(IP_STORAGE_KEY) ?? '192.168.1.75',
  );

  readonly cloudModels = CLOUD_MODELS;
  selectedModel = signal<string>(CLOUD_MODELS[0]);

  edgeStatus = signal<Status>('idle');
  edgeResult = signal<EdgeResult | null>(null);
  edgeError = signal<string>('');

  benchStatus = signal<Status>('idle');
  benchResult = signal<BenchmarkResult | null>(null);
  benchError = signal<string>('');

  cloudStatus = signal<Status>('idle');
  cloudResult = signal<CloudResult | null>(null);
  cloudError = signal<string>('');

  readonly benchColumns = [
    'model',
    'input_size',
    'inference_ms',
    'ram_delta_mb',
    'turbidity',
    'particle',
    'color',
  ];

  constructor(private esp: EspService) {}

  display(value: number | string | null | undefined, suffix = ''): string {
    if (value === null || value === undefined || value === '') return 'N/A';
    return `${value}${suffix}`;
  }

  onIpChange(value: string) {
    this.esp32Ip.set(value);
    localStorage.setItem(IP_STORAGE_KEY, value);
  }

  private errorMessage(err: unknown): string {
    if (err instanceof HttpErrorResponse) {
      if (err.status === 0) return 'Cannot reach ESP32 (network / CORS error).';
      return `HTTP ${err.status} ${err.statusText || ''}`.trim();
    }
    return String(err);
  }

  private revoke(url?: string) {
    if (url) URL.revokeObjectURL(url);
  }

  runEdge() {
    this.revoke(this.edgeResult()?.imageUrl);
    this.edgeStatus.set('loading');
    this.edgeError.set('');
    this.edgeResult.set(null);
    this.esp.compute(this.esp32Ip()).subscribe({
      next: (r) => {
        this.edgeResult.set(r);
        this.edgeStatus.set('success');
      },
      error: (e) => {
        this.edgeError.set(this.errorMessage(e));
        this.edgeStatus.set('error');
      },
    });
  }

  runBenchmark() {
    this.revoke(this.benchResult()?.imageUrl);
    this.benchStatus.set('loading');
    this.benchError.set('');
    this.benchResult.set(null);
    this.esp.computeBenchmark(this.esp32Ip()).subscribe({
      next: (r) => {
        this.benchResult.set(r);
        this.benchStatus.set('success');
      },
      error: (e) => {
        this.benchError.set(this.errorMessage(e));
        this.benchStatus.set('error');
      },
    });
  }

  runCloud() {
    this.revoke(this.cloudResult()?.imageUrl);
    this.cloudStatus.set('loading');
    this.cloudError.set('');
    this.cloudResult.set(null);
    this.esp.computeCloud(this.esp32Ip(), this.selectedModel()).subscribe({
      next: (r) => {
        this.cloudResult.set(r);
        this.cloudStatus.set('success');
      },
      error: (e) => {
        this.cloudError.set(this.errorMessage(e));
        this.cloudStatus.set('error');
      },
    });
  }
}
