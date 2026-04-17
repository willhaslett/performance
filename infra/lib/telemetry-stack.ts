import { Stack, StackProps, Duration, RemovalPolicy, CfnOutput } from 'aws-cdk-lib';
import { Construct } from 'constructs';
import { Bucket, BlockPublicAccess, BucketEncryption } from 'aws-cdk-lib/aws-s3';
import { Table, AttributeType, BillingMode } from 'aws-cdk-lib/aws-dynamodb';
import { Runtime, FunctionUrlAuthType, HttpMethod } from 'aws-cdk-lib/aws-lambda';
import { NodejsFunction } from 'aws-cdk-lib/aws-lambda-nodejs';
import { RetentionDays } from 'aws-cdk-lib/aws-logs';
import { CfnBudget } from 'aws-cdk-lib/aws-budgets';
import { StringParameter } from 'aws-cdk-lib/aws-ssm';
import * as crypto from 'crypto';
import * as path from 'path';

// Telemetry ingest for the Performance app.
//
// Shape:
//   App → POST (gzipped log, headers X-Install-Id / X-Commit / X-Version,
//               Authorization: Bearer <token>) → Lambda function URL
//   Lambda validates bearer token, writes to S3, upserts install record in
//   DynamoDB. Bucket keeps objects for 1 year; stack retention for bucket
//   is RETAIN so teardown doesn't destroy accumulated logs.
//
// Notes:
//   - The bearer token is regenerated on first deploy and stored in SSM
//     Parameter Store under /performance/telemetry/bearer-token. Subsequent
//     deploys reuse the stored value so the token is stable across changes.
//   - CORS origin is wide open since the client is a macOS app (no browser).
//   - Monthly budget alarm at $5 with email notification at 80%.
export class TelemetryStack extends Stack {
  constructor(scope: Construct, id: string, props?: StackProps) {
    super(scope, id, props);

    const notificationEmail = this.node.tryGetContext('notificationEmail') as string;

    // Session-log storage. Lifecycle: expire objects after 1 year; keep bucket on stack destroy.
    const bucket = new Bucket(this, 'SessionLogs', {
      bucketName: `performance-session-logs-${this.account}`,
      blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
      encryption: BucketEncryption.S3_MANAGED,
      lifecycleRules: [{ expiration: Duration.days(365) }],
      removalPolicy: RemovalPolicy.RETAIN,
    });

    // Per-installation metadata. Write-only from the Lambda; queryable via AWS console.
    const installations = new Table(this, 'Installations', {
      tableName: 'performance-installations',
      partitionKey: { name: 'installationId', type: AttributeType.STRING },
      billingMode: BillingMode.PAY_PER_REQUEST,
      removalPolicy: RemovalPolicy.RETAIN,
    });

    // Bearer token — generated on first synth, persisted to SSM so redeploys reuse it.
    // Not a secret in the security sense — app ships the token baked in; role is rate-
    // limiting + "are you a Performance app or random spam." Rotate by deleting the SSM
    // parameter and redeploying.
    const bearerToken = crypto.randomBytes(32).toString('base64url');
    const tokenParam = new StringParameter(this, 'BearerToken', {
      parameterName: '/performance/telemetry/bearer-token',
      stringValue: bearerToken,
      description: 'Shared bearer token embedded in Performance app builds',
    });

    const ingest = new NodejsFunction(this, 'Ingest', {
      entry: path.join(__dirname, '..', 'lambda', 'ingest.ts'),
      handler: 'handler',
      runtime: Runtime.NODEJS_20_X,
      timeout: Duration.seconds(30),
      memorySize: 256,
      logRetention: RetentionDays.ONE_MONTH,
      environment: {
        BUCKET: bucket.bucketName,
        TABLE: installations.tableName,
        BEARER_TOKEN_PARAM: tokenParam.parameterName,
      },
    });

    bucket.grantPut(ingest);
    installations.grantWriteData(ingest);
    tokenParam.grantRead(ingest);

    const url = ingest.addFunctionUrl({
      authType: FunctionUrlAuthType.NONE,
      cors: {
        allowedOrigins: ['*'],
        allowedMethods: [HttpMethod.POST],
        allowedHeaders: ['authorization', 'content-type', 'content-encoding',
                         'x-install-id', 'x-commit', 'x-version'],
      },
    });

    // Cost alarm — email at 80% of $5/month. At 4 testers this should never fire.
    if (notificationEmail) {
      new CfnBudget(this, 'MonthlyBudget', {
        budget: {
          budgetType: 'COST',
          timeUnit: 'MONTHLY',
          budgetLimit: { amount: 5, unit: 'USD' },
          budgetName: 'performance-telemetry',
          costFilters: {
            TagKeyValue: ['user:Project$Performance'],
          },
        },
        notificationsWithSubscribers: [{
          notification: {
            notificationType: 'ACTUAL',
            comparisonOperator: 'GREATER_THAN',
            threshold: 80,
            thresholdType: 'PERCENTAGE',
          },
          subscribers: [{
            subscriptionType: 'EMAIL',
            address: notificationEmail,
          }],
        }],
      });
    }

    new CfnOutput(this, 'IngestUrl', {
      value: url.url,
      description: 'Endpoint the app POSTs session logs to',
    });
    new CfnOutput(this, 'BearerTokenParameterName', {
      value: tokenParam.parameterName,
      description: 'Read via: aws ssm get-parameter --with-decryption --name ...',
    });
    new CfnOutput(this, 'BucketName', {
      value: bucket.bucketName,
      description: 'S3 bucket holding session logs',
    });
    new CfnOutput(this, 'TableName', {
      value: installations.tableName,
      description: 'DynamoDB table with per-installation metadata',
    });
  }
}
