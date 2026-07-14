# Thermal Analysis — DC Link Module Standoff Heat Path

Heat load at rated ripple was calculated to be 40W across all capacitors.

## 1. Methodology

All calculations use one-dimensional steady-state thermal resistance:

$$\Delta T = Q \cdot R_{th}$$

where

$$R_{th} = \frac{L}{k \cdot A}$$

Total system resistance is the sum of three series components:

1. **Standoff conduction** — $R_{standoff} = \frac{L}{k_{standoff} \cdot A_{standoff} \cdot n}$
2. **Contact resistance** (both faces in series) — $R_{contact} = \frac{2 \cdot \rho_{contact}}{n \cdot A_{standoff}}$
3. **Aluminium spreading** — $R_{spread} \approx \frac{\ln(r_{cell}/r_{standoff}) - 0.5}{2\pi k_{Al} t_{Al}}$

### Material properties

| Material | Thermal conductivity $k$ [W/(m·K)] |
|----------|-----------------------------------|
| Aluminium (6063 / generic) | 200 |
| Brass (C36000) | 120 |
| Copper (C11000) | 400 |
| Carbon steel | 50 |
| 18-8 Stainless steel | 16 |

### Contact resistivity values

| Condition | Resistivity $\rho_{contact}$ [m²·K/W] |
|-----------|----------------------------------------|
| Dry metal-to-metal | $1.0 \times 10^{-4}$ |
| With thermal paste / thin pad | $5.0 \times 10^{-5}$ |

### Geometry constants

- Heat-spreader plate: 4 mm thick aluminium
- Standoff length: 55 mm (final design)
- Number of standoffs: 6 (final design)
- Standoff spacing: assumed ~100 mm centre-to-centre (spreading cell radius $r_{cell} \approx 50$ mm)

---

## 2. Design Evolution

### 2.1 Initial concepts (for reference)

| Configuration | $k$ [W/m·K] | Area [mm²] | $R_{standoff}$ [K/W] | $\Delta T_{standoff}$ [°C] | Total $\Delta T$ (paste) [°C] |
|-------------|-------------|------------|----------------------|---------------------------|-------------------------------|
| 8 mm hex brass, hollow M5 | 120 | 35.8 | 2.33 | 93 | 124 |
| 10 mm hex Al, hollow M5 | 200 | 67.0 | 0.75 | 30 | 52 |
| 16 mm round Al, hollow M8 | 200 | 150.8 | 0.29 | 12 | 30 |
| 16 mm round Al, solid | 200 | 201.1 | 0.22 | 9 | 25 |

*All at 40 W, 6 standoffs, 55 mm long. Values rounded.*

### 2.2 Selected design — 13 mm round aluminium spacers

**Final part specification:**
- Outer diameter: 13.0 mm
- Inner diameter (M6 clearance): 6.3 mm  
  *(Note: M6 major diameter = 6.0 mm; 6.3 mm ID provides thread engagement or clearance depending on part type)*
- Wall thickness: 3.35 mm
- Length: 55 mm
- Material: Aluminium
- Thread: M6 × 1 (male-female or through-hole with bolt)
- Quantity: 6

**Cross-sectional area:**

$$A = \pi (r_{outer}^2 - r_{inner}^2) = \pi (6.5^2 - 3.15^2) \times 10^{-6} = 101.6 \times 10^{-6} \text{ m}^2$$

---

## 3. Final Design Calculation

### 3.1 Standoff conduction resistance

$$R_{standoff} = \frac{L}{k_{Al} \cdot A \cdot n} = \frac{0.055}{200 \times 101.6 \times 10^{-6} \times 6} = 0.451 \text{ K/W}$$

$$\Delta T_{standoff} = 40 \times 0.451 = \mathbf{18.1 \text{ °C}}$$

### 3.2 Contact resistance (both faces)

With thermal paste:

$$R_{contact} = \frac{2 \times 5.0 \times 10^{-5}}{6 \times 101.6 \times 10^{-6}} = 0.164 \text{ K/W}$$

$$\Delta T_{contact} = 40 \times 0.164 = \mathbf{6.6 \text{ °C}}$$

