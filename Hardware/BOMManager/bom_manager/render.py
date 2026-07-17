"""Preview rendering: STEP files -> PNG (cadquery + matplotlib, no GL needed),
and board 3D renders via KiCad CLI (works with flatpak KiCad).
"""

import subprocess
import sys
from pathlib import Path
from typing import Optional

_MATERIAL_COLORS = {
    "copper": "#b87333",
    "brass": "#b5a642",
    "aluminum": "#c6c9cc",
    "aluminium": "#c6c9cc",
    "steel": "#9aa0a6",
    "stainless": "#b8bcc2",
}
_DEFAULT_COLOR = "#7d8590"


def _fresh(out: Path, src: Path) -> bool:
    return out.is_file() and out.stat().st_mtime >= src.stat().st_mtime


def _frame_aspect(verts) -> float:
    """Figure aspect ratio (h/w) tuned to the part's projected shape."""
    import numpy as np
    span = np.maximum(verts.max(axis=0) - verts.min(axis=0), 1e-9)
    w_units = span[0] + span[1] * 0.6
    h_units = span[2] + span[1] * 0.5
    return float(np.clip(h_units / w_units, 0.3, 1.6))


def render_step(step_path: Path, out_png: Path, material: str = "", size=(1200, 900)) -> bool:
    """Render a shaded 3D preview of a STEP file. Pure python (cadquery for
    tessellation, matplotlib for shading) — no GL/display required."""
    if _fresh(out_png, step_path):
        return True
    try:
        import cadquery as cq
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    except ImportError:
        return False

    color = _DEFAULT_COLOR
    for token, c in _MATERIAL_COLORS.items():
        if token in material.lower():
            color = c
            break

    try:
        shape = cq.importers.importStep(str(step_path)).val()
        verts, tris = shape.tessellate(0.1)
        v = np.array([[p.x, p.y, p.z] for p in verts])
        faces = v[np.array(tris)]
    except Exception as e:
        print(f"  STEP render failed for {step_path.name}: {e}", file=sys.stderr)
        return False

    fig = plt.figure(figsize=(8, 8 * _frame_aspect(v)), dpi=180)
    ax = fig.add_subplot(111, projection="3d")
    # Solid look without mesh-seam artifacts: shade each face manually with a
    # directional light and use the same per-face color for edges, so seams
    # are invisible. Orthographic projection: no perspective distortion.
    v0, v1, v2 = faces[:, 0], faces[:, 1], faces[:, 2]
    normals = np.cross(v1 - v0, v2 - v0)
    normals /= np.linalg.norm(normals, axis=1, keepdims=True) + 1e-12
    light = np.array([0.35, -0.45, 0.82])
    light /= np.linalg.norm(light)
    intensity = 0.35 + 0.65 * np.abs(normals @ light)
    import matplotlib.colors as mcolors
    base = np.array(mcolors.to_rgb(color))
    cols = np.clip(base * intensity[:, None], 0, 1)
    pc = Poly3DCollection(faces, facecolors=cols, edgecolors=cols,
                          linewidths=0.1, alpha=1.0, shade=False)
    ax.add_collection3d(pc)
    mins, maxs = v.min(axis=0), v.max(axis=0)
    extent = (maxs - mins).max()
    m = extent * 0.04 + 1e-6
    ax.set_xlim(mins[0] - m, maxs[0] + m)
    ax.set_ylim(mins[1] - m, maxs[1] + m)
    ax.set_zlim(mins[2] - m, maxs[2] + m)
    span = np.maximum(maxs - mins, 1e-9)
    ax.set_box_aspect(tuple(span))
    ax.set_proj_type("ortho")
    ax.view_init(elev=18, azim=-55)
    ax.set_axis_off()
    out_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_png, bbox_inches="tight", pad_inches=0.03, facecolor="#f5f5f5")
    plt.close(fig)
    return True


def render_board_3d(pcb_path: Path, out_png: Path, side: str = "top",
                    width: int = 1400, height: int = 900) -> bool:
    """3D render of a .kicad_pcb via KiCad CLI (flatpak first, PATH fallback)."""
    if _fresh(out_png, pcb_path):
        return True
    out_png.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "flatpak", "run", "--command=kicad-cli", "org.kicad.KiCad",
        "pcb", "render", str(pcb_path),
        "-o", str(out_png), "--width", str(width), "--height", str(height),
        "--side", side, "--background", "transparent",
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except FileNotFoundError:
        cmd[0:4] = ["kicad-cli"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if result.returncode != 0 or not out_png.is_file():
        print(f"  board render failed for {pcb_path.name}: {result.stderr.strip()[:200]}", file=sys.stderr)
        return False
    return True
