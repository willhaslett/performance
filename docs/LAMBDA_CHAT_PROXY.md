# Lambda Chat Proxy — Design & Implementation Plan

The "AI for testers" chunk: a thin Lambda in front of the Anthropic API so testers never see (or need) an API key, costs are bounded per-install, and the model + system prompt can change without an app update.

This doc captures the design before implementation. Source of truth for the work; CLAUDE.md only points here.

## Goals

In priority order:

1. **Tester ergonomics.** Open the app, open Chat, type, get help. Zero setup, no key paste.
2. **Cost-bounded.** Hard per-install monthly cap. We can leave this running with strangers without a runaway bill.
3. **Server-controlled model + prompt.** Bump Sonnet → Opus (or the reverse, or a future model) without shipping a build. Same for the system prompt if we iterate on it post-launch.
4. **Cheap at idle, cheap at low scale, sane at moderate scale.** Pay-per-use everything; zero fixed overhead. Should be ~free for 4 friends, ~$50/month for 40 active strangers, still tractable at 400.
5. **Reuse existing infra.** We already have a CDK stack (`PerformanceTelemetry`), a bearer-token pattern, an `installId`, and a Lambda. Extend, don't fork.

Non-goals for now: streaming history server-side, tester-paid usage, multi-region, auth beyond bearer + install ID.

## Architecture

```
ChatView (C++)
    │
    │  POST /chat with bearer + X-Install-Id, JSON body matching Anthropic Messages API
    │
    ▼
Lambda Function URL (response streaming enabled)
    │
    │  validate(bearer + installId)
    │  loadBudget(installId) from DynamoDB
    │  if exceeded → 402 + friendly message, return
    │  forward to api.anthropic.com with our key
    │  stream response back to client
    │  on completion: count input+output tokens, updateBudget(installId)
    │
    ▼
DynamoDB: performance-installations
    chatTokensInThisMonth, chatTokensOutThisMonth, chatMonthKey
```

### Why each piece

- **Lambda Function URL (not API Gateway):** function URLs natively support response streaming (`awslambda.streamifyResponse`), have no per-request overhead, and we already use one for telemetry. API Gateway adds cost + complexity for zero benefit at this scale.
- **Bearer token + install ID:** bearer says "this is a Performance build talking" (gates random internet traffic). Install ID is the per-user budget unit and the join key for telemetry. Already deployed; reuse.
- **DynamoDB pay-per-request:** we already have the `performance-installations` table. Add chat-budget attributes. PPR pricing is rounding error at our scale.
- **Streaming:** smoke test of non-streaming Sonnet 4.6 felt sluggish (multi-second first-token, then wall-of-text). Streaming makes perceived latency much better at zero cost difference. Budget ~4 hours of C++ SSE plumbing per CLAUDE.md §3.
- **Stateless server (history client-side):** ChatView already stores history. Sending the full transcript per turn is exactly what Anthropic's stateless API expects. Saves DynamoDB writes, keeps history portable, no schema lock-in.

## Cost model

Order-of-magnitude numbers for budgeting. Treat as Fermi estimates, not contracts.

### Per-turn

- **System prompt + tool defs + history:** ~5–10k input tokens.
- **Assistant response + tool turns:** 500–2000 output tokens for a typical "add a reverb to track 2"; 5–10k for an exploratory turn ("why isn't my MIDI working").
- **Sonnet 4.6 pricing (as of writing):** ~$3/Mtok input, ~$15/Mtok output.
- **Cost per turn:** $0.02–$0.20. Call it $0.05 average.

### Per tester

- **Casual day:** 5–10 turns. ~$0.50.
- **Active day (recording, debugging):** 30–60 turns. ~$3–6.
- **Mostly-idle tester:** maybe 5 turns/week. <$0.10.

### Per month, by population

| Testers | Mix | Estimated $/mo |
|---|---|---|
| 4 friends | half active, half curious | $20–50 |
| 40 strangers | mostly curious, few active | $100–300 |
| 400 | mostly idle, long tail | $500–1500 |

Lambda + DynamoDB stay <$5/mo at all of these. Anthropic dominates.

### Existing budget alarm

`infra/lib/performance-telemetry-stack.ts` has a $5/mo CloudWatch billing alarm. Bump to $50 before opening the friends round; revisit at each population step.

## Per-install caps

Initial proposal — adjust after a week of friends-round telemetry:

- **100k input tokens / month** per install (≈ 10–20 turns of heavy exploration)
- **25k output tokens / month** per install
- **No per-minute throttle.** Anthropic's own 429 will surface; we forward it.
- **Calendar-month reset.** `chatMonthKey = "2026-04"`; if loaded value differs from current, reset counters.

When exceeded: return HTTP 402 with body `{"error": "monthly chat budget exhausted; resets on the 1st"}`. ChatView shows a friendly notice.

Will gets uncapped via a special install ID list in env vars.

## Decisions still open

These need a call before coding starts. Recommendations in italics; flag if you disagree.

