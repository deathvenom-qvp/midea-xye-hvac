#!/usr/bin/env python3
"""
xye_analyze.py - turn an XYE capture file into a command/response catalog.

Input : the capture file written by the Home Assistant package
        (lines of the form  XYE|{json} ; other lines are ignored).
Output: <out>/report.md      human-readable catalog
        <out>/steps.csv      one row per sweep step: what Midea says changed,
                             which reply bytes changed, which commands were seen
        <out>/frames.csv     every captured frame, decoded into columns

Usage : python3 xye_analyze.py capture.log [-o out_dir]

Only the Python 3.8+ standard library is needed. It runs fine in the HA
"Advanced SSH & Web Terminal" add-on:  python3 /config/xye/xye_analyze.py /config/xye/capture.log
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta

# Community byte names for the 32-byte reply, used only as labels.
RESP_NAMES = {
    0: "start", 1: "cmd echo", 2: "dir", 3: "dst", 4: "src", 5: "dst2", 6: "?6",
    7: "capabilities", 8: "mode", 9: "fan", 10: "setpoint", 11: "T1", 12: "T2A",
    13: "T2B", 14: "T3", 15: "current", 16: "?16", 17: "timer start",
    18: "timer stop", 19: "?19", 20: "mode flags", 21: "op flags", 22: "err lo",
    23: "err hi", 24: "prot lo", 25: "prot hi", 26: "CCM err", 27: "?27",
    28: "?28", 29: "?29", 30: "crc", 31: "end",
}
TEMP_BYTES = (11, 12, 13, 14, 30)  # drift on their own (30 = CRC follows them)
CMD_NAMES = {0xC0: "query", 0xC3: "set", 0xC4: "query ext", 0xC6: "follow-me/temp",
             0xCC: "lock", 0xCD: "unlock"}


def parse_time(s: str) -> datetime:
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def load(path: str):
    frames, marks, snaps, climates = [], [], [], []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for ln, line in enumerate(fh, 1):
            i = line.find("XYE|")
            if i < 0:
                continue
            try:
                d = json.loads(line[i + 4:])
            except json.JSONDecodeError:
                print(f"warning: line {ln} is not valid JSON, skipped", file=sys.stderr)
                continue
            d["t"] = parse_time(d["t"])
            k = d.get("kind")
            if k == "frame":
                try:
                    d["b"] = bytes.fromhex(d.get("hex", "").replace(" ", ""))
                except ValueError:
                    continue
                frames.append(d)
            elif k == "mark":
                marks.append(d)
            elif k == "snap":
                snaps.append(d)
            elif k == "climate":
                climates.append(d)
    for lst in (frames, marks, snaps, climates):
        lst.sort(key=lambda x: x["t"])
    return frames, marks, snaps, climates


def is_resp(f) -> bool:
    return f["src"] == "bus" and f.get("crc_ok") and len(f["b"]) == 32


def is_cmd(f) -> bool:
    return f["src"] in ("bus", "esp_tx") and f.get("crc_ok") and len(f["b"]) <= 24


def hx(v: int) -> str:
    return f"0x{v:02X}"


def num(v):
    try:
        x = float(v)
        if x != x:  # NaN
            return None
        return x
    except (TypeError, ValueError):
        return None


def flatten_snap(s) -> dict:
    """Ground-truth features from a Midea snapshot."""
    out = {"hvac_mode": s.get("state")}
    for k, v in (s.get("attrs") or {}).items():
        if isinstance(v, (str, int, float, bool)) or v is None:
            out[f"attr.{k}"] = v
    for k, v in (s.get("entities") or {}).items():
        out[k] = v
    return out


def nearest_resp(resps, t, before=timedelta(seconds=20), after=timedelta(seconds=2)):
    best = None
    for f in resps:
        if f["t"] > t + after:
            break
        if f["t"] >= t - before:
            best = f
    return best


def linfit(xs, ys):
    n = len(xs)
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    a = sxy / sxx
    b = my - a * mx
    ss_tot = sum((y - my) ** 2 for y in ys)
    ss_res = sum((y - (a * x + b)) ** 2 for x, y in zip(xs, ys))
    r2 = 1 - ss_res / ss_tot if ss_tot else 0.0
    return a, b, r2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("-o", "--out", default="xye_report")
    ap.add_argument("--settle", type=float, default=10.0,
                    help="seconds after a step before replies count as steady (default 10)")
    args = ap.parse_args()

    frames, marks, snaps, climates = load(args.capture)
    os.makedirs(args.out, exist_ok=True)
    R = []  # report lines
    w = R.append

    resps = [f for f in frames if is_resp(f)]
    cmds = [f for f in frames if is_cmd(f)]
    foreign = [f for f in cmds if f["src"] == "bus"]

    # ------------------------------------------------------------------ summary
    w("# XYE capture catalog\n")
    if not frames:
        w("No frames in the capture. Check that the ESPHome device may perform HA actions, "
          "that `input_boolean.xye_capture` was on, and that the sniffer's frame counter is rising.\n")
    else:
        span = frames[-1]["t"] - frames[0]["t"]
        w(f"Capture: {frames[0]['t']:%Y-%m-%d %H:%M:%S} to {frames[-1]['t']:%H:%M:%S} "
          f"({span}), {len(frames)} frames, {len(marks)} steps, {len(snaps)} Midea snapshots.\n")
        w("## Frame types\n")
        w("| source | length | type byte | count | meaning |\n|---|---|---|---|---|")
        cnt = Counter((f["src"], len(f["b"]), f["b"][1] if len(f["b"]) > 1 else None) for f in frames)
        for (src, ln, t), c in sorted(cnt.items(), key=lambda kv: -kv[1]):
            meaning = CMD_NAMES.get(t, "") if ln <= 24 else ("reply" if ln == 32 else "")
            w(f"| {src} | {ln} | {hx(t) if t is not None else '-'} | {c} | {meaning} |")
        frag = sum(1 for f in frames if f["src"] == "frag")
        w("")
        if frag:
            w(f"{frag} fragments (bytes that did not form a valid frame). A few at boot are normal; "
              "a steady stream means noise, a wrong baud rate, or two masters colliding.\n")

        w("## Is the thermostat on this bus?\n")
        if foreign:
            kinds = Counter(f["b"][1] for f in foreign)
            kind_txt = ", ".join("%s %s x%d" % (hx(k), CMD_NAMES.get(k, ""), v) for k, v in kinds.items())
            w(f"**Yes.** {len(foreign)} command frames were sent by something other than the ESP "
              f"({kind_txt}). "
              "The per-step tables below list the exact commands the controller sent for each change.\n")
        else:
            w("**No other controller was heard.** Every command on the bus came from the ESP. "
              "The thermostat / Wi-Fi module reaches the unit some other way, so this capture maps the "
              "unit's *replies* (status) but not the commands the thermostat sends. "
              "See the README for the next step.\n")

    # --------------------------------------------------- per-step steady replies
    steps = []
    for i, m in enumerate(marks):
        end = marks[i + 1]["t"] if i + 1 < len(marks) else (frames[-1]["t"] if frames else m["t"])
        settle = min(timedelta(seconds=args.settle), (end - m["t"]) / 2)
        win = [f for f in resps if m["t"] + settle <= f["t"] <= end]
        cwin = [f for f in cmds if m["t"] <= f["t"] <= end and f["src"] == "bus"]
        in_win = [s for s in snaps if m["t"] < s["t"] <= end and s.get("tag") == "steady"
                  and s.get("state") not in ("unavailable", "unknown")]
        snap = in_win[-1] if in_win else None
        steps.append({"mark": m, "end": end, "resp": win[-1] if win else None,
                      "n_resp": len(win), "cmds": cwin, "snap": snap,
                      "unstable": len({bytes(x for i, x in enumerate(f["b"]) if i not in TEMP_BYTES)
                                       for f in win}) > 1})

    w("## Step by step\n")
    w("Each step compares the steady reply at the end of the step with the previous step's. "
      "Temperature bytes (T1/T2/T3) drift on their own, so focus on bytes that move *with* the step.\n")
    prev_b, prev_feat = None, None
    rows = []
    for st in steps:
        label = st["mark"]["step"]
        if label.startswith("run_start"):
            continue
        b = st["resp"]["b"] if st["resp"] else None
        feat = flatten_snap(st["snap"]) if st["snap"] else None
        changed = []
        if b is not None and prev_b is not None:
            changed = [(i, prev_b[i], b[i]) for i in range(30) if prev_b[i] != b[i]]
        midea = []
        if feat and prev_feat:
            for k in sorted(set(feat) | set(prev_feat)):
                if feat.get(k) != prev_feat.get(k):
                    midea.append(f"{k}: {prev_feat.get(k)} -> {feat.get(k)}")
        cmd_hex = sorted({f["hex"] for f in st["cmds"] if f["b"][1] != 0xC0})
        w(f"### {label}\n")
        if b is None:
            w("_no steady reply captured in this step_\n")
        else:
            if changed:
                w("Reply bytes changed: " + ", ".join(
                    f"`b{i}` ({RESP_NAMES.get(i, '?')}) {hx(o)}->{hx(n)}" for i, o, n in changed))
            else:
                w("Reply bytes changed: none")
            if st["unstable"]:
                w(" _(reply was still changing during this step; consider a longer dwell)_")
            w("\n")
        if midea:
            w("Midea reports: " + "; ".join(midea[:12]) + (" ..." if len(midea) > 12 else "") + "\n")
        if cmd_hex:
            w("Commands from another controller:\n")
            for h in cmd_hex:
                w(f"    {h}")
            w("")
        rows.append({
            "step": label,
            "time": st["mark"]["t"].isoformat(),
            "reply_hex": st["resp"]["hex"] if st["resp"] else "",
            "reply_bytes_changed": " ".join(f"b{i}:{o:02X}>{n:02X}" for i, o, n in changed),
            "midea_changes": " | ".join(midea),
            "foreign_commands": " | ".join(cmd_hex),
        })
        if b is not None:
            prev_b = b
        if feat is not None:
            prev_feat = feat

    with open(os.path.join(args.out, "steps.csv"), "w", newline="") as fh:
        cw = csv.DictWriter(fh, fieldnames=list(rows[0].keys()) if rows else ["step"])
        cw.writeheader()
        cw.writerows(rows)

    # ----------------------------------------- pair Midea ground truth to replies
    obs = []  # (features, reply bytes)
    for s in snaps:
        if s.get("state") in ("unavailable", "unknown"):
            continue  # Midea integration dropped out; no ground truth in this snapshot
        r = nearest_resp(resps, s["t"])
        if r:
            obs.append((flatten_snap(s), r["b"], s.get("tag")))
    steady = [(f, b) for f, b, tag in obs if tag == "steady"]

    # Categorical: which reply byte / bit is a pure function of each feature?
    w("## Setting -> reply byte mapping\n")
    w("For each Midea setting, reply bytes whose value is fully determined by that setting "
      "(steady snapshots only). The best candidates are listed first.\n")
    if len(steady) < 3:
        w("_Not enough steady snapshots yet._\n")
    else:
        feats = sorted({k for f, _ in steady for k in f})
        for k in feats:
            vals = [f.get(k) for f, _ in steady]
            distinct = {str(v) for v in vals if v not in (None, "unknown", "unavailable")}
            if len(distinct) < 2 or len(distinct) > 12:
                continue
            numeric = all(num(v) is not None for v in vals if v is not None)
            if numeric and len(distinct) > 4:
                continue  # handled by the numeric fit
            hits = []
            for idx in range(30):
                if idx in TEMP_BYTES:  # temperatures drift; numeric section covers them
                    continue
                m = defaultdict(set)
                for f, b in steady:
                    v = f.get(k)
                    if v in (None, "unknown", "unavailable"):
                        continue
                    m[str(v)].add(b[idx])
                if all(len(s) == 1 for s in m.values()) and len({next(iter(s)) for s in m.values()}) > 1:
                    # how many distinct byte values vs feature values: 1:1 is best
                    score = len({next(iter(s)) for s in m.values()}) / len(m)
                    hits.append((score, idx, {kk: next(iter(vv)) for kk, vv in m.items()}))
            if not hits:
                # Byte-level failed (flag bytes share bits with other settings): try single bits.
                for idx in range(30):
                    if idx in TEMP_BYTES:
                        continue
                    for bit in range(8):
                        m = defaultdict(set)
                        for f, b in steady:
                            v = f.get(k)
                            if v in (None, "unknown", "unavailable"):
                                continue
                            m[str(v)].add((b[idx] >> bit) & 1)
                        if all(len(s) == 1 for s in m.values()) and len({next(iter(s)) for s in m.values()}) > 1:
                            hits.append((0.5, idx, {kk: next(iter(vv)) for kk, vv in m.items()}, bit))
                if not hits:
                    continue
                w(f"**{k}**\n")
                for _, idx, mp, bit in hits[:3]:
                    pairs = ", ".join(f"{kk}={vv}" for kk, vv in sorted(mp.items()))
                    w(f"- `b{idx}` ({RESP_NAMES.get(idx, '?')}) bit {bit} (mask {hx(1 << bit)}): {pairs}")
                w("")
                continue
            hits.sort(key=lambda h: (-h[0], h[1]))
            w(f"**{k}**\n")
            for score, idx, mp in hits[:3]:
                pairs = ", ".join(f"{kk}={hx(vv)}" for kk, vv in sorted(mp.items()))
                bits = ""
                vs = list(mp.values())
                diff = 0
                for v in vs:
                    diff |= v ^ vs[0]
                if diff and bin(diff).count("1") <= 2:
                    bits = f" (only bit mask {hx(diff)} changes)"
                w(f"- `b{idx}` ({RESP_NAMES.get(idx, '?')}): {pairs}{bits}")
            w("")

    # Numeric: linear fits (temperatures, setpoint)
    w("## Numeric fits (temperatures / setpoint)\n")
    w("Linear fit of each Midea number against each reply byte, over every snapshot and climate update. "
      "A good encoding shows R^2 close to 1. If the Midea value is in °F, the °C column gives the encoding.\n")
    samples = list(obs)
    for c in climates:
        r = nearest_resp(resps, c["t"], before=timedelta(seconds=10), after=timedelta(seconds=10))
        if r:
            samples.append(({f"attr.{k}": v for k, v in (c.get("attrs") or {}).items()}, r["b"], "climate"))
    numfeats = defaultdict(list)
    for f, b, _ in samples:
        for k, v in f.items():
            x = num(v)
            if x is not None and not isinstance(v, bool):
                numfeats[k].append((x, b))
    w("| Midea value | best byte | fit | R^2 | n | if °F: °C fit |\n|---|---|---|---|---|---|")
    any_fit = False
    for k, pts in sorted(numfeats.items()):
        if len({x for x, _ in pts}) < 3:
            continue
        best = None
        for idx in range(30):
            raws = [b[idx] for _, b in pts]
            if len(set(raws)) < 3:
                continue
            fit = linfit(raws, [x for x, _ in pts])
            if fit and (best is None or fit[2] > best[1][2]):
                best = (idx, fit)
        if best and best[1][2] > 0.8:
            any_fit = True
            idx, (a, b0, r2) = best
            fitc = linfit([b[idx] for _, b in pts], [(x - 32) * 5 / 9 for x, _ in pts])
            cfit = ""
            if fitc and abs(fitc[0]) > 1e-6:
                ac, bc, _ = fitc
                # express as (raw - offset) / divisor
                cfit = f"°C ≈ (raw - {-bc / ac:.1f}) / {1 / ac:.2f}"
            w(f"| {k} | `b{idx}` ({RESP_NAMES.get(idx, '?')}) | {a:.3f}*raw {b0:+.2f} | {r2:.3f} | {len(pts)} | {cfit} |")
    if not any_fit:
        w("| _no strong fits yet_ | | | | | |")
    w("")

    # Byte activity
    if resps:
        w("## Reply byte activity\n")
        w("| byte | name | distinct values seen | values |\n|---|---|---|---|")
        for idx in range(32):
            vals = Counter(f["b"][idx] for f in resps)
            shown = ", ".join(hx(v) for v, _ in vals.most_common(8)) + (" ..." if len(vals) > 8 else "")
            w(f"| b{idx} | {RESP_NAMES.get(idx, '?')} | {len(vals)} | {shown} |")
        w("")

    with open(os.path.join(args.out, "report.md"), "w") as fh:
        fh.write("\n".join(R) + "\n")

    with open(os.path.join(args.out, "frames.csv"), "w", newline="") as fh:
        cw = csv.writer(fh)
        cw.writerow(["time", "step", "src", "len", "crc_ok", "hex"] + [f"b{i}" for i in range(32)])
        for f in frames:
            cw.writerow([f["t"].isoformat(), f.get("step", ""), f["src"], len(f["b"]), f.get("crc_ok"), f["hex"]]
                        + [f"{x:02X}" for x in f["b"][:32]])

    print(f"wrote {args.out}/report.md, steps.csv, frames.csv "
          f"({len(frames)} frames, {len(marks)} steps, {len(foreign)} foreign commands)")


if __name__ == "__main__":
    main()