Dry metal-to-metal:

$$R_{contact} = \frac{2 \times 1.0 \times 10^{-4}}{6 \times 101.6 \times 10^{-6}} = 0.328 \text{ K/W}$$

$$\Delta T_{contact} = 40 \times 0.328 = \mathbf{13.1 \text{ °C}}$$

### 3.3 Aluminium spreading resistance

$$R_{spread} = \frac{\ln(50/6.5) - 0.5}{2\pi \times 200 \times 0.004} \approx 0.30 \text{ K/W}$$

$$\Delta T_{spread} = 40 \times 0.30 = \mathbf{12.0 \text{ °C}}$$

*(Spreading resistance is independent of standoff material; it depends only on plate conductivity, thickness, and cell geometry.)*

### 3.4 Total temperature rise

| Condition | $\Delta T_{total}$ |
|-----------|---------------------|
| **With thermal paste** | $18.1 + 6.6 + 12.0 = \mathbf{36.7 \text{ °C}}$ |
| Dry metal-to-metal | $18.1 + 13.1 + 12.0 = \mathbf{43.2 \text{ °C}}$ |

### 3.5 Absolute temperatures (heatsink base = 40 °C)

| Condition | Aluminium plate temperature |
|-----------|----------------------------|
| With thermal paste | **~77 °C** |
| Dry metal-to-metal | **~83 °C** |

---

## 4. Sensitivity & Margin

### 4.1 Effect of standoff material

If stainless steel (18-8, $k = 16$ W/m·K) were used instead of aluminium:

$$\Delta T_{standoff} = 40 \times \frac{0.055}{16 \times 101.6 \times 10^{-6} \times 6} \approx 226 \text{ °C}$$

**Total rise would exceed 240 °C.** Stainless steel is **not acceptable** for this thermal path.

### 4.2 Effect of quantity

| Standoff count | $R_{standoff}$ [K/W] | $\Delta T_{standoff}$ [°C] | Total $\Delta T$ (paste) [°C] |
|----------------|----------------------|---------------------------|-------------------------------|
| 4 | 0.677 | 27.1 | 46 |
| 6 (selected) | 0.451 | 18.1 | 37 |
| 8 | 0.338 | 13.5 | 32 |

Six standoffs provides adequate margin; eight would be better but is not required at 40 W.

### 4.3 Effect of length

| Length [mm] | $\Delta T_{standoff}$ [°C] | Total $\Delta T$ (paste) [°C] |
|-------------|---------------------------|-------------------------------|
| 30 | 9.9 | 28 |
| 55 (selected) | 18.1 | 37 |
| 65 | 21.4 | 40 |

---

## 5. Recommendations

1. **Use aluminium standoffs/spacers only.** Do not substitute stainless or carbon steel.
2. **Apply thermal paste** (or a thin graphite / indium thermal pad) at both the plate-to-standoff and standoff-to-heatsink interfaces. This saves ~6.5 °C and improves long-term thermal stability.
3. **Ensure adequate clamping force** on the M6 bolts to minimize contact resistance. Target ~5–10 N·m on steel bolts into aluminium.
4. **Verify standoff placement** is reasonably distributed across the plate. Uneven distribution will increase local spreading resistance and hot-spot temperatures.
5. **If power increases above ~60 W**, consider upgrading to 8 standoffs or thicker-wall spacers (e.g., 16 mm OD / 6 mm ID).

---

## 6. Assumptions & Limitations

- One-dimensional conduction assumed; actual 3D spreading may vary ±20 %.
- Contact resistivity values are typical estimates; actual values depend on surface finish, flatness, and clamping pressure.
- Heat generation is assumed uniform across the aluminium plate. Localised hot spots will increase peak temperatures.
- Radiation and natural convection from the plate are neglected; in reality they provide additional heat rejection, so actual plate temperature may be slightly lower.
- Ambient / heatsink base temperature is assumed constant at 40 °C; if the heatsink warms up under load, the absolute plate temperature rises proportionally.

---

*Prepared for DC link module thermal design review.*
