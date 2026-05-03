// Chat proxy: Anthropic Messages API, with bearer auth, per-install token
// budgets, and response streaming.
//
// See docs/LAMBDA_CHAT_PROXY.md for design context.

import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { DynamoDBDocumentClient, GetCommand, UpdateCommand } from '@aws-sdk/lib-dynamodb';
import { SecretsManagerClient, GetSecretValueCommand } from '@aws-sdk/client-secrets-manager';
import { SSMClient, GetParameterCommand } from '@aws-sdk/client-ssm';

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const secrets = new SecretsManagerClient({});
const ssm = new SSMClient({});

const TABLE = process.env.TABLE!;
const BEARER_TOKEN_SECRET = process.env.BEARER_TOKEN_SECRET!;
const ANTHROPIC_KEY_PARAM = process.env.ANTHROPIC_KEY_PARAM!;
const DEFAULT_MODEL = process.env.DEFAULT_MODEL ?? 'claude-sonnet-4-5';
const MONTHLY_INPUT_CAP = parseInt(process.env.MONTHLY_INPUT_CAP ?? '100000', 10);
const MONTHLY_OUTPUT_CAP = parseInt(process.env.MONTHLY_OUTPUT_CAP ?? '25000', 10);
const UNCAPPED_INSTALLS = (process.env.UNCAPPED_INSTALLS ?? '')
  .split(',').map(s => s.trim()).filter(Boolean);

// Cached at cold start.
let cachedBearerToken: string | undefined;
async function getBearerToken(): Promise<string> {
  if (cachedBearerToken !== undefined) return cachedBearerToken;
  const res = await secrets.send(new GetSecretValueCommand({ SecretId: BEARER_TOKEN_SECRET }));
  cachedBearerToken = res.SecretString ?? '';
  return cachedBearerToken;
}

const ssmCache = new Map<string, string>();
async function getSsmParam(name: string): Promise<string> {
  const cached = ssmCache.get(name);
  if (cached !== undefined) return cached;
  const res = await ssm.send(new GetParameterCommand({ Name: name, WithDecryption: true }));
  const value = res.Parameter?.Value ?? '';
  ssmCache.set(name, value);
  return value;
}

function lowercaseHeaders(h?: Record<string, string | undefined>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(h ?? {})) {
    if (v !== undefined) out[k.toLowerCase()] = v;
  }
  return out;
}

function currentMonthKey(): string {
  const d = new Date();
  return `${d.getUTCFullYear()}-${String(d.getUTCMonth() + 1).padStart(2, '0')}`;
}

interface BudgetState {
  monthKey: string;
  inputTokens: number;
  outputTokens: number;
}

async function loadBudget(installId: string): Promise<BudgetState> {
  const res = await ddb.send(new GetCommand({
    TableName: TABLE,
    Key: { installationId: installId },
    ProjectionExpression: 'chatMonthKey, chatInputTokens, chatOutputTokens',
  }));
  const item = res.Item ?? {};
  const month = currentMonthKey();
  if (item.chatMonthKey !== month) {
    return { monthKey: month, inputTokens: 0, outputTokens: 0 };
  }
  return {
    monthKey: month,
    inputTokens: item.chatInputTokens ?? 0,
    outputTokens: item.chatOutputTokens ?? 0,
  };
}

async function saveBudget(installId: string, b: BudgetState, addedIn: number, addedOut: number) {
  const newIn = b.inputTokens + addedIn;
  const newOut = b.outputTokens + addedOut;
  await ddb.send(new UpdateCommand({
    TableName: TABLE,
    Key: { installationId: installId },
    UpdateExpression: 'SET chatMonthKey = :m, chatInputTokens = :i, chatOutputTokens = :o, chatLastSeen = :now',
    ExpressionAttributeValues: {
      ':m': b.monthKey, ':i': newIn, ':o': newOut, ':now': new Date().toISOString(),
    },
  }));
}

interface FunctionUrlEvent {
  headers?: Record<string, string | undefined>;
  body?: string;
  isBase64Encoded?: boolean;
  requestContext?: { http?: { method?: string } };
}

// awslambda is provided as a runtime global by the Node.js Lambda runtime when
// response streaming is enabled. Minimal type declaration so TS is happy.
declare global {
  // eslint-disable-next-line @typescript-eslint/no-namespace, no-var
  var awslambda: {
    streamifyResponse: (
      handler: (event: FunctionUrlEvent, responseStream: NodeJS.WritableStream, context: unknown) => Promise<void>
    ) => unknown;
    HttpResponseStream: {
      from: (
        stream: NodeJS.WritableStream,
        metadata: { statusCode: number; headers?: Record<string, string> }
      ) => NodeJS.WritableStream;
    };
  };
}

function jsonResponse(stream: NodeJS.WritableStream, status: number, body: object) {
  const meta = { statusCode: status, headers: { 'content-type': 'application/json' } };
  const out = awslambda.HttpResponseStream.from(stream, meta);
  out.write(JSON.stringify(body));
  out.end();
}

