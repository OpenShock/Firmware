import os
import requests
import zipfile
import tempfile
from pathlib import Path


def fetch_latest_flatbuffers_release(output_dir):
    # API URL for the latest release
    api_url = "https://api.github.com/repos/google/flatbuffers/releases/latest"

    try:
        # Fetch the latest release data
        response = requests.get(api_url)
        response.raise_for_status()
        release_data = response.json()

        # Extract assets matching the desired filenames
        assets = release_data.get("assets", [])
        linux_asset = next((asset for asset in assets if "Linux.flatc.binary.clang" in asset["name"]), None)
        windows_asset = next((asset for asset in assets if "Windows.flatc.binary" in asset["name"]), None)

        if not linux_asset or not windows_asset:
            raise ValueError("Required assets not found in the latest release.")

        # Create a temporary directory for downloads
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_dir_path = Path(temp_dir)

            # Download and extract Linux asset
            linux_zip_path = temp_dir_path / linux_asset["name"]
            download_file(linux_asset["browser_download_url"], linux_zip_path)
            extract_zip(linux_zip_path, output_dir)

            # Download and extract Windows asset
            windows_zip_path = temp_dir_path / windows_asset["name"]
            download_file(windows_asset["browser_download_url"], windows_zip_path)
            extract_zip(windows_zip_path, output_dir)

            print(f"Files have been successfully downloaded, extracted to {output_dir}, and temporary files deleted.")

    except requests.RequestException as e:
        print(f"Error fetching release data: {e}")
    except Exception as e:
        print(f"An error occurred: {e}")


def download_file(url, dest_path):
    """Download a file from a URL to the specified destination."""
    response = requests.get(url, stream=True)
    response.raise_for_status()

    with open(dest_path, "wb") as f:
        for chunk in response.iter_content(chunk_size=8192):
            f.write(chunk)
    print(f"Downloaded: {dest_path}")


def extract_zip(zip_path, extract_to):
    """Extract a ZIP file to the specified directory."""
    with zipfile.ZipFile(zip_path, "r") as zip_ref:
        zip_ref.extractall(extract_to)
    print(f"Extracted: {zip_path} to {extract_to}")


if __name__ == "__main__":
    # Specify the output directory for the extracted files
    output_directory = Path(input("Enter the directory to extract the files to: ")).resolve()

    if not output_directory.exists():
        output_directory.mkdir(parents=True)

    fetch_latest_flatbuffers_release(output_directory)
