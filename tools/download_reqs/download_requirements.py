#!/usr/bin/env python3
"""
Download Requirements Script
Downloads application requirements from a remote server using rsync with MD5 verification.
"""

import argparse
import hashlib
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

try:
    import yaml
except ImportError:
    print("Error: PyYAML is not installed. Please run: pip install pyyaml", file=sys.stderr)
    sys.exit(1)


@dataclass
class ConnectionConfig:
    """Remote connection configuration"""
    host: str
    username: str


@dataclass
class Requirement:
    """A single file requirement"""
    name: str
    source: Dict[str, str]  # platform -> remote_path
    destination: str
    checksum: Dict[str, str]  # platform -> md5_checksum


@dataclass
class Target:
    """A target application with its requirements"""
    name: str
    enabled: bool
    description: str
    platforms: List[str]
    requirements: List[Requirement]


class DownloadRequirementsError(Exception):
    """Base exception for download requirements errors"""
    pass


class ChecksumMismatchError(DownloadRequirementsError):
    """Raised when file checksum doesn't match expected value"""
    pass


class RequirementFileNotFoundError(DownloadRequirementsError):
    """Raised when required file is not found"""
    pass


class DownloadManager:
    """Manages downloading and verification of requirements files"""

    def __init__(self, config_path: str, workspace_root: str = None):
        """
        Initialize the download manager

        Args:
            config_path: Path to the YAML configuration file
            workspace_root: Root directory of the workspace (defaults to parent of config dir)
        """
        self.config_path = Path(config_path)
        if not self.config_path.exists():
            raise RequirementFileNotFoundError(f"Configuration file not found: {config_path}")

        with open(self.config_path, 'r') as f:
            config_data = yaml.safe_load(f)

        # Parse connection config
        conn = config_data['connection']
        self.connection = ConnectionConfig(host=conn['host'], username=conn['username'])

        # Parse targets
        self.targets: Dict[str, Target] = {}
        for target_name, target_data in config_data['targets'].items():
            requirements = []
            for req_data in target_data.get('requirements', []):
                requirement = Requirement(name=req_data['name'], source=req_data['source'],
                                         destination=req_data['destination'], checksum=req_data['checksum'])
                requirements.append(requirement)

            target = Target(name=target_name, enabled=target_data.get('enabled', False),
                           description=target_data['description'], platforms=target_data['platforms'],
                           requirements=requirements)
            self.targets[target_name] = target

        # Set workspace root (where files will be downloaded relative to)
        if workspace_root:
            self.workspace_root = Path(workspace_root)
        else:
            # Default to media-library root (two levels up from download_reqs/)
            self.workspace_root = self.config_path.parent.parent.parent

    def calculate_md5(self, file_path: str) -> str:
        """Calculate MD5 checksum of a file"""
        md5_hash = hashlib.md5()
        with open(file_path, 'rb') as f:
            for chunk in iter(lambda: f.read(4096), b''):
                md5_hash.update(chunk)
        return md5_hash.hexdigest()

    def verify_checksum(self, file_path: str, expected_checksum: str) -> None:
        """Verify file checksum matches expected value"""
        if not os.path.exists(file_path):
            raise RequirementFileNotFoundError(f"File not found: {file_path}")

        actual_checksum = self.calculate_md5(file_path)
        if actual_checksum != expected_checksum:
            raise ChecksumMismatchError(f"Checksum mismatch for {file_path}\n"
                                       f"  Expected: {expected_checksum}\n"
                                       f"  Actual:   {actual_checksum}")

    def _is_http_prefix(self, path: str) -> bool:
        """Check if path is an HTTP/HTTPS URL."""
        return path.startswith('http://') or path.startswith('https://')

    def _download_http(self, url: str, local_path: str) -> None:
        """Download a file via wget."""
        wget_cmd = ['wget', '-q', '--no-check-certificate', '-O', local_path, url]
        try:
            subprocess.run(wget_cmd, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
        except subprocess.CalledProcessError as e:
            raise DownloadRequirementsError(f"wget failed: {e.stderr}")

    def _download_rsync(self, remote_path: str, local_path: str) -> None:
        """Download a file via rsync."""
        rsync_cmd = ['rsync', '-atcPL', '--progress',
                     f'{self.connection.username}@{self.connection.host}:{remote_path}', local_path]
        try:
            subprocess.run(rsync_cmd, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
        except subprocess.CalledProcessError as e:
            raise DownloadRequirementsError(f"rsync failed: {e.stderr}")

    def download_file(self, remote_path: str, local_path: str, dry_run: bool = False) -> bool:
        """Download a file using wget (for URLs) or rsync (for server paths)."""
        if dry_run:
            print(f"[DRY-RUN] Would download: {remote_path} -> {local_path}")
            return True

        print(f"Downloading: {remote_path}")
        print(f"         to: {local_path}")

        # Create destination directory if it doesn't exist
        os.makedirs(os.path.dirname(local_path), exist_ok=True)

        try:
            if self._is_http_prefix(remote_path):
                self._download_http(remote_path, local_path)
            else:
                self._download_rsync(remote_path, local_path)
            print("Download complete")
            return True
        except DownloadRequirementsError:
            raise
        except Exception as e:
            print(f"Download failed: {e}", file=sys.stderr)
            raise DownloadRequirementsError(f"Download failed: {e}")

    def process_requirement(self, req: Requirement, platform: str, dry_run: bool = False,
                           verify_only: bool = False, force: bool = False) -> None:
        """Process a single requirement (download and verify)"""
        # Get platform-specific source path
        if platform not in req.source:
            print(f"Skipping {req.name}: No source path defined for platform {platform}")
            return
        remote_path = req.source[platform]

        # Get platform-specific checksum
        if platform not in req.checksum:
            print(f"Skipping {req.name}: No checksum defined for platform {platform}")
            return
        expected_checksum = req.checksum[platform]

        # Construct local path
        local_path = self.workspace_root / req.destination / req.name

        print(f"\n--- Processing: {req.name} ---")

        # Check if file already exists
        if local_path.exists() and not force:
            print(f"File exists: {local_path}")
            try:
                self.verify_checksum(str(local_path), expected_checksum)
                print(f"Checksum verified: {expected_checksum}")
                if verify_only:
                    return
                print("Skipping download (file exists with correct checksum)")
                return
            except ChecksumMismatchError as e:
                if verify_only:
                    raise
                print(f"Checksum mismatch, will re-download")
                print(f"  {e}")
        elif not local_path.exists() and verify_only:
            raise RequirementFileNotFoundError(f"File not found: {local_path}")

        # Download file
        if not verify_only:
            self.download_file(remote_path, str(local_path), dry_run)

            if not dry_run:
                # Verify checksum after download
                try:
                    self.verify_checksum(str(local_path), expected_checksum)
                    print(f"Checksum verified: {expected_checksum}")
                except ChecksumMismatchError as e:
                    local_path.unlink()
                    raise DownloadRequirementsError(f"Downloaded file has incorrect checksum. File deleted.\n{e}")

    def process_target(self, target_name: str, platform: str, dry_run: bool = False,
                      verify_only: bool = False, force: bool = False) -> None:
        """Process all requirements for a target"""
        if target_name not in self.targets:
            raise DownloadRequirementsError(f"Unknown target: {target_name}")

        target = self.targets[target_name]

        if not target.enabled:
            print(f"Target '{target_name}' is disabled in configuration")
            return

        if platform not in target.platforms:
            raise DownloadRequirementsError(f"Target '{target_name}' does not support platform '{platform}'\n"
                                           f"Supported platforms: {', '.join(target.platforms)}")

        print(f"\n{'='*60}")
        print(f"Target: {target_name}")
        print(f"Description: {target.description}")
        print(f"Platform: {platform}")
        print(f"{'='*60}")

        if not target.requirements:
            print("No requirements defined for this target")
            return

        # Process each requirement
        errors = []
        for req in target.requirements:
            try:
                self.process_requirement(req, platform, dry_run, verify_only, force)
            except DownloadRequirementsError as e:
                errors.append((req.name, str(e)))

        # Report results
        print(f"\n{'='*60}")
        if errors:
            print(f"Completed with {len(errors)} error(s):")
            for name, error in errors:
                print(f"  - {name}: {error}")
            raise DownloadRequirementsError(f"Failed to process {len(errors)} file(s)")
        else:
            print("All files processed successfully")
        print(f"{'='*60}\n")

    def list_targets(self) -> None:
        """List all available targets"""
        print("\nAvailable targets:")
        print(f"{'='*60}")
        for target_name, target in self.targets.items():
            status = "enabled" if target.enabled else "disabled"
            platforms = ", ".join(target.platforms)
            print(f"\n{target_name} ({status})")
            print(f"  Description: {target.description}")
            print(f"  Platforms: {platforms}")
            print(f"  Requirements: {len(target.requirements)} file(s)")
        print(f"\n{'='*60}\n")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='Download application requirements from remote server',
                                    formatter_class=argparse.RawDescriptionHelpFormatter,
                                    epilog="""
Examples:
  # List all available targets
  %(prog)s --list-targets

  # Download all requirements for a specific target
  %(prog)s --target webserver --platform hailo15h

  # Download multiple targets
  %(prog)s --target dynamic_privacy_mask,webserver --platform hailo15l

  # Download all enabled targets
  %(prog)s --all --platform hailo15h

  # Verify existing files without downloading
  %(prog)s --target webserver --platform hailo15h --verify-only

  # Preview what would be downloaded (dry-run)
  %(prog)s --target webserver --platform hailo15h --dry-run

  # Force re-download even if files exist
  %(prog)s --target webserver --platform hailo15h --force
        """
    )

    parser.add_argument('--config', default=None,
                       help='Path to configuration YAML file (default: download_requirements.yaml in script directory)')
    parser.add_argument('--target', help='Target(s) to download (comma-separated for multiple)')
    parser.add_argument('--platform', choices=['hailo15h', 'hailo15l'],
                       help='Platform to download for (required unless --list-targets)')
    parser.add_argument('--all', action='store_true', help='Download all enabled targets')
    parser.add_argument('--list-targets', action='store_true', help='List all available targets and exit')
    parser.add_argument('--verify-only', action='store_true', help='Only verify existing files, do not download')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be downloaded without actually downloading')
    parser.add_argument('--force', action='store_true', help='Force re-download even if file exists with correct checksum')
    parser.add_argument('--workspace-root', help='Root directory of the workspace (default: auto-detected)')

    args = parser.parse_args()

    config_path = args.config if args.config else Path(__file__).parent / 'download_requirements.yaml'

    try:
        # Initialize download manager
        manager = DownloadManager(str(config_path), args.workspace_root)

        # Handle list targets
        if args.list_targets:
            manager.list_targets()
            return 0

        # Validate arguments
        if not args.platform:
            parser.error("--platform is required (unless using --list-targets)")

        if not args.target and not args.all:
            parser.error("Either --target or --all must be specified")

        if args.target and args.all:
            parser.error("Cannot specify both --target and --all")

        # Determine which targets to process
        if args.all:
            targets_to_process = [name for name, target in manager.targets.items()
                                 if target.enabled and args.platform in target.platforms]
            if not targets_to_process:
                print(f"No enabled targets found for platform {args.platform}")
                return 0
        else:
            targets_to_process = [t.strip() for t in args.target.split(',')]

        # Process each target
        for target_name in targets_to_process:
            manager.process_target(target_name, args.platform, dry_run=args.dry_run,
                                 verify_only=args.verify_only, force=args.force)

        return 0

    except DownloadRequirementsError as e:
        print(f"\nError: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n\nInterrupted by user", file=sys.stderr)
        return 130
    except Exception as e:
        print(f"\nUnexpected error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())
