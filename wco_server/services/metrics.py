import cv2
import numpy as np
 
# ── ROI Constants (800x600 coordinate space) ──────────────────────────────────
#
# Derived from physical image analysis of the WCO container setup.
#
# Container circle (blob detection + HSV clean oil zone):
#   centre (400, 288), safe inner radius 248 px
#
# Reference grid zone (turbidity / FFT frequency power):
#   rows 186-382, cols 297-493  — 3x3 black grid lives here
#
# Blob detection and HSV use the circular container mask with the
# reference grid rows excluded, so the grid pattern does not
# corrupt particle or colour metrics.
 
_IMG_W = 800
_IMG_H = 600
 

CIRCLE_R      = 250
RED_OFFSET    = 310   # kırmızı noktadan sola kaç px
LINE_ROW_HALF = 100   # yukarı / aşağı
LINE_COL_HALF = 100    # sağ / sol

  
def _decode(image_bytes: bytes) -> np.ndarray:
    arr = np.frombuffer(image_bytes, dtype=np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError("Failed to decode image bytes")
    # Ensure consistent 800x600 — handles any resolution from ESP32
    if img.shape[1] != _IMG_W or img.shape[0] != _IMG_H:
        img = cv2.resize(img, (_IMG_W, _IMG_H))
    return img
 
 


def compute_roi(img_bgr):
    """Resimdeki kırmızı noktanın merkez koordinatını döner. Kırmızı noktadan ROI parametrelerini hesaplar."""
    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    
    r = img_rgb[:, :, 0].astype(int)
    g = img_rgb[:, :, 1].astype(int)
    b = img_rgb[:, :, 2].astype(int)
    
    mask = (r > 150) & (g < 80) & (b < 80)
    coords = np.where(mask)

    if len(coords[0]) == 0:
        return None, None

    red_y = int(coords[0].mean())
    red_x = int(coords[1].mean())
    
    cx = red_x - RED_OFFSET
    cy = red_y

    params = {
        "CIRCLE_CX":       cx,
        "CIRCLE_CY":       cy,
        "CIRCLE_R":        CIRCLE_R,
        "LINE_ROW_START":  cy - LINE_ROW_HALF,
        "LINE_ROW_END":    cy + LINE_ROW_HALF,
        "LINE_COL_START":  cx - LINE_COL_HALF,
        "LINE_COL_END":    cx + LINE_COL_HALF,
    }
    
    return params


def _oil_mask(img) -> np.ndarray:
    """
    Circular mask covering the container interior,
    with the reference grid rows zeroed out.
    Used by blob_count and darkening_score.
    """
    roi = compute_roi(img)

    mask = np.zeros((_IMG_H, _IMG_W), dtype=np.uint8)
    cv2.circle(mask, (roi['CIRCLE_CX'], roi['CIRCLE_CY']), roi['CIRCLE_R'], 255, -1)
    mask[roi['LINE_ROW_START']:roi['LINE_ROW_END'], :] = 0
    return mask
 
 
def michelson_contrast(image_bytes, s_ref=None):
    img = _decode(image_bytes)
    gray_u8 = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    gray = gray_u8.astype(float)

    roi_params = compute_roi(img)

    # Statik ROI
    roi_gray = gray[roi_params['LINE_ROW_START']:roi_params['LINE_ROW_END'], roi_params['LINE_COL_START']:roi_params['LINE_COL_END']]
    roi_u8   = gray_u8[roi_params['LINE_ROW_START']:roi_params['LINE_ROW_END'], roi_params['LINE_COL_START']:roi_params['LINE_COL_END']]

    # Particle mask
    roi_mean = roi_u8.mean()
    _, particle_mask = cv2.threshold(roi_u8, int(roi_mean * 0.6), 255, cv2.THRESH_BINARY_INV)
    kernel = np.ones((5,5), np.uint8)
    particle_mask = cv2.morphologyEx(particle_mask, cv2.MORPH_OPEN, kernel)
    valid_mask = (particle_mask == 0)

    # Illumination correction
    background = cv2.GaussianBlur(roi_gray, (51,51), 0)
    roi_normalized = roi_gray / (background + 1e-6)

    # Edge strength
    roi_norm_u8 = np.clip(roi_normalized * 127, 0, 255).astype(np.uint8)
    # CLAHE before Sobel: lifts local contrast in dark/degraded oil so
    # grid edges remain detectable even when the oil is heavily browned.
    # Applied after illumination correction so s_ref comparisons stay
    # consistent (raw score path uses the same normalised base).
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    roi_norm_u8 = clahe.apply(roi_norm_u8)
    sobel_x = cv2.Sobel(roi_norm_u8, cv2.CV_64F, 1, 0, ksize=3)
    sobel_y = cv2.Sobel(roi_norm_u8, cv2.CV_64F, 0, 1, ksize=3)
    edge_mag = np.sqrt(sobel_x**2 + sobel_y**2)
    edge_valid = edge_mag[valid_mask]
    if len(edge_valid) < 100:
        edge_valid = edge_mag.flatten()
    score = float(edge_valid.mean())

    if s_ref is None:
        return score
    return float(1.0 - (score / (s_ref + 1e-6)))
 
 
def blob_count(image_bytes: bytes) -> float:
    """
    Particle metric — counts dark blobs in clean oil zone using
    cv2.SimpleBlobDetector.
 
    CLAHE (Contrast Limited Adaptive Histogram Equalization) is applied
    before detection so that particles remain visible even in dark /
    heavily degraded oil where the global contrast is low.
 
    Returns number of detected blobs (cast to float for consistency).
    Clean oil: 0 blobs.
    Contaminated: 5+ blobs.
 
    ROI: circular container mask, reference grid rows excluded.
    Background outside oil circle set to 255 (white) so detector
    only finds blobs inside container.
    """
    img = _decode(image_bytes)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
 
    # ── CLAHE normalisation ───────────────────────────────────────────
    # clipLimit=2.0  : limits contrast amplification to reduce noise
    # tileGridSize   : 8x8 tiles ≈ 100x75 px each at 800x600 —
    #                  large enough to span a particle cluster, small
    #                  enough to stay local so dark backgrounds are
    #                  lifted independently of bright zones.
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    gray_eq = clahe.apply(gray)
 
    # Apply oil mask — set outside to white so blobs only found inside
    masked = gray_eq.copy()
    mask = _oil_mask(img)
    masked[mask == 0] = 255
 
    params = cv2.SimpleBlobDetector_Params()
    params.filterByColor = True
    params.blobColor = 0           # dark blobs only
    params.filterByArea = True
    params.minArea = 50            # ~7x7 px minimum
    params.maxArea = 8000          # ~90x90 px maximum
    params.filterByCircularity = True
    params.minCircularity = 0.2    # particles are irregular
    params.filterByConvexity = False
    params.filterByInertia = False
 
    detector = cv2.SimpleBlobDetector_create(params)
    keypoints = detector.detect(masked)
    return float(len(keypoints))


def darkening_score(image_bytes: bytes) -> float:
    """
    Colour degradation metric — low V (dark) + high S (saturated) = degraded oil.
    Score near 0 -> fresh, light oil.
    Score near 1 -> dark, heavily degraded oil.
    ROI: circular container mask, reference grid rows excluded.
    """
    img = _decode(image_bytes)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    mask = _oil_mask(img)
    v_pixels = hsv[:, :, 2][mask > 0].astype(float) / 255.0
    s_pixels = hsv[:, :, 1][mask > 0].astype(float) / 255.0
    mean_v = float(v_pixels.mean())
    mean_s = float(s_pixels.mean())
    return (1.0 - mean_v) * 0.7 + mean_s * 0.3