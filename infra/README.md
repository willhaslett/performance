# Performance — Infra

AWS CDK (TypeScript) stack for the Performance app's backend.

## Stacks

- **`PerformanceTelemetry`** — session-log ingest.
  - S3 bucket for log storage (1-year lifecycle, RETAIN on stack destroy).
  - DynamoDB `performance-installations` table for per-install metadata.
  - Lambda with function URL; validates `Authorization: Bearer <token>` and writes to S3 + DDB.
  - SSM Parameter `/performance/telemetry/bearer-token` — shared token baked into app builds.
  - Monthly $5 cost budget with email alarm at 80%.

## One-time setup

```bash
cd infra
npm install
# bootstrap CDK in your account/region (once per account + region)
npx cdk bootstrap aws://<account-id>/us-east-1
```

## Deploy / diff / destroy

```bash
npx cdk diff      # preview changes
npx cdk deploy    # apply
npx cdk destroy   # remove stack (bucket + DDB retained by design)
```

After a deploy, capture outputs:
- **IngestUrl** — baked into the Performance app.
- **BearerTokenParameterName** — read the actual token via:
  ```bash
  aws ssm get-parameter --with-decryption \
      --name /performance/telemetry/bearer-token \
      --query 'Parameter.Value' --output text
  ```

## Rotating the bearer token

```bash
aws ssm delete-parameter --name /performance/telemetry/bearer-token
npx cdk deploy
# then rebuild the app with the new token
```

## Why retained resources

`RemovalPolicy.RETAIN` on the bucket and DDB table means `cdk destroy`
tears down the Lambda + URL + budget but leaves data intact. Reason:
data loss from an accidental `destroy` is worse than the cost of a few
orphaned resources you can delete manually if truly unneeded.
