// Plugins proxy: fronts the private plugins S3 bucket.
//
// Flow on first launch:
//   App → GET <fn-url> with Authorization: Bearer <token>, X-Install-Id
//   Lambda → validates bearer, reads manifest.json from the bucket,
//            replaces each entry's `archiveUrl` with a short-lived
//            presigned S3 GET URL, returns the enriched manifest
//   App → GETs each archive directly from S3 using the presigned URL
//
// Rationale (see docs/BUNDLED_PLUGINS.md):
//   - The bucket stays BlockPublicAccess.BLOCK_ALL. No hotlinking.
//   - App holds no AWS credentials — just the same bearer it uses for
//     telemetry + chat (already baked in).
//   - Presigned URLs expire in 1 hour, which is comfortable headroom
//     for a ~86 MB sequential download on any reasonable connection.
//   - Plugins themselves aren't secret (all are freely available
//     upstream); the bearer check is casual rate-limiting, not a
//     hardened access gate.

import { S3Client, GetObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';
import {
  SecretsManagerClient,
  GetSecretValueCommand,
} from '@aws-sdk/client-secrets-manager';

const s3 = new S3Client({});
const secrets = new SecretsManagerClient({});

const BUCKET = process.env.PLUGINS_BUCKET!;
const BEARER_TOKEN_SECRET = process.env.BEARER_TOKEN_SECRET!;
const PRESIGN_EXPIRY_SECONDS = parseInt(
  process.env.PRESIGN_EXPIRY_SECONDS ?? '3600',
  10
);

let cachedBearerToken: string | undefined;
async function getBearerToken(): Promise<string> {
  if (cachedBearerToken !== undefined) return cachedBearerToken;
  const res = await secrets.send(
    new GetSecretValueCommand({ SecretId: BEARER_TOKEN_SECRET })
  );
  cachedBearerToken = res.SecretString ?? '';
  return cachedBearerToken;
}

function lowercaseHeaders(
  h?: Record<string, string | undefined>
): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(h ?? {})) {
    if (v !== undefined) out[k.toLowerCase()] = v;
  }
  return out;
}

function json(statusCode: number, body: unknown) {
  return {
    statusCode,
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body),
  };
}

interface ManifestArchive {
  slug: string;
  version: string;
  archiveName: string;
  archiveUrl: string | null;
  archiveSize: number;
  archiveSha256: string;
  components: string[];
}

interface Manifest {
  version: number;
  generatedAt: string;
  archives: ManifestArchive[];
}

export async function handler(event: any) {
  const headers = lowercaseHeaders(event.headers);

  const auth = (headers['authorization'] ?? '').trim();
  const token = auth.replace(/^Bearer\s+/i, '');
  const expected = await getBearerToken();
  if (!token || !expected || token !== expected) {
    return json(401, { error: 'unauthorized' });
  }

  // Fetch manifest.json from the private bucket.
  let manifest: Manifest;
  try {
    const res = await s3.send(
      new GetObjectCommand({ Bucket: BUCKET, Key: 'manifest.json' })
    );
    const body = await res.Body!.transformToString();
    manifest = JSON.parse(body);
  } catch (err: any) {
    if (err?.name === 'NoSuchKey') {
      return json(404, { error: 'manifest not published yet' });
    }
    console.error('manifest fetch failed', err);
    return json(500, { error: 'manifest fetch failed' });
  }

  // Replace each archiveUrl with a presigned GET URL.
  for (const archive of manifest.archives) {
    const key = `plugins/${archive.archiveName}`;
    archive.archiveUrl = await getSignedUrl(
      s3,
      new GetObjectCommand({ Bucket: BUCKET, Key: key }),
      { expiresIn: PRESIGN_EXPIRY_SECONDS }
    );
  }

  return json(200, manifest);
}
