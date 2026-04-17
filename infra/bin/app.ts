#!/usr/bin/env node
import 'source-map-support/register';
import * as cdk from 'aws-cdk-lib';
import { TelemetryStack } from '../lib/telemetry-stack';

const app = new cdk.App();

new TelemetryStack(app, 'PerformanceTelemetry', {
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: process.env.CDK_DEFAULT_REGION ?? 'us-east-1',
  },
  description: 'Session-log ingest for the Performance macOS app',
});
