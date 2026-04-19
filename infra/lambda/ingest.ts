import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';
import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { DynamoDBDocumentClient, UpdateCommand } from '@aws-sdk/lib-dynamodb';
import { SecretsManagerClient, GetSecretValueCommand } from '@aws-sdk/client-secrets-manager';

const s3 = new S3Client({});
const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const secrets = new SecretsManagerClient({});

const BUCKET = process.env.BUCKET!;
const TABLE = process.env.TABLE!;
const BEARER_TOKEN_SECRET = process.env.BEARER_TOKEN_SECRET!;

// Cached at cold start. Never rotated within a container's lifetime —
// rotate by changing the secret value and creating a new container.
let cachedToken: string | undefined;

async function bearerToken(): Promise<string> {
  if (cachedToken) return cachedToken;
  const res = await secrets.send(new GetSecretValueCommand({
    SecretId: BEARER_TOKEN_SECRET,
  }));
  cachedToken = res.SecretString ?? '';
  return cachedToken;
}

interface FunctionUrlEvent {
  headers?: Record<string, string | undefined>;
  body?: string;
  isBase64Encoded?: boolean;
}

function lowercaseHeaders(h?: Record<string, string | undefined>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(h ?? {})) {
    if (v !== undefined) out[k.toLowerCase()] = v;
  }
  return out;
}

export const handler = async (event: FunctionUrlEvent) => {
  const headers = lowercaseHeaders(event.headers);

  const auth = headers['authorization'] ?? '';
  const token = await bearerToken();
  if (auth !== `Bearer ${token}`) {
    return { statusCode: 401, body: 'Unauthorized' };
  }

  const installId = headers['x-install-id'];
  const commit = headers['x-commit'] ?? 'unknown';
  const version = headers['x-version'] ?? 'unknown';

  if (!installId) {
    return { statusCode: 400, body: 'Missing X-Install-Id header' };
  }
  if (!event.body) {
    return { statusCode: 400, body: 'Empty body' };
  }

  const buf = Buffer.from(event.body, event.isBase64Encoded ? 'base64' : 'utf8');

  const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
  const safeCommit = commit.replace(/[^a-zA-Z0-9._-]/g, '_');
  const key = `${installId}/${timestamp}-${safeCommit}.log.gz`;

  await s3.send(new PutObjectCommand({
    Bucket: BUCKET,
    Key: key,
    Body: buf,
    ContentType: 'application/gzip',
    Metadata: { version, commit, installId },
  }));

  const now = new Date().toISOString();
  await ddb.send(new UpdateCommand({
    TableName: TABLE,
    Key: { installationId: installId },
    UpdateExpression:
      'SET firstSeen = if_not_exists(firstSeen, :now), ' +
      'lastSeen = :now, lastCommit = :commit, lastVersion = :version ' +
      'ADD totalBytes :bytes, totalLogs :one',
    ExpressionAttributeValues: {
      ':now': now,
      ':commit': commit,
      ':version': version,
      ':bytes': buf.length,
      ':one': 1,
    },
  }));

  return {
    statusCode: 200,
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ stored: key, bytes: buf.length }),
  };
};
