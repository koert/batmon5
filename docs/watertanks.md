# Water tank level sensors

How to connect the two tank level senders (fresh water and grey water) to the
ES3C28P board.

> **Assumptions, not yet checked:** the senders are resistive float senders
> with a range of **0–190 Ω**, the common European standard. Most of these have
> a float with reed switches and a resistor ladder, so the reading changes in
> steps rather than smoothly. The product page
> (<https://nl.aliexpress.com/item/1005006431297292.html>) couldn't be read
> automatically. Check the range and which end is empty with a multimeter (see
> [Before wiring](#before-wiring)).

## Principle

The ESP32 can't measure resistance directly, but its ADC can measure voltage.
A reference resistor in series with the sender forms a voltage divider, and
the voltage across the sender depends on how far the float has risen.

There is one divider per tank, on the two free ADC1 pins of the expansion
connector:

| Tank        | GPIO | ADC channel    |
|-------------|------|----------------|
| Fresh water | IO2  | ADC1 channel 1 |
| Grey water  | IO3  | ADC1 channel 2 |

Use ADC1, not ADC2. ADC2 (for example on IO14) can't be read while Wi-Fi is
on.

## Circuit (one per tank)

```
3.3V ──[ R_ref 220 Ω ]──┬──[ 4.7 kΩ ]──┬── IO2 (or IO3)
                        │              │
                     sender         100 nF
                    (0–190 Ω)          │
                        │              │
GND ────────────────────┴──────────────┘
```

| Part        | Value            | Purpose                                              |
|-------------|------------------|------------------------------------------------------|
| R_ref       | 220 Ω, ¼ W       | Reference resistor for the voltage divider           |
| Series R    | 4.7 kΩ           | Limits current into the pin and, with the capacitor, filters noise |
| Capacitor   | 100 nF           | Filters noise picked up on the long tank wires        |
| Clamp diode | BAT54S, optional | Clamps spikes to 3.3 V and GND                        |

Take 3.3 V and GND from the board, for example from the IIC connector. It
hasn't been checked whether the 4-pin expansion connector has its own power
pins.

### Why 220 Ω

| Sender | Voltage on the ADC pin |
|--------|------------------------|
| 0 Ω    | 0 V                    |
| 95 Ω   | ≈ 1.00 V               |
| 190 Ω  | ≈ 1.53 V               |

- The voltage stays well inside the ADC's usable range of about 0–3.1 V (with
  12 dB attenuation).
- The most current flows when the sender is at 0 Ω: 3.3 V / 220 Ω = 15 mA,
  with about 50 mW in R_ref. A normal ¼ W resistor is fine.
- A lower R_ref gives a larger voltage swing but more current and heat. A
  higher one saves current but uses less of the ADC's range. 220 Ω is a
  reasonable middle.

## Firmware

1. Read both channels with the ESP-IDF `adc_oneshot` driver at 12 dB
   attenuation, and convert the readings to millivolts with `adc_cali`.
2. Work back from the voltage to the sender resistance:

   ```
   R_sender = R_ref × V / (3.3 − V)        (R_ref = 220 Ω)
   ```

3. Map 0–190 Ω to 0–100 %. Whether 0 Ω means empty or full depends on the
   sender, so check it (see below).
4. Because the output is stepped, averaging a few readings is enough.
5. Pass the levels to the overview screen with
   `OverviewScreen::set_water(fresh_pct, grey_pct)`.

The formula assumes the 3.3 V rail really is 3.3 V. If the readings drift,
measure the rail and put the measured value in the formula.

## Before wiring

1. **Measure each sender.** With a multimeter, measure the resistance with
   the float at the bottom and at the top. This confirms the 0–190 Ω range and
   shows which end is empty. Write down the in-between steps too.
2. **Common ground.** One side of the sender is often connected to the tank or
   the chassis ground. The board's GND must then be connected to the van's
   battery negative. That's usually already the case if the board is powered
   from the 12 V system through a USB adapter or a buck converter. With an
   isolated supply, connect them explicitly.
3. **Existing gauges.** If a sender is already wired to a gauge on a control
   panel, don't add the 220 Ω divider in parallel: both circuits would push
   current through the sender and both readings would be wrong. Either:
   - give the ESP32 its own sender, or
   - leave out R_ref and only measure the voltage the panel already puts
     across the sender. That needs a different calculation, and you have to
     check that the voltage stays below 3.1 V.
