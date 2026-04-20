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

new TelemetryStack(app, 'PerformanceTelemetry', {
  env,
  description: 'Session-log ingest for the Performance macOS app',
});

new PluginsStack(app, 'PerformancePlugins', {
  env,
  description: 'S3 hosting for bundled plugin archives + manifest',
});
