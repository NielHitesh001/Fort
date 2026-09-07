#!/usr/bin/env bash
set -euo pipefail

PROSPECT="${1:-Acme Treasury}"
UPTIME="${UPTIME:-UNVERIFIED}"
P99_LATENCY="${P99_LATENCY:-UNVERIFIED}"
AUDIT_RECORDS="${AUDIT_RECORDS:-UNVERIFIED}"
OUTPUT_PATH="${OUTPUT_PATH:-pilot_sla_report.html}"
DATE_STAMP="${DATE_STAMP:-$(date +%F)}"

format_percent() {
  if [[ "$1" == "UNVERIFIED" ]]; then printf '%s' "$1"; else printf '%s%%' "$1"; fi
}

format_ms() {
  if [[ "$1" == "UNVERIFIED" ]]; then printf '%s' "$1"; else printf '%sms' "$1"; fi
}

cat > "$OUTPUT_PATH" <<EOF
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Pilot SLA Report</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 2rem; }
    .card { border: 1px solid #d0d0d0; border-radius: 8px; padding: 1.25rem; max-width: 720px; }
    .metric { font-size: 1.1rem; margin: 0.5rem 0; }
    strong { color: #1a1a1a; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Pilot SLA Tracking</h1>
    <p><strong>Prospect:</strong> ${PROSPECT}</p>
    <p><strong>Date:</strong> ${DATE_STAMP}</p>
    <div class="metric"><strong>Uptime:</strong> $(format_percent "$UPTIME")</div>
    <div class="metric"><strong>p99 Latency:</strong> $(format_ms "$P99_LATENCY")</div>
    <div class="metric"><strong>Audit Continuity:</strong> ${AUDIT_RECORDS} records</div>
    <div class="metric"><strong>Status:</strong> Draft; attach source artifacts before external use</div>
  </div>
</body>
</html>
EOF

echo "SLA report generated at $OUTPUT_PATH"
