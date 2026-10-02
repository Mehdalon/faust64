#!/usr/bin/env python3
"""faust64 audio lab - a local page for auditioning Faust DSPs the way the N64
will play them, and building the ROM once they sound right.

Stdlib only, binds to localhost. Start it with:  lab/labserver.py
"""
import http.server, json, os, re, shutil, socketserver, subprocess, sys, tempfile, urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.path.join(ROOT, "lab")
PORT = int(os.environ.get("FAUSTLAB_PORT", "8791"))
RENDERS = os.path.join(ROOT, "build", "lab-renders")


def dsp_files():
    """Every .dsp under the project, newest first, as paths relative to ROOT."""
    out = []
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in ("build", ".git")]
        for f in files:
            if f.endswith(".dsp"):
                p = os.path.join(base, f)
                out.append((os.path.getmtime(p), os.path.relpath(p, ROOT)))
    return [p for _, p in sorted(out, reverse=True)]


def safe_dsp(rel):
    """Reject anything that is not a .dsp inside the project."""
    p = os.path.normpath(os.path.join(ROOT, rel))
    if not p.startswith(ROOT + os.sep) or not p.endswith(".dsp") or not os.path.isfile(p):
        raise ValueError("bad dsp path: %r" % rel)
    return p


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


CTRL_RE = re.compile(r"(\w+)=(-?[\d.eE+]+)")


def controls(rel):
    """Ask faustlab to enumerate the controls, and faust to give us the ranges."""
    r = run([os.path.join(LAB, "faustlab"), "-l", rel])
    if r.returncode != 0:
        return {"error": (r.stderr or r.stdout).strip()[:2000]}
    vals = dict((k, float(v)) for k, v in CTRL_RE.findall(r.stdout.replace("controls:", "")))
    # Ranges come from the .dsp source: faust does not print them in -l output.
    ranges = {}
    src = open(safe_dsp(rel)).read()
    for m in re.finditer(r'\b[nvh](?:slider|bargraph|entry)\s*\(\s*"([^"]+)"\s*,'
                         r'\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)\s*,'
                         r'\s*([-\d.eE+]+)\s*\)', src):
        label, init, lo, hi, step = m.groups()
        label = label.split("[")[0].strip()
        ranges[label] = {"init": float(init), "lo": float(lo),
                         "hi": float(hi), "step": float(step)}
    out = []
    for k, v in vals.items():
        rng = ranges.get(k, {"init": v, "lo": 0.0, "hi": 1.0, "step": 0.01})
        out.append(dict(name=k, value=v, **rng))
    out.sort(key=lambda c: c["name"])
    return {"controls": out}


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *a):
        sys.stderr.write("lab: " + fmt % a + "\n")

    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        try:
            if u.path in ("/", "/index.html"):
                return self._file(os.path.join(LAB, "index.html"), "text/html")
            if u.path == "/api/dsps":
                return self._json({"dsps": dsp_files()})
            if u.path == "/api/source":
                return self._json({"src": open(safe_dsp(q["dsp"][0])).read()})
            if u.path == "/api/controls":
                return self._json(controls(q["dsp"][0]))
            if u.path.startswith("/render/"):
                name = os.path.basename(u.path)
                p = os.path.join(RENDERS, name)
                if os.path.isfile(p):
                    return self._file(p, "audio/wav")
                return self._json({"error": "gone"}, 404)
        except Exception as e:
            return self._json({"error": str(e)}, 400)
        self._json({"error": "not found"}, 404)

    def _file(self, path, ctype):
        data = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        n = int(self.headers.get("Content-Length", 0))
        try:
            req = json.loads(self.rfile.read(n) or b"{}")
            rel = req["dsp"]
            safe_dsp(rel)
            if u.path == "/api/render":
                return self._json(self._render(req, rel))
            if u.path == "/api/rom":
                return self._json(self._rom(req, rel))
            if u.path == "/api/preset":
                return self._json(self._preset(req, rel))
        except Exception as e:
            return self._json({"error": str(e)}, 400)
        self._json({"error": "not found"}, 404)

    def _render(self, req, rel):
        os.makedirs(RENDERS, exist_ok=True)
        sr = int(req.get("sr", 22050))
        secs = float(req.get("secs", 6))
        ident = re.sub(r"\W", "_", rel)
        wav = os.path.join(RENDERS, ident + ".wav")
        cmd = [os.path.join(LAB, "faustlab"), "-n", "-r", str(sr),
               "-d", str(secs), "-o", wav]
        for k, v in (req.get("params") or {}).items():
            cmd += ["-p", "%s=%s" % (k, v)]
        for k, v in (req.get("sweeps") or {}).items():
            cmd += ["-s", "%s=%s:%s" % (k, v[0], v[1])]
        cmd.append(rel)
        r = run(cmd)
        if r.returncode != 0 or not os.path.isfile(wav):
            return {"error": (r.stderr or r.stdout).strip()[:4000]}
        stats = ""
        for line in r.stderr.splitlines():
            if "peak" in line:
                stats = line.strip()
        return {"url": "/render/" + os.path.basename(wav) + "?t=" + str(os.path.getmtime(wav)),
                "stats": stats}

    def _preset(self, req, rel):
        """Save the current slider positions into synth/presets.json, keyed by
        the DSP's file basename."""
        name = (req.get("name") or "").strip()
        if not name:
            return {"error": "give the preset a name"}
        params = req.get("params") or {}
        if not params:
            return {"error": "this DSP has no controls to save"}
        eng = os.path.splitext(os.path.basename(rel))[0]
        pj = os.path.join(ROOT, "synth", "presets.json")
        os.makedirs(os.path.dirname(pj), exist_ok=True)
        data = {}
        if os.path.isfile(pj):
            with open(pj) as f:
                data = json.load(f) or {}
        lst = data.setdefault(eng, [])
        entry = {"name": name[:14], "params": {k: float(v) for k, v in params.items()}}
        for i, existing in enumerate(lst):
            if existing.get("name") == entry["name"]:
                lst[i] = entry
                break
        else:
            lst.append(entry)
        with open(pj, "w") as f:
            json.dump(data, f, indent=2)
        # is this engine actually in the synth ROM?
        et = os.path.join(ROOT, "synth", "engines.txt")
        listed = False
        if os.path.isfile(et):
            listed = any(os.path.splitext(os.path.basename(l.strip()))[0] == eng
                         for l in open(et) if l.strip() and not l.startswith("#"))
        return {"saved": entry["name"], "engine": eng, "count": len(lst),
                "listed": listed}

    def _rom(self, req, rel):
        sr = int(req.get("sr", 22050))
        env = dict(os.environ)
        env.setdefault("N64_INST", os.path.expanduser("~/n64-toolchain"))
        out = os.path.splitext(os.path.join(ROOT, rel))[0] + ".z64"
        r = subprocess.run([os.path.join(ROOT, "faust2n64"), "-r", str(sr), "-o", out, rel],
                           cwd=ROOT, capture_output=True, text=True, env=env)
        if r.returncode != 0:
            return {"error": (r.stderr or r.stdout).strip()[:4000]}
        return {"rom": os.path.relpath(out, ROOT),
                "bytes": os.path.getsize(out),
                "log": r.stdout.strip()}


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    print("faust64 audio lab -> http://127.0.0.1:%d/" % PORT)
    Server(("127.0.0.1", PORT), Handler).serve_forever()
