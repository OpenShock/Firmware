import * as fs from 'fs';
import * as path from 'path';
import mime from 'mime';
import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';
import { setFailed } from '@actions/core';
import * as child_process from 'child_process';

const s3AccountId = process.env.S3_ACCOUNT_ID;
const s3AccessKeyId = process.env.S3_ACCESS_KEY_ID;
const s3SecretAccessKey = process.env.S3_SECRET_ACCESS_KEY;
const bucketName = process.env.AWS_BUCKET_NAME;

if (!s3AccessKeyId || !s3SecretAccessKey || !bucketName) {
  setFailed('AWS credentials are required');
  process.exit();
}

const S3 = new S3Client({
  region: 'auto',
  endpoint: `https://${s3AccountId}.r2.cloudflarestorage.com`,
  credentials: {
    accessKeyId: s3AccessKeyId,
    secretAccessKey: s3SecretAccessKey,
  },
});

function getFileHashMD5(fileName: string): string {
  const md5sum = child_process.execSync(`md5sum ${fileName}`).toString().trim();

  if (md5sum === undefined) {
    setFailed('Failed to get md5sum');
    process.exit();
  }

  return md5sum.split(' ')[0];
}

async function uploadFile(fileName: string, contentTypeMime: string | null, remoteFileKey: string) {
  // Check if file exists
  if (!fs.existsSync(fileName)) {
    setFailed(`File ${fileName} does not exist`);
    process.exit();
  }

  // Check if file is readable
  if (!fs.statSync(fileName).isFile()) {
    setFailed(`File ${fileName} is not readable`);
    process.exit();
  }

  const command = new PutObjectCommand({
    Bucket: bucketName,
    Key: remoteFileKey,
    Body: fs.createReadStream(fileName),
    ContentType: contentTypeMime ?? mime.getType(fileName) ?? 'application/octet-stream',
    ContentLength: fs.statSync(fileName).size,
    ContentMD5: getFileHashMD5(fileName),
  });

  const response = await S3.send(command);

  console.log(response);
}

// Usage:
//    node s3-upload.js <dir-name> <remote-dir-key>
//    node s3-upload.js <file-name> <remote-file-key>
//    node s3-upload.js <file-name> <content-type> <remote-file-key>

// Process arguments
const args = process.argv.slice(2);
if (args.length < 2 || args.length > 3) {
  setFailed('Invalid number of arguments');
  process.exit();
}

let fileOrDirName: string;
let contentTypeMime: string | null = null;
let remoteFileOrDirKey: string;

if (args.length === 2) {
  [fileOrDirName, remoteFileOrDirKey] = args;
} else {
  [fileOrDirName, contentTypeMime, remoteFileOrDirKey] = args;
}

if (!fs.existsSync(fileOrDirName)) {
  setFailed(`File or directory ${fileOrDirName} does not exist`);
  process.exit();
}

async function* walk(dir: string): AsyncGenerator<string> {
  for await (const d of await fs.promises.opendir(dir)) {
    const fileName = path.join(dir, d.name);
    if (d.isDirectory()) yield* walk(fileName);
    else if (d.isFile()) yield fileName;
  }
}

if (fs.statSync(fileOrDirName).isFile()) {
  await uploadFile(fileOrDirName, contentTypeMime, remoteFileOrDirKey);
} else {
  for await (const fileName of walk(fileOrDirName)) {
    const remoteFileKey = `${remoteFileOrDirKey}/${fileName.replace(fileOrDirName, '')}`;
    await uploadFile(fileName, null, remoteFileKey);
  }
}
