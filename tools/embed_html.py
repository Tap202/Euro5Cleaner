import os

root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
html_path = os.path.join(root_dir, "dashboard_wifi.html")
header_path = os.path.join(root_dir, "include", "dashboard_html.h")

with open(html_path, "r", encoding="utf-8") as f:
    html_content = f.read()

header_content = (
    "#pragma once\n"
    "#include <Arduino.h>\n\n"
    "const char INDEX_HTML[] PROGMEM = R\"rawliteral("
    + html_content
    + ")rawliteral\";\n"
)

with open(header_path, "w", encoding="utf-8") as f:
    f.write(header_content)

print(f"Successfully generated {header_path} ({len(html_content)} bytes of HTML)")
