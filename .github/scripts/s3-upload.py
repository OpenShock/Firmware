import io
import os
import uuid
import boto3
import github

if __name__ != '__main__':
    print('This script is not meant to be imported.')
    exit(1)

github.ensure_ci('This script is meant to be run in GitHub Actions.')

s3_bucket = os.environ.get('S3_BUCKET')
s3_account_id = os.environ.get('S3_ACCOUNT_ID')
s3_access_key_id = os.environ.get('S3_ACCESS_KEY_ID')
s3_secret_access_key = os.environ.get('S3_SECRET_ACCESS_KEY')

if s3_bucket is None or s3_account_id is None or s3_access_key_id is None or s3_secret_access_key is None:
    print('One or more environment variables not found.')
    exit(1)

client = boto3.client('s3', aws_access_key_id=s3_access_key_id, aws_secret_access_key=s3_secret_access_key)
