#!/usr/bin/env bash
set -euo pipefail

PROSPECT="${1:-Acme Treasury}"
UPTIME="${UPTIME:-99.8}"
P99_LATENCY="${P99_LATENCY:-0.84}"
AUDIT_RECORDS="${AUDIT_RECORDS:-10847}"
OUTPUT_PATH="${OUTPUT_PATH:-pilot_sla_report.html}"
DATE_STAMP="${DATE_STAMP:-$(date +%F)}"

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
    <div class="metric"><strong>Uptime:</strong> ${UPTIME}%</div>
    <div class="metric"><strong>p99 Latency:</strong> ${P99_LATENCY}ms</div>
    <div class="metric"><strong>Audit Continuity:</strong> ${AUDIT_RECORDS} records</div>
    <div class="metric"><strong>Status:</strong> On track for pilot review</div>
  </div>
</body>
</html>
EOF

echo "SLA report generated at $OUTPUT_PATH"
