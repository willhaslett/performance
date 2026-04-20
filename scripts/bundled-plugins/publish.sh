#!/usr/bin/env bash
# Upload the packaged plugin archives + manifest.json to the private
# plugins S3 bucket.
#
# Bucket is discovered from the PerformancePlugins CloudFormation
# stack's BucketName output. Override with BUCKET=<name> for testing
# against a different account/region.
#
# Objects go to:
#   plugins/<archive-name>.zip     immutable, long cache
#   manifest.json                  short cache, overwritten on each publish
#
# The uploaded manifest is manifest-draft.json as-is — the
# archiveUrl fields stay null. At request time, the PluginsProxy
# Lambda reads this file and fills each URL in with a short-lived
# presigned S3 GET before handing it to the app. Nothing in the
# bucket is publicly accessible; the Lambda's bearer check is the
# only authorization surface.
#
# Usage:  scripts/bundled-plugins/publish.sh
# Requires: aws CLI configured; package-all.sh has been run.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCHIVES="$REPO_ROOT/.cache/staging/archives"
MANIFEST="$ARCHIVES/manifest-draft.json"

if [ ! -d "$ARCHIVES" ] || [ ! -f "$MANIFEST" ]; then
    echo "!! Run package-all.sh first — $ARCHIVES or manifest-draft.json missing" >&2
    exit 1
fi

if [ -z "${BUCKET:-}" ]; then
    echo "==> Looking up bucket from CloudFormation"
    BUCKET=$(aws cloudformation describe-stacks \
        --stack-name PerformancePlugins \
        --query "Stacks[0].Outputs[?OutputKey=='BucketName'].OutputValue" \
        --output text)
    if [ -z "$BUCKET" ] || [ "$BUCKET" = "None" ]; then
        echo "!! Could not resolve BucketName output from PerformancePlugins stack" >&2
        echo "   Deploy with: cd infra && npx cdk deploy PerformancePlugins" >&2
        exit 1
    fi
fi
echo "==> Target: s3://$BUCKET/"

# Sanity check: bucket reachable + we can list it.
if ! aws s3 ls "s3://$BUCKET/" >/dev/null 2>&1; then
    echo "!! Cannot list s3://$BUCKET/ — check AWS credentials or bucket name" >&2
    exit 1
fi

shopt -s nullglob
count=0
for archive in "$ARCHIVES"/*.zip; do
    name="$(basename "$archive")"
    echo "==> uploading $name ($(du -h "$archive" | awk '{print $1}'))"
    aws s3 cp "$archive" "s3://$BUCKET/plugins/$name" \
        --content-type "application/zip" \
        --cache-control "public, max-age=31536000, immutable" \
        --only-show-errors
    count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
    echo "!! No archives found under $ARCHIVES" >&2
    exit 1
fi

echo "==> uploading manifest.json (archiveUrl filled in per-request by Lambda)"
aws s3 cp "$MANIFEST" "s3://$BUCKET/manifest.json" \
    --content-type "application/json" \
    --cache-control "public, max-age=300" \
    --only-show-errors

echo ""
echo "==> Done. $count archives + manifest.json uploaded to:"
echo "    s3://$BUCKET/"
echo ""
echo "Verify end-to-end:"
echo "  BEARER=\$(aws secretsmanager get-secret-value \\"
echo "      --secret-id performance/telemetry/bearer-token \\"
echo "      --query SecretString --output text)"
echo "  curl -s -H \"Authorization: Bearer \$BEARER\" \\"
echo "      https://<PluginsProxyUrl> | jq ."
