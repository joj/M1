<!-- See COPYING.txt for license details. -->

# Sub-GHz Validate

A diagnostic for "is this remote the same one I recorded earlier?".
Pick a saved capture, press the remote at the M1, and the M1 tells you
whether the press matches the recording (and if it does, whether it's
a fixed-code remote that can be replay-attacked, a rolling-code remote
that's secure, or a sibling remote from the same chip family).

## How to use

1. *Menu → Sub-GHz → Record* — capture the remote you want to validate
   later. If the M1's decoder recognises the protocol during record,
   a tiny `.sgv` sidecar file is written next to the `.sgh` recording
   on the SD card. (If the decoder didn't recognise the protocol, no
   `.sgv` is written — there's no protocol-aware comparison available
   in v1 for raw-only captures.)
2. *Menu → Sub-GHz → Validate* — file browser opens. Pick the `.sgv`
   for the remote you want to test.
3. The M1 sets the radio to the saved frequency + modulation and
   enters RX. Screen reads "Press the remote..."
4. Press the remote. The M1 captures one decoded packet, then shows a
   verdict.
5. Times out after 30 s if nothing arrives. BACK aborts at any point.

## Verdicts

| Verdict                          | Meaning                                                                                                    |
|----------------------------------|------------------------------------------------------------------------------------------------------------|
| **MATCH (identical)**            | Bit-for-bit equal. For a **fixed-code** remote this is normal; the remote is replay-attack vulnerable. For a **rolling-code** remote (serial != 0) this is suspicious — usually means an actual replay attack is in progress (saved frame being re-emitted). The screen flags this with **REPLAY RISK**. |
| **MATCH (rolling, counter +N)**  | Same protocol + same serial number; rolling counter advanced by N. The remote is the same physical unit and rolling-code security is working. N is the forward delta (a small number per button press, typically 1–4). |
| **MATCH (same family)**          | Same protocol, different serial. Could be a sibling remote you also own, or a different device using the same chip (Security+ 2.0, etc.) — informational, not a security concern. |
| **NO MATCH**                     | Different protocol, or fixed-code key bits differ. The remote you pressed is not the one saved. |

## When a verdict is *not* shown

- The M1 didn't receive any packet within the 30 s window.
- The packet arrived but didn't decode as a known protocol.
- The `.sgv` file was missing or malformed.

In all those cases the screen says "Timed out." or "Bad .sgv file" and
you can press BACK to return.

## Files on the SD card

```
/SUBGHZ/sghz_433_OOK_001.sgh      <-- raw pulse capture (Record/Replay)
/SUBGHZ/sghz_433_OOK_001.sgv      <-- decoded metadata (Validate)
```

The `.sgv` is a tiny (~150 byte) UTF-8 text file:

```
Filetype: M1 SubGHz Validate
Version: 1
Frequency: 433920000
Modulation: OOK
Protocol: 1
BitLen: 56
Key: 0xCAFEBABE
Serial: 0x00ABCDEF
Rolling: 0x12345678
Button: 3
```

Hand-editing is supported but unnecessary.

## Limitations

- Rolling counter advance is read modulo 2^32. We treat an
  implausibly-large forward jump (≥ 2^28) as a serial collision and
  report **FAMILY** rather than ROLLING.
- A backward counter (fresh < saved) is reported as **IDENTICAL**
  with the REPLAY RISK flag — there is no honest forward press that
  produces a backward counter.
- Raw-only captures (no decoded protocol) cannot be Validated in v1.
  This would require a pulse-edit-distance compare which is noisy and
  out of scope; for now Validate only works on captures where the
  Record screen also produced a decode.

## Source map

| File                              | Role                                                  |
|-----------------------------------|-------------------------------------------------------|
| `Sub_Ghz/m1_sub_ghz_validate.{c,h}` | Verdict engine. Pure logic, host-testable.            |
| `Sub_Ghz/m1_sub_ghz_sgv.{c,h}`      | `.sgv` (de)serialiser + FatFs I/O.                    |
| `m1_csrc/m1_sub_ghz.c::sub_ghz_validate()` | Procedural UI loop (file browse → RX → verdict).   |
| `m1_csrc/m1_menu.c`                | Menu entry under Sub-GHz.                             |
| `scripts/test_subghz_validate.c`   | 26 host tests covering verdict logic + `.sgv` round-trip. |