function sseResponse(stream: NodeJS.WritableStream): NodeJS.WritableStream {
  return awslambda.HttpResponseStream.from(stream, {
    statusCode: 200,
    headers: { 'content-type': 'text/event-stream' },
  });
}

export const handler = awslambda.streamifyResponse(async (event, responseStream) => {
  try {
    const headers = lowercaseHeaders(event.headers);

    const auth = headers['authorization'] ?? '';
    const expected = `Bearer ${await getBearerToken()}`;
    if (auth !== expected) {
      return jsonResponse(responseStream, 401, { error: 'Unauthorized' });
    }

    const installId = headers['x-install-id'];
    if (!installId) {
      return jsonResponse(responseStream, 400, { error: 'Missing X-Install-Id' });
    }
    if (!event.body) {
      return jsonResponse(responseStream, 400, { error: 'Empty body' });
    }

    // Budget check (skip for uncapped installs)
    const uncapped = UNCAPPED_INSTALLS.includes(installId);
    const budget = uncapped ? null : await loadBudget(installId);
    if (budget && (budget.inputTokens >= MONTHLY_INPUT_CAP || budget.outputTokens >= MONTHLY_OUTPUT_CAP)) {
      return jsonResponse(responseStream, 402, {
        error: 'Free chat budget exhausted for this month — resets on the 1st.',
      });
    }

    // Parse body, override model + force streaming
    const raw = event.isBase64Encoded
      ? Buffer.from(event.body, 'base64').toString('utf8')
      : event.body;
    let body: Record<string, unknown>;
    try {
      body = JSON.parse(raw);
    } catch {
      return jsonResponse(responseStream, 400, { error: 'Body is not valid JSON' });
    }
    body.model = body.model ?? DEFAULT_MODEL;
    body.stream = true;

    // Forward to Anthropic
    const apiKey = await getSsmParam(ANTHROPIC_KEY_PARAM);
    const upstream = await fetch('https://api.anthropic.com/v1/messages', {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        'x-api-key': apiKey,
        'anthropic-version': '2023-06-01',
      },
      body: JSON.stringify(body),
    });

    if (!upstream.ok) {
      const errorBody = await upstream.text();
      return jsonResponse(responseStream, upstream.status, {
        error: 'Upstream error',
        upstream_status: upstream.status,
        upstream_body: errorBody.slice(0, 2000),
      });
    }
    if (!upstream.body) {
      return jsonResponse(responseStream, 502, { error: 'Upstream returned no body' });
    }

    // Pipe SSE stream through, parsing usage events as we go.
    const out = sseResponse(responseStream);
    const decoder = new TextDecoder();
    let buffer = '';
    let inputTokens = 0;
    let cacheCreationTokens = 0;
    let cacheReadTokens = 0;
    let outputTokens = 0;

    const reader = upstream.body.getReader();
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      out.write(value);

      // Accumulate for SSE event parsing
      buffer += decoder.decode(value, { stream: true });
      let nl: number;
      while ((nl = buffer.indexOf('\n\n')) !== -1) {
        const evtBlock = buffer.slice(0, nl);
        buffer = buffer.slice(nl + 2);
        for (const line of evtBlock.split('\n')) {
          if (!line.startsWith('data: ')) continue;
          const json = line.slice(6).trim();
          if (!json || json === '[DONE]') continue;
          try {
            const evt = JSON.parse(json);
            if (evt.type === 'message_start' && evt.message?.usage) {
              const u = evt.message.usage;
              if (u.input_tokens != null) inputTokens = u.input_tokens;
              if (u.cache_creation_input_tokens != null)
                cacheCreationTokens = u.cache_creation_input_tokens;
              if (u.cache_read_input_tokens != null)
                cacheReadTokens = u.cache_read_input_tokens;
            }
            if (evt.type === 'message_delta' && evt.usage?.output_tokens != null) {
              outputTokens = evt.usage.output_tokens;
            }
          } catch { /* ignore unparseable */ }
        }
      }
    }
    out.end();

    // Cost-weighted input tokens against the cap. Anthropic Sonnet 4.5
    // pricing relative to base input ($3/M):
    //   - input_tokens                  → 1.0x
    //   - cache_creation_input_tokens   → 1.25x ($3.75/M, one-time write)
    //   - cache_read_input_tokens       → 0.1x  ($0.30/M, subsequent reads)
    // Weighting by relative cost means the cap tracks dollar impact, so
    // a heavily-cached session with the same effective spend doesn't
    // burn the budget the way an unweighted token count would. Output
    // is unaffected (no caching applies to it).
    const weightedInputTokens = Math.round(
      inputTokens + cacheCreationTokens * 1.25 + cacheReadTokens * 0.1
    );

    // Best-effort budget update — if this fails the user already got their reply.
    if (budget && (weightedInputTokens > 0 || outputTokens > 0)) {
      try {
        await saveBudget(installId, budget, weightedInputTokens, outputTokens);
      } catch (e) {
        console.error('Budget update failed:', e);
      }
    }
  } catch (e) {
    console.error('Handler error:', e);
    try {
      jsonResponse(responseStream, 500, { error: 'Internal server error' });
    } catch { /* stream may already be closed */ }
  }
});
