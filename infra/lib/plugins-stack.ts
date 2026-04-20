import { Stack, StackProps, RemovalPolicy, CfnOutput, Tags } from 'aws-cdk-lib';
import { Construct } from 'constructs';
import {
  Bucket,
  BlockPublicAccess,
  BucketEncryption,
  HttpMethods,
} from 'aws-cdk-lib/aws-s3';
import { PolicyStatement, Effect, AnyPrincipal } from 'aws-cdk-lib/aws-iam';

// Bundled-plugin hosting for the Performance app.
//
// Shape:
//   App (first launch) → HTTPS GET → S3 bucket `performance-plugins-<acct>`
//   Bucket is public-read but NOT public-write/list. Objects:
//     manifest.json                       short TTL, mutable
//     plugins/<slug>-<version>-macos.zip  long TTL, immutable
//
// Rationale:
//   - No CloudFront: tester population is small, direct S3 URLs are fine
//     for a first pass. The bucket policy allows cheap future migration
//     to an OAC-fronted distribution without URL-scheme change.
//   - Tagged Project=Performance so it rolls up into the $50/mo budget
//     defined on the telemetry stack.
//   - Versioned immutable URLs (version + sha256 in the path) let the
//     client cache aggressively; manifest.json is the only indirection
//     we need to update when a plugin bumps.
export class PluginsStack extends Stack {
  constructor(scope: Construct, id: string, props?: StackProps) {
    super(scope, id, props);

    const bucket = new Bucket(this, 'PluginsBucket', {
      bucketName: `performance-plugins-${this.account}`,
      // Allow public-read bucket policy; keep ACL / public listing blocked.
      blockPublicAccess: new BlockPublicAccess({
        blockPublicAcls: true,
        blockPublicPolicy: false,
        ignorePublicAcls: true,
        restrictPublicBuckets: false,
      }),
      encryption: BucketEncryption.S3_MANAGED,
      removalPolicy: RemovalPolicy.RETAIN,
      cors: [
        {
          allowedMethods: [HttpMethods.GET, HttpMethods.HEAD],
          allowedOrigins: ['*'],
          allowedHeaders: ['*'],
          maxAge: 300,
        },
      ],
    });

    bucket.addToResourcePolicy(
      new PolicyStatement({
        effect: Effect.ALLOW,
        principals: [new AnyPrincipal()],
        actions: ['s3:GetObject'],
        resources: [`${bucket.bucketArn}/*`],
      })
    );

    Tags.of(this).add('Project', 'Performance');

    new CfnOutput(this, 'BucketName', {
      value: bucket.bucketName,
      description: 'S3 bucket hosting bundled plugin archives + manifest.json',
    });
    new CfnOutput(this, 'BucketBaseUrl', {
      value: `https://${bucket.bucketName}.s3.${this.region}.amazonaws.com`,
      description: 'Base URL used by the app to fetch manifest.json + archives',
    });
  }
}