1. **Streaming.** *Yes.* Non-streaming felt sluggish in the smoke test, and Lambda makes it cheap to add.
2. **Default model.** *Try `claude-sonnet-4-6` again from the proxy.* The slow/rate-limited behavior we saw on inline bump might have been personal-key throughput, not the model. Have a server-side env var so we can flip without an app build. If 4-6 still misbehaves through the proxy with our org key, fall back to `claude-sonnet-4-20250514` (current pin).
3. **System prompt source.** *Bundled in the binary (current behavior) for now.* Server-controlled prompt is nice but adds a moving part. Defer until we want to iterate on prompts post-ship.
4. **Bring-your-own-key fallback.** *Defer to 0.2.x* per CLAUDE.md §5. Friends round is fully proxy-only.

## Implementation plan

### 1. Infra (CDK)

`infra/lib/performance-telemetry-stack.ts`:

- Add second Lambda (`performance-chat-proxy`) with its own function URL + response streaming enabled.
- Lambda env vars: `ANTHROPIC_API_KEY` (from SSM), `BEARER_TOKEN` (existing SSM param), `DEFAULT_MODEL`, `MONTHLY_INPUT_CAP`, `MONTHLY_OUTPUT_CAP`, `UNCAPPED_INSTALLS` (CSV).
- DynamoDB: extend the existing `performance-installations` table with attributes — no schema change since DDB is schemaless. Just write new attrs on first chat call.
- IAM: Lambda needs `dynamodb:GetItem`, `dynamodb:UpdateItem` on the existing table. No S3 needed.
- CloudWatch billing alarm: bump from $5 → $50.

Deploy: `cd infra && npx cdk deploy`. Outputs new function URL.

### 2. Lambda handler (Node 20, ESM)

```js
import { streamifyResponse } from "lambda-stream";  // or built-in awslambda.streamifyResponse
import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { UpdateCommand, GetCommand } from "@aws-sdk/lib-dynamodb";

export const handler = streamifyResponse(async (event, responseStream) => {
    // 1. Validate bearer + install ID
    // 2. Load budget (handle missing row + month rollover)
    // 3. Check caps; if exceeded, write 402 to responseStream and return
    // 4. POST to api.anthropic.com/v1/messages with stream: true
    // 5. Pipe Anthropic SSE → responseStream, accumulating token counts from
    //    the `message_start` and `message_delta` events
    // 6. On stream end: update DDB with new totals
});
```

Key correctness points:
- **Token counting:** read from `message_delta.usage`, not from manual estimation. Anthropic returns final input + output token counts in the streaming events.
- **Budget update is best-effort.** If the DDB write fails post-stream, we've already served the response. Log + continue. Worst case: tester gets a free turn.
- **No retries on Anthropic 5xx.** Forward the error.

### 3. C++ client (`src/api/ClaudeClient.cpp`)

Changes from the current `api.anthropic.com` direct path:

- Compile-time `CHAT_PROXY_URL` (in `BuildConfig.h`, written by `cmake/GenerateBuildConfig.cmake` from `keys/telemetry.json`).
- Auth: `Authorization: Bearer <token>` + `X-Install-Id: <uuid>` headers. Drop `x-api-key`.
- Body: same Anthropic Messages API JSON. Proxy is transparent.
- For streaming: replace `readEntireStreamAsString()` with a line-by-line SSE reader that emits text deltas to the listener. Bound the work — JUCE's `URL::createInputStream` returns a stream you can read incrementally.
- Keep the existing `getenv("ANTHROPIC_API_KEY")` dev path under a debug flag for Will's local iteration. Delete once the proxy is stable.

Error handling:
- 402: tester sees "Free chat budget for this month is used up — message Will."
- 429: "Try again in a moment."
- Other 4xx/5xx: log + show the message body.

### 4. Build-time config

Extend `keys/telemetry.json`:
```json
{
  "telemetryUrl": "...",
  "chatProxyUrl": "...",
  "bearerToken": "..."
}
```

`scripts/fetch-telemetry-config.sh` already pulls from CloudFormation outputs + SSM; add the chat URL output.

### 5. Tests

- New unit test for SSE parsing (mock incremental reads).
- Manual: `bin/perf` script that hits the proxy with a fake install ID, verifies 200 + streaming chunks.

### 6. Tester-facing copy (perfuce.com)

Per CLAUDE.md §3 to-do "Tester onboarding copy":
- 3–4 example prompts on the docs page.
- One sentence: "Chat is free for testers — if it stops working, tell Will."
- No mention of API keys.

## Sequencing

1. Deploy stack with new Lambda + URL (no app changes yet) — verify with curl.
2. Wire C++ client to proxy, keep streaming off — test parity with current behavior.
3. Add streaming end-to-end — test with long responses.
4. Add per-install caps + 402 handling — test by setting cap to 100 and burning through it.
5. Bump CloudWatch alarm to $50.
6. Update perfuce.com onboarding copy.
7. Delete `getenv("ANTHROPIC_API_KEY")` dev path.

Estimate: 1–2 days end-to-end if no surprises. Most risk in the SSE plumbing on the C++ side.

## Future work (post-0.1.0)

- **Per-install model overrides.** Some testers might want Opus; flag in DDB.
- **Bring-your-own-key.** Settings field that bypasses the proxy. Useful for power users + insurance against proxy outage.
- **Conversation summarization on long sessions.** Cut input cost when transcripts get long.
- **Cached system prompt.** Anthropic supports prompt caching; system prompt is the same across all testers.
- **Per-tester usage dashboard.** Will-only view in the existing telemetry stack.
