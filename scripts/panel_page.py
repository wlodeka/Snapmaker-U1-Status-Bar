import gzip
from pathlib import Path

Import("env")

# Embed web/panel.html gzip-compressed; the panel sends it with Content-Encoding: gzip.
source = Path(env.subst("$PROJECT_DIR")) / "web" / "panel.html"
output_dir = Path(env.subst("$BUILD_DIR")) / "generated"
output = output_dir / "panel_page.h"

data = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
rows = ",\n".join(
    "    " + ",".join(f"0x{byte:02x}" for byte in data[i:i + 20])
    for i in range(0, len(data), 20)
)
header = f"""// Generated from web/panel.html by scripts/panel_page.py. Do not edit.
#pragma once

#include <Arduino.h>

namespace u1 {{

const uint8_t PanelHtmlGzip[] PROGMEM = {{
{rows}
}};

}}  // namespace u1
"""

output_dir.mkdir(parents=True, exist_ok=True)
# Rewrite only on change so SCons does not rebuild web.cpp every time.
if not output.exists() or output.read_text() != header:
    output.write_text(header)
env.Append(CPPPATH=[str(output_dir)])
