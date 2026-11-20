#!/usr/bin/env python3
"""Assemble build/preview/index.html from sdk/Update.html.in (port of the
LocalPreview branch of fm1_open/tools/Build-Web.ps1)."""
import base64
import hashlib
import html
import json
import re
import sys
from pathlib import Path

root, wasm_path = Path(sys.argv[1]), Path(sys.argv[2])
rd = lambda p: (root / p).read_text(encoding="utf-8")
m = re.search(r'(?m)^#define FM1_RELEASE_VERSION "(V(?:0[1-9]|[1-9][0-9]{1,2}))"\s*$', rd("app/version.h"))
if not m:
    sys.exit("Set FM1_RELEASE_VERSION in app/version.h")
wasm = wasm_path.read_bytes()
sim = json.dumps({"schema": 1, "release_version": m.group(1), "sha256": hashlib.sha256(wasm).hexdigest(),
                  "source_sha256": "local-preview", "sample_bank": "synthesized-demo",
                  "data": base64.b64encode(wasm).decode()}, separators=(",", ":"))
web = rd("sdk/Update.html.in")
web = re.sub(r'<a\b[^>]*href="/\*FM1_SOURCE_URL\*/"[^>]*>(.*?)</a>', r'<span>\1 (local preview)</span>', web)
notices = ["APP: GPLv3 with the FM1 SDK linking exception; no warranty. Local preview build."]
for f in ("THIRD_PARTY.md", "LICENSE.md"):
    notices.append("\n===== %s =====\n" % f + rd(f))
rep = {
    "/*FM1_SOURCE_URL*/": "",
    "/*FM1_PAYLOAD*/": "const FM1_PACKAGE = null;",
    "/*FM1_SIMULATOR*/": "const FM1_SIMULATOR = " + sim + ";",
    "/*FM1_I18N*/": rd("sdk/I18n.js"),
    "/*FM1_SCRIPT*/": rd("sdk/WebUpdate.js"),
    "/*FM1_SIM_SCRIPT*/": rd("sdk/Simulator.js"),
    "/*FM1_SAMPLE_SCRIPT*/": rd("sdk/Samples.js"),
    "/*FM1_NOTICES*/": html.escape("\n".join(notices), quote=True),
}
for k, v in rep.items():
    web = web.replace(k, v)
if re.search(r"/\*FM1_[A-Z_]+\*/", web):
    sys.exit("Unexpanded HTML placeholder")
out = root / "build/preview/index.html"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(web, encoding="utf-8")
print("Preview:", out, "(%d KB wasm)" % (len(wasm) // 1024))
