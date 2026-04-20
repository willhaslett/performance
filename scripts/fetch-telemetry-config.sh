#!/bin/bash
# Fetch telemetry endpoint, chat-proxy endpoint, and bearer token from
# AWS and write keys/telemetry.json so the CMake build can bake them
# into the app.
#
# Run this once per machine, or after rotating the bearer token (delete
# the secret in Secrets Manager and redeploy the CDK stack, then re-run
# this).
#
# If this machine has no AWS access, skip running this — the build will
# emit empty values and the shipper / chat client become no-ops.

set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p keys

URL=$(aws cloudformation describe-stacks \
    --stack-name PerformanceTelemetry \
    --query 'Stacks[0].Outputs[?OutputKey==`IngestUrl`].OutputValue' \
    --output text)

CHAT_URL=$(aws cloudformation describe-stacks \
    --stack-name PerformanceTelemetry \
    --query 'Stacks[0].Outputs[?OutputKey==`ChatProxyUrl`].OutputValue' \
    --output text)

PLUGINS_URL=$(aws cloudformation describe-stacks \
    --stack-name PerformanceTelemetry \
    --query 'Stacks[0].Outputs[?OutputKey==`PluginsProxyUrl`].OutputValue' \
    --output text)

TOKEN=$(aws secretsmanager get-secret-value \
    --secret-id performance/telemetry/bearer-token \
    --query 'SecretString' --output text)

umask 077
cat > keys/telemetry.json <<EOF
{
  "url": "$URL",
  "chatUrl": "$CHAT_URL",
  "pluginsUrl": "$PLUGINS_URL",
  "token": "$TOKEN"
}
EOF

echo "Wrote keys/telemetry.json"
echo "Reconfigure CMake and rebuild to pick up the new values."
