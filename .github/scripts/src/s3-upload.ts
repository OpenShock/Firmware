import * as fs from 'fs';
import * as path from 'path';
import * as stream from 'node:stream';
import mime from 'mime';
import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';
import { setFailed } from '@actions/core';
import * as child_process from 'child_process';

const s3Bucket = process.env.S3_BUCKET;
const s3AccountId = process.env.S3_ACCOUNT_ID;
const s3AccessKeyId = process.env.S3_ACCESS_KEY_ID;
const s3SecretAccessKey = process.env.S3_SECRET_ACCESS_KEY;

if (!s3Bucket || !s3AccountId || !s3AccessKeyId || !s3SecretAccessKey) {
  console.log('S3_BUCKET: ' + (s3Bucket ? 'OK' : 'MISSING'));
  console.log('S3_ACCOUNT_ID: ' + (s3AccountId ? 'OK' : 'MISSING'));
  console.log('S3_ACCESS_KEY_ID: ' + (s3AccessKeyId ? 'OK' : 'MISSING'));
  console.log('S3_SECRET_ACCESS_KEY: ' + (s3SecretAccessKey ? 'OK' : 'MISSING'));
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

async function* walk(dir: string): AsyncGenerator<string> {
  for await (const d of await fs.promises.opendir(dir)) {
    const fileName = path.join(dir, d.name);
    if (d.isDirectory()) yield* walk(fileName);
    else if (d.isFile()) yield fileName;
  }
}

function getFileHashMD5(fileName: string): string {
  const md5sum = child_process.execSync(`md5sum ${fileName}`).toString().trim();

  if (md5sum === undefined) {
    setFailed('Failed to get md5sum');
    process.exit();
  }

  return md5sum.split(' ')[0];
}

async function uploadStream(Body: stream.Readable | Buffer, Key: string, ContentLength: number | undefined = undefined, contentTypeMime: string | null = null, ContentMD5: string | undefined = undefined) {
  const command = new PutObjectCommand({
    Bucket: s3Bucket,
    Key,
    Body,
    ContentType: contentTypeMime ?? 'application/octet-stream',
    ContentLength,
    ContentMD5,
  });

  const response = await S3.send(command);

  console.log(response);
}
async function uploadFile(fileName: string, remoteFileKey: string, contentTypeMime: string | null) {
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

  await uploadStream(fs.createReadStream(fileName), remoteFileKey, fs.statSync(fileName).size, contentTypeMime ?? mime.getType(fileName), getFileHashMD5(fileName));
}
async function uploadDir(dirName: string, remoteDirKey: string) {
  // Check if directory exists
  if (!fs.existsSync(dirName)) {
    setFailed(`Directory ${dirName} does not exist`);
    process.exit();
  }

  // Check if directory is readable
  if (!fs.statSync(dirName).isDirectory()) {
    setFailed(`Directory ${dirName} is not readable`);
    process.exit();
  }

  for await (const fileName of walk(dirName)) {
    const remoteFileKey = `${remoteDirKey}/${fileName.replace(dirName, '')}`;
    await uploadFile(fileName, remoteFileKey, null);
  }
}
async function uploadStdin(remoteFileKey: string, contentTypeMime: string | null) {
  const stdin = fs.readFileSync(0);
  await uploadStream(stdin, remoteFileKey, undefined, contentTypeMime);
}

// Usage:
//    node s3-upload.js dir   <dir-name> <remote-dir-key>
//    node s3-upload.js file  <file-name> <remote-file-key>
//    node s3-upload.js file  <file-name> <remote-file-key> <content-type>
//    node s3-upload.js stdin <remote-file-key>
//    node s3-upload.js stdin <remote-file-key> <content-type>

// Process arguments
const args = process.argv.slice(2);
if (args.length < 2 || args.length > 3) {
  setFailed('Invalid number of arguments');
  process.exit();
}

switch (args[0]) {
  case 'dir':
    if (args.length !== 3) {
      setFailed('Invalid number of arguments');
      process.exit();
    }
    await uploadDir(args[1], args[2]);
    break;
  case 'file':
    if (args.length !== 3 && args.length !== 4) {
      setFailed('Invalid number of arguments');
      process.exit();
    }
    await uploadFile(args[1], args[2], args[3] ?? null);
    break;
  case 'stdin':
    if (args.length !== 2 && args.length !== 3) {
      setFailed('Invalid number of arguments');
      process.exit();
    }
    await uploadStdin(args[1], args[2] ?? null);
    break;
  default:
    setFailed('Invalid argument');
    process.exit();
}
