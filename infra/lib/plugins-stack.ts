import { Stack, StackProps, RemovalPolicy, CfnOutput, Tags } from 'aws-cdk-lib';
import { Construct } from 'constructs';
import {
  Bucket,
  IBucket,
  BlockPublicAccess,
  BucketEncryption,
} from 'aws-cdk-lib/aws-s3';

// Bundled-plugin hosting for the Performance app.
//
// Shape:
//   App → PerformanceTelemetry.ChatProxy-style Lambda (plugins handler) →
//     returns manifest.json with presigned S3 GET URLs →
//     App downloads each archive directly from S3 via the presigned URL.
//
// The bucket itself is fully private — no public read, no public ACLs.
// The Lambda in the telemetry stack is granted read access and issues
// short-lived (1h) presigned URLs scoped to the archives the app needs.
// That makes hotlinking and drive-by scraping a non-issue without
// putting any AWS credentials in the app.
//
// Tagged Project=Performance so traffic rolls up into the $50/mo budget
// defined on the telemetry stack.
export class PluginsStack extends Stack {
  readonly bucket: IBucket;

  constructor(scope: Construct, id: string, props?: StackProps) {
    super(scope, id, props);

    this.bucket = new Bucket(this, 'PluginsBucket', {
      bucketName: `performance-plugins-${this.account}`,
      blockPublicAccess: BlockPublicAccess.BLOCK_ALL,
      encryption: BucketEncryption.S3_MANAGED,
      removalPolicy: RemovalPolicy.RETAIN,
    });

    Tags.of(this).add('Project', 'Performance');

    new CfnOutput(this, 'BucketName', {
      value: this.bucket.bucketName,
      description: 'S3 bucket hosting bundled plugin archives + manifest.json (private)',
    });
  }
}
