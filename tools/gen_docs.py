#!/usr/bin/env python3
"""Generate packaging/INSTALL.txt and docs/index.html for a logue unit repo.
Reads manifest.json (parameter names and ranges) and docs/unit-docs.json
(hand-written descriptions, wave names, starter sounds in screen values).
Typeless params ("" type) show one higher on the synth screen than the code value.
Run from the repo root: python3 tools/gen_docs.py"""
import json, html, os, sys, textwrap

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
man = json.load(open(os.path.join(root, "manifest.json")))["header"]
doc = json.load(open(os.path.join(root, "docs", "unit-docs.json")))

def screen_range(p):
    name, lo, hi, typ = p
    if typ == "%":
        return f"{lo}% to {hi}%"
    if typ == "":
        return f"{lo + 1} to {hi + 1}"
    return f"{lo} to {hi} {typ}"

rows = []   # (control, screen range, what)
for k in doc.get("knobs", []):
    rows.append((k["name"], k.get("range", ""), k["what"]))
for p in man.get("params", []):
    rows.append((p[0], screen_range(p), doc["params"][p[0]]))
enums = doc.get("enums", {})   # param name -> list of names, shown 1..N
assert man.get("num_param", 0) == len(man.get("params", [])), "param count mismatch"
for k in doc["params"]:
    assert any(p[0] == k for p in man.get("params", [])), "doc param not in manifest: " + k

def wrap(s, ind=""):
    return textwrap.fill(s, 76, initial_indent=ind, subsequent_indent=" " * len(ind) + "  ")

# ---------------- INSTALL.txt
t = []
t.append(f"{doc['name']} {doc['version']}, {doc['device']} {doc['kind']} (Studio 87)")
t.append("")
t.append(wrap(doc["tagline"]))
t.append("")
t.append("WHAT YOU NEED")
t.append(f"  - A KORG {doc['device']} with a USB cable to your computer")
t.append("  - Windows or Mac: the KORG minilogue xd Sound Librarian")
t.append("  - Linux: KORG's logue-cli (see the Linux section, not tested yet)")
t.append("")
t.append("INSTALL (Windows or Mac)")
t.append("  1. Open the zip and keep the " + doc["file"] + " file as it is. It is")
t.append("     itself a zip: do not rename it or open it.")
t.append("  2. Open the Sound Librarian and connect the synth by USB.")
t.append(f"  3. Drag {doc['file']} into a free {doc['slot']} slot.")
t.append("  4. Send to the synth and wait until it finishes.")
t.append(f"  5. On the synth: {doc['where']}.")
t.append("")
t.append("INSTALL (Linux, not tested yet)")
t.append("  The Sound Librarian has no Linux version. KORG's command line tool")
t.append("  logue-cli can load units (64-bit Linux only; get it from the logue-sdk")
t.append("  repo, folder tools/logue-cli, script get_logue_cli_linux.sh).")
t.append(f"  1. logue-cli check -u {doc['file']}")
t.append("  2. logue-cli probe -l")
t.append("     Use the input port ending in KBD/KNOB and the output port ending in")
t.append("     SOUND. The other ports are the old MIDI DIN jacks.")
t.append(f"  3. logue-cli load -u {doc['file']} -i <in port> -o <out port>")
t.append("     (this uses the first free slot)")
t.append("  If load fails on Linux, that is a known issue in KORG's repo (#37).")
t.append("")
t.append("CONTROLS (as the synth screen shows them)")
if doc.get("screen_note"):
    t.append(wrap(doc["screen_note"], "  "))
    t.append("")
for c, r, w in rows:
    t.append(wrap(f"{c}" + (f" [{r}]" if r else "") + ": " + w, "  - "))
for pn, names in enums.items():
    t.append("")
    t.append(f"{pn} values (screen shows 1 to {len(names)}):")
    for i, n in enumerate(names):
        t.append(f"  {i+1:>2}  {n}")
if doc.get("notes"):
    t.append("")
    for n in doc["notes"]:
        t.append(wrap(n, "  "))
if doc.get("starters"):
    t.append("")
    t.append("STARTER SOUNDS")
    for s in doc["starters"]:
        t.append(wrap(s, "  - "))
t.append("")
t.append("PROBLEMS")
t.append(wrap(doc["problems"], "  "))
t.append("")
t.append("STATUS")
for s in doc["status"]:
    t.append(wrap(s, "  - "))
t.append("")
t.append(f"Source and updates: {doc['repo']}")
t.append("Free and open source (MIT). Collects nothing, no network.")
t.append("Not affiliated with or endorsed by KORG. KORG and minilogue are trademarks of")
t.append("KORG Inc., used only to say which synth this works with.")
os.makedirs(os.path.join(root, "packaging"), exist_ok=True)
open(os.path.join(root, "packaging", "INSTALL.txt"), "w").write("\n".join(t) + "\n")

