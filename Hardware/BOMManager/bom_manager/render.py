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

    fig = plt.figure(figsize=(size[0] / 150, size[1] / 150), dpi=150)
    ax = fig.add_subplot(111, projection="3d")
    import matplotlib.colors as mcolors
    r, g, b = mcolors.to_rgb(color)
    edge = (r * 0.6, g * 0.6, b * 0.6)
    pc = Poly3DCollection(faces, facecolors=color, edgecolors=[edge],
                          linewidths=0.05, alpha=1.0, shade=True)
    ax.add_collection3d(pc)
    mins, maxs = v.min(axis=0), v.max(axis=0)
    extent = (maxs - mins).max()
    m = extent * 0.08 + 1e-6
    ax.set_xlim(mins[0] - m, maxs[0] + m)
    ax.set_ylim(mins[1] - m, maxs[1] + m)
    ax.set_zlim(mins[2] - m, maxs[2] + m)
    span = maxs - mins
    ax.set_box_aspect(tuple(np.maximum(span, extent * 0.05)))
    ax.view_init(elev=25, azim=-55)
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
