#!/usr/bin/env node
import 'source-map-support/register';
import * as cdk from 'aws-cdk-lib';
import { TelemetryStack } from '../lib/telemetry-stack';
import { PluginsStack } from '../lib/plugins-stack';

const app = new cdk.App();

const env = {
  account: process.env.CDK_DEFAULT_ACCOUNT,
  region: process.env.CDK_DEFAULT_REGION ?? 'us-east-1',
};

// Plugins bucket first — TelemetryStack's PluginsProxy Lambda needs a
// reference to it so it can read manifest.json and issue presigned
// URLs. The bucket itself is fully private (BlockPublicAccess.BLOCK_ALL);
// all external access is brokered by the Lambda.
const pluginsStack = new PluginsStack(app, 'PerformancePlugins', {
  env,
  description: 'Private S3 bucket hosting bundled plugin archives + manifest',
});

new TelemetryStack(app, 'PerformanceTelemetry', {
  env,
  description: 'Session-log ingest, chat proxy, and plugins proxy for the Performance macOS app',
  pluginsBucket: pluginsStack.bucket,
});
