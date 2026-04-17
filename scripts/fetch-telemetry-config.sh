#!/bin/bash
# Fetch telemetry endpoint + bearer token from AWS and write
# keys/telemetry.json so the CMake build can bake them into the app.
#
# Run this once per machine, or after rotating the token (delete the
# SSM parameter and redeploy the CDK stack, then re-run this).
#
# If this machine has no AWS access, skip running this — the build will
# emit empty TELEMETRY_URL / TELEMETRY_TOKEN and the shipper becomes a
# no-op.

set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p keys

URL=$(aws cloudformation describe-stacks \
    --stack-name PerformanceTelemetry \
    --query 'Stacks[0].Outputs[?OutputKey==`IngestUrl`].OutputValue' \
    --output text)

TOKEN=$(aws ssm get-parameter --with-decryption \
    --name /performance/telemetry/bearer-token \
    --query 'Parameter.Value' --output text)

umask 077
cat > keys/telemetry.json <<EOF
{
  "url": "$URL",
  "token": "$TOKEN"
}
EOF

echo "Wrote keys/telemetry.json"
echo "Reconfigure CMake and rebuild to pick up the new values."