# ---------------- docs/index.html
E = html.escape
h = []
h.append(f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{E(doc['name'])}</title>
<style>
:root{{--bg:#161638;--panel:#1c1c4a;--line:#33403f;--text:#e8e6e1;--muted:#9a9690;--teal:#02EBBA;--pink:#FF1383;--amber:#FFA600;--sky:#6CC9F5}}
*{{box-sizing:border-box}}
body{{margin:0;background:var(--bg);color:var(--text);font:16px/1.55 system-ui,Arial,sans-serif}}
main{{max-width:860px;margin:0 auto;padding:28px 16px 48px}}
h1{{margin:0 0 4px;font-size:34px}} h2{{color:var(--teal);font-size:20px;margin:30px 0 8px}}
a{{color:var(--pink)}} p{{margin:8px 0}} .muted{{color:var(--muted)}}
img.shot{{width:100%;height:auto;border:1px solid var(--line);border-radius:8px;display:block}}
.cap{{color:var(--muted);font-size:13px;margin-top:6px}}
.box{{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 16px;margin:12px 0}}
.box b{{color:var(--amber)}}
table{{width:100%;border-collapse:collapse;margin:8px 0;font-size:15px}}
th,td{{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}}
th{{color:var(--sky);font-weight:600}}
code{{background:#0e0e26;padding:1px 5px;border-radius:4px}}
.links a{{display:inline-block;margin:4px 12px 4px 0}}
.wrap{{overflow-x:auto}}
</style></head><body><main>
<h1>{E(doc['name'])}</h1>
<p class="muted">{E(doc['device'])} {E(doc['kind'])} · version {E(doc['version'])} · Studio 87</p>
<p>{E(doc['tagline'])}</p>
<img class="shot" src="screenshot.png" alt="Illustrated card for {E(doc['name'])}">
<div class="cap">Illustrated card, not a screenshot: this unit has no window, it runs on the synth.</div>
<div class="box"><b>Status.</b><ul>''')
for s in doc["status"]:
    h.append(f"<li>{E(s)}</li>")
h.append("</ul></div>")
h.append("<h2>Install</h2><ol>")
inst = [f"Download the zip from <a href=\"{E(doc['repo'])}/releases\">Releases</a> (or the unit file in <code>dist/</code>). Do not rename or open the <code>{E(doc['file'])}</code> file.",
        "Open the KORG minilogue xd Sound Librarian and connect the synth by USB.",
        f"Drag <code>{E(doc['file'])}</code> into a free <b>{E(doc['slot'])}</b> slot and send it to the synth.",
        f"On the synth: <b>{E(doc['where'])}</b>."]
for s in inst: h.append(f"<li>{s}</li>")
h.append("</ol><p class=\"muted\">Linux (not tested yet): use KORG's <code>logue-cli</code>, steps are in INSTALL.txt inside the zip and in the README.</p>")
h.append("<h2>Controls, as the synth screen shows them</h2>")
if doc.get("screen_note"):
    h.append(f"<p>{E(doc['screen_note'])}</p>")
h.append('<div class="wrap"><table><tr><th>Control</th><th>Screen shows</th><th>What it does</th></tr>')
for c, r, w in rows:
    h.append(f"<tr><td>{E(c)}</td><td>{E(r)}</td><td>{E(w)}</td></tr>")
h.append("</table></div>")
for pn, names in enums.items():
    h.append(f"<h2>{E(pn)} values</h2><div class=\"wrap\"><table><tr><th>Screen</th><th>Value</th></tr>")
    for i, n in enumerate(names):
        h.append(f"<tr><td>{i+1}</td><td>{E(n)}</td></tr>")
    h.append("</table></div>")
for n in doc.get("notes", []):
    h.append(f"<p>{E(n)}</p>")
if doc.get("starters"):
    h.append("<h2>Starter sounds</h2><ul>")
    for s in doc["starters"]:
        h.append(f"<li>{E(s)}</li>")
    h.append("</ul>")
h.append(f"<h2>Problems</h2><p>{E(doc['problems'])}</p>")
h.append(f'<div class="links"><h2>Links</h2><a href="{E(doc["repo"])}">Source on GitHub</a><a href="{E(doc["repo"])}/releases">Releases</a><a href="https://ko-fi.com/studio87">Support Studio 87 on Ko-fi</a></div>')
h.append('<p class="cap">Free and open source (MIT). Collects nothing, no network. Not affiliated with or endorsed by KORG. KORG and minilogue are trademarks of KORG Inc., used only to say which synth this works with.</p></main></body></html>')
open(os.path.join(root, "docs", "index.html"), "w").write("\n".join(h) + "\n")
print("wrote INSTALL.txt and docs/index.html for", doc["name"], "with", len(rows), "rows")
