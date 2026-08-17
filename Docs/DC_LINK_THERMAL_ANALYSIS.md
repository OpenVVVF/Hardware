# Thermal Analysis — DC Link Module Standoff Heat Path

Heat load at rated ripple was calculated to be 40W across all capacitors.

## 1. Methodology

All calculations use one-dimensional steady-state thermal resistance:

ΔT = Q × R_th

where

R_th = L / (k × A)

Total system resistance is the sum of three series components:

1. **Standoff conduction** — R_standoff = L / (k_standoff × A_standoff × n)
2. **Contact resistance** (both faces in series) — R_contact = (2 × ρ_contact) / (n × A_standoff)
3. **Aluminium spreading** — R_spread ≈ (ln(r_cell / r_standoff) − 0.5) / (2π × k_Al × t_Al)

### Material properties

| Material | Thermal conductivity k [W/(m·K)] |
|----------|-----------------------------------|
| Aluminium (6063 / generic) | 200 |
| Brass (C36000) | 120 |
| Copper (C11000) | 400 |
| Carbon steel | 50 |
| 18-8 Stainless steel | 16 |

### Contact resistivity values

| Condition | Resistivity ρ_contact [m²·K/W] |
|-----------|--------------------------------|
| Dry metal-to-metal | 1.0 × 10⁻⁴ |
| With thermal paste / thin pad | 5.0 × 10⁻⁵ |

### Geometry constants

- Heat-spreader plate: 3.18 mm (1/8 in) thick aluminium *(corrected from 4 mm to match the fabricated plate, HW-C2-PLT-CHSP-A; all spreading-resistance values below use 3.18 mm)*
- Standoff length: 55 mm (final design)
- Number of standoffs: 6 (final design)
- Standoff spacing: assumed ~100 mm centre-to-centre (spreading cell radius r_cell ≈ 50 mm)

---

## 2. Design Evolution

### 2.1 Initial concepts (for reference)

| Configuration | k [W/m·K] | Area [mm²] | R_standoff [K/W] | ΔT_standoff [°C] | Total ΔT (paste) [°C] |
|-------------|-------------|------------|----------------------|---------------------------|-------------------------------|
| 8 mm hex brass, hollow M5 | 120 | 35.8 | 2.13 | 85.4 | 123 |
| 10 mm hex Al, hollow M5 | 200 | 67.0 | 0.684 | 27.4 | 53.9 |
| 16 mm round Al, hollow M8 | 200 | 150.8 | 0.304 | 12.2 | 29.9 |
| 16 mm round Al, solid | 200 | 201.1 | 0.228 | 9.1 | 25.8 |

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

A = π × (r_outer² − r_inner²) = π × (6.5² − 3.15²) × 10⁻⁶ = 101.6 × 10⁻⁶ m²

---

## 3. Final Design Calculation

### 3.1 Standoff conduction resistance

R_standoff = L / (k_Al × A × n) = 0.055 / (200 × 101.6 × 10⁻⁶ × 6) = 0.451 K/W

ΔT_standoff = 40 × 0.451 = **18.1 °C**

### 3.2 Contact resistance (both faces)

With thermal paste:

R_contact = (2 × 5.0 × 10⁻⁵) / (6 × 101.6 × 10⁻⁶) = 0.164 K/W

ΔT_contact = 40 × 0.164 = **6.6 °C**

Dry metal-to-metal:

R_contact = (2 × 1.0 × 10⁻⁴) / (6 × 101.6 × 10⁻⁶) = 0.328 K/W

ΔT_contact = 40 × 0.328 = **13.1 °C**

### 3.3 Aluminium spreading resistance

R_spread = (ln(50 / 6.5) − 0.5) / (2π × 200 × 0.00318) ≈ 0.385 K/W

ΔT_spread = 40 × 0.385 = **15.4 °C**

*(Spreading resistance is independent of standoff material; it depends only on plate conductivity, thickness, and cell geometry.)*

### 3.4 Total temperature rise

| Condition | ΔT_total |
|-----------|---------------------|
| **With thermal paste** | 18.1 + 6.6 + 15.4 = **40.1 °C** |
| Dry metal-to-metal | 18.1 + 13.1 + 15.4 = **46.6 °C** |

### 3.5 Absolute temperatures (heatsink base = 40 °C)

| Condition | Aluminium plate temperature |
|-----------|----------------------------|
| With thermal paste | **~80 °C** |
| Dry metal-to-metal | **~87 °C** |

**450 V capacitor-only upgrade:** The 450 V upgrade is a single part-number swap to 60&times; Nichicon UCS2W680MHD 68 &micro;F / 450 V capacitors (4.08 mF total). These parts are 5 mm shorter than the 200 V UCS2D331MHD, so the standoff length is reduced by 5 mm (from 55 mm to 50 mm). The same 13 mm OD aluminium standoff thermal path applies, with marginally lower conduction resistance due to the shorter length.

---

## 4. Sensitivity & Margin

### 4.1 Effect of standoff material

If stainless steel (18-8, k = 16 W/m·K) were used instead of aluminium:

ΔT_standoff = 40 × 0.055 / (16 × 101.6 × 10⁻⁶ × 6) ≈ 226 °C

**Total rise would exceed 245 °C.** Stainless steel is **not acceptable** for this thermal path.

### 4.2 Effect of quantity

| Standoff count | R_standoff [K/W] | ΔT_standoff [°C] | Total ΔT (paste) [°C] |
|----------------|----------------------|---------------------------|-------------------------------|
| 4 | 0.677 | 27.1 | 52.3 |
| 6 (selected) | 0.451 | 18.1 | 40 |
| 8 | 0.338 | 13.5 | 33.9 |

Six standoffs provides adequate margin; eight would be better but is not required at 40 W.

### 4.3 Effect of length

| Length [mm] | ΔT_standoff [°C] | Total ΔT (paste) [°C] |
|-------------|---------------------------|-------------------------------|
| 30 | 9.9 | 32 |
| 55 (selected) | 18.1 | 40 |
| 65 | 21.4 | 43 |

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
