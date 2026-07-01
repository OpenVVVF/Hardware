# Encoder Offset Calibration: What Went Wrong and How It Was Fixed

## Goal

`encoffset` measures the mechanical encoder angle that corresponds to the stator U-high electrical vector.  For a motor with `P` pole pairs the result is only unique modulo `360 / P` degrees, but the value must be **repeatable** regardless of the rotor's starting position.

## The Bug

The encoder ADC computes angle by normalizing the raw sin/cos values against dynamic min/max bounds.  After `resetBounds()` the bounds are invalid, so `EncoderADC::lastAngle()` returns `0` until the rotor has moved enough for the bounds to widen.

We tried to solve this with an `EncoderTracker` that unwrapped encoder movement.  The tracker was reset at the start of the `OFFSET_ROTATE` state, but it **silently refused to track** until the dynamic bounds were stable.  That meant the tracker's "zero" reference was not set at field angle `0`; it was set at whatever field angle happened to exist when the bounds finally became stable (often ~45–60° mechanical).  Because the offset formula is:

```
offset = encoder_mech + sign * field_mech
```

the measured offset became approximately equal to that arbitrary field angle at which tracking started, not the true encoder-field alignment.  This produced results around 58° instead of the true ~13°.

## Why Other Attempts Did Not Work

- **Hard-cap fallback:** Using the fixed hard calibration limits as a fallback gave an angle immediately, but the hard limits do not match the live signal amplitude exactly, so the angle scale was wrong and the result still drifted.
- **Larger warmup / more revolutions:** This made the result more consistent for the *same* starting position, but it did not remove the tracker-reference problem, so moving the rotor still changed the answer.
- **Sign detection:** The sign was actually correct (`-1` for this motor).  The problem was not the sign; it was the reference frame in which encoder movement was measured.

## The Fix

Do **not** rely on a movement tracker for the absolute offset measurement.  Instead:

1. Use the raw absolute encoder angle `encoderADC().lastAngle()` directly (it is 0–360° and available once bounds are stable).
2. Compare it to the continuous, commanded field mechanical angle from `fieldMechanicalAngle()`.
3. Unwrap the raw encoder reading against the field angle using the known pole-pair period.
4. Drive acquisition start and rotation completion from the **field angle**, which is always known and continuous, not from the encoder tracker.

The relevant code is in:

- `Src/Inverter/Calibration/EncoderOffsetCalibrator.cpp`
- `Src/Inverter/Calibration/EncoderOffsetCalibrator.h`

Key pieces:

- `accumulateOffsetSample()` uses `encoderADC().lastAngle()` and unwraps against `fieldMechanicalAngle()`.
- `OFFSET_ROTATE` starts acquisition when the field has moved `OFFSET_ACQUIRE_START_DEG` and finishes when it has moved `OFFSET_ROTATE_REVS * 360` degrees.
- The `EncoderTracker` is still updated for diagnostics, but it is no longer used to decide acquisition timing or to compute the offset.

## Lessons

- An encoder movement tracker can only measure **relative** movement.  Do not use it as an absolute reference for an alignment measurement.
- If an encoder angle is invalid at the start of a calibration, either:
  - use the raw absolute angle once it becomes valid and unwrap it against a known continuous reference (the field angle), or
  - hold the calibration until the angle is valid and use that instant as the reference for both encoder and field.
- The field angle is the most reliable reference during open-loop rotation because it is commanded and continuous.
