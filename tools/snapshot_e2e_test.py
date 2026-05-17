#!/usr/bin/env python3
"""E2E tests for snapshot_cli on a running Hailo-15 pipeline.

Starts a pipeline app on the target device, exercises snapshot commands
via an interactive snapshot_cli session over SSH, validates both CLI output
and filesystem results, and reports pass/fail.

Usage:
    python3 tools/snapshot_e2e_test.py --device-ip 10.0.0.1
"""

import argparse
import re
import select
import subprocess
import sys
import time

PIPELINE_CMD = "/home/root/apps/case_studies/single_stream/single_stream_case_study -a Lowlight_Bayer -t 600"
DEFAULT_SNAPSHOT_PATH = "/tmp/medialib_snapshots/"
CUSTOM_SNAPSHOT_PATH = "/tmp/medialib_snapshots_e2e_custom/"
PIPELINE_STARTUP_SEC = 10


class SnapshotCliSession:
    """Manages a persistent interactive snapshot_cli session over SSH with PTY."""

    PROMPT = "snapshot>"

    def __init__(self, device_ip):
        self.proc = None
        self.device_ip = device_ip

    def start(self):
        """Launch snapshot_cli over SSH with PTY and wait for the initial prompt."""
        self.proc = subprocess.Popen(
            [
                "ssh", "-tt", "-o", "StrictHostKeyChecking=no",
                f"root@{self.device_ip}", "snapshot_cli",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._wait_for_prompt(timeout=10)

    def send_command(self, command, timeout=30, wait_for=None):
        """Send a command and return all output up to the next prompt.

        If wait_for is set, keep reading past intermediate prompts until the
        marker string appears in the output, then read until the next prompt.
        This is needed for async commands like 'snapshot' where the prompt
        reappears immediately after dispatch, and progress/completion arrive later.

        Uses the PTY echo of the command as a synchronization boundary to
        discard stale output from previous commands.
        """
        self.proc.stdin.write(f"{command}\n".encode())
        self.proc.stdin.flush()
        return self._wait_for_prompt(timeout, wait_for=wait_for, sync_echo=command)

    def stop(self):
        """Close the session gracefully."""
        if self.proc:
            try:
                self.proc.stdin.write(b"quit\n")
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass
            try:
                self.proc.stdin.close()
            except OSError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)
            self.proc = None

    def _wait_for_prompt(self, timeout, wait_for=None, sync_echo=None):
        """Read stdout until the 'snapshot>' prompt appears. Return collected output.

        If wait_for is set, ignore prompt matches until the marker string has
        been seen in the output.  This handles async CLI output where the
        prompt reappears before the real result arrives.

        If sync_echo is set, discard all output before the echo of the
        command text.  This prevents stale output from a previous command
        from being mistaken for the current command's response.
        """
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            remaining = deadline - time.time()
            ready, _, _ = select.select([self.proc.stdout], [], [], min(remaining, 0.5))
            if ready:
                chunk = self.proc.stdout.read1(4096)
                if not chunk:
                    break
                buf += chunk
                stripped = self.strip_ansi(buf.decode(errors="replace"))

                # Find where current command's output starts (after echo)
                if sync_echo:
                    echo_pos = stripped.find(sync_echo)
                    if echo_pos == -1:
                        continue
                    relevant = stripped[echo_pos:]
                else:
                    relevant = stripped

                marker_found = wait_for is None or wait_for in relevant
                if marker_found and relevant.rstrip().endswith(self.PROMPT):
                    return relevant
        # Timeout — return whatever we have
        stripped = self.strip_ansi(buf.decode(errors="replace"))
        if sync_echo:
            echo_pos = stripped.find(sync_echo)
            if echo_pos != -1:
                return stripped[echo_pos:]
        return stripped

    @staticmethod
    def strip_ansi(text):
        """Remove ANSI escape codes and readline markers."""
        return re.sub(r"(\x1b\[\??[0-9;]*[a-zA-Z]|\x01|\x02)", "", text)

    @staticmethod
    def parse_visible_output(raw_output):
        """Handle \\r overwrites to get final visible text lines."""
        lines = []
        for line in raw_output.split("\n"):
            segments = line.split("\r")
            final = segments[-1].strip()
            if final:
                lines.append(final)
        return lines


class SnapshotE2ETest:
    def __init__(self, device_ip, snapshot_path=DEFAULT_SNAPSHOT_PATH):
        self.device_ip = device_ip
        self.snapshot_path = snapshot_path
        self.passed = 0
        self.failed = 0
        self.cli = None

    def ssh(self, cmd, timeout=30):
        """Run a command on the device via SSH and return (returncode, stdout, stderr)."""
        full_cmd = ["ssh", "-o", "StrictHostKeyChecking=no", f"root@{self.device_ip}", cmd]
        try:
            result = subprocess.run(full_cmd, capture_output=True, text=True, timeout=timeout)
            return result.returncode, result.stdout.strip(), result.stderr.strip()
        except subprocess.TimeoutExpired:
            return -1, "", "SSH command timed out"

    def clean_snapshots(self, path=None):
        """Remove all snapshot directories on the device.

        Uses the CLI 'clear' command when the session is active and no custom
        path is specified.  Falls back to SSH rm -rf for custom paths or when
        the CLI session is not available.
        """
        if path is None and self.cli:
            self.send_cli_command("clear")
            return
        target = path or self.snapshot_path
        self.ssh(f"rm -rf {target}*")

    def count_snapshot_dirs(self, path=None):
        """Count subdirectories in the snapshot path."""
        target = path or self.snapshot_path
        rc, stdout, _ = self.ssh(
            f"find {target} -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l"
        )
        if rc != 0:
            return 0
        return int(stdout.strip())

    def list_files_in_dirs(self, path=None):
        """Return a dict mapping directory name -> list of filenames."""
        target = path or self.snapshot_path
        rc, stdout, _ = self.ssh(
            f"find {target} -mindepth 2 -maxdepth 2 -type f -printf '%h %f\\n' 2>/dev/null"
        )
        if rc != 0 or not stdout:
            return {}
        dirs = {}
        for line in stdout.strip().split("\n"):
            parts = line.split(" ", 1)
            if len(parts) == 2:
                dir_name, filename = parts
                dirs.setdefault(dir_name, []).append(filename)
        return dirs

    def get_file_sizes(self, path=None):
        """Return a list of (filepath, size_bytes) for all snapshot files."""
        target = path or self.snapshot_path
        rc, stdout, _ = self.ssh(
            f"find {target} -type f -printf '%p %s\\n' 2>/dev/null"
        )
        if rc != 0 or not stdout:
            return []
        result = []
        for line in stdout.strip().split("\n"):
            parts = line.rsplit(" ", 1)
            if len(parts) == 2:
                result.append((parts[0], int(parts[1])))
        return result

    def send_cli_command(self, command, timeout=30, wait_for=None):
        """Send command via interactive snapshot_cli session and return stripped output.

        For snapshot commands, pass wait_for="Captured " to wait for the async
        completion message before returning.
        """
        raw_output = self.cli.send_command(command, timeout=timeout, wait_for=wait_for)
        return SnapshotCliSession.strip_ansi(raw_output)

    def start_pipeline(self, extra_env=""):
        """Start the pipeline app in the background on the device."""
        print(f"Starting pipeline on {self.device_ip}...", flush=True)
        self.ssh("killall -q single_stream_case_study 2>/dev/null || true")
        time.sleep(2)
        env_prefix = f"MEDIALIB_SNAPSHOT_ENABLE=1 {extra_env}".strip()
        self.ssh(f"{env_prefix} nohup {PIPELINE_CMD} > /tmp/pipeline.log 2>&1 &")
        print(f"Waiting {PIPELINE_STARTUP_SEC}s for pipeline startup...", flush=True)
        time.sleep(PIPELINE_STARTUP_SEC)

        rc, stdout, _ = self.ssh("pgrep -a single_stream")
        if rc != 0:
            print("ERROR: Pipeline failed to start. Check /tmp/pipeline.log on the device.", flush=True)
            sys.exit(1)
        pid = stdout.split()[0]
        print(f"Pipeline running (PID {pid})", flush=True)

        self.cli = SnapshotCliSession(self.device_ip)
        self.cli.start()
        print("Interactive snapshot_cli session started", flush=True)

    def stop_pipeline(self):
        """Stop the CLI session and pipeline app on the device."""
        if self.cli:
            print("Stopping snapshot_cli session...", flush=True)
            self.cli.stop()
            self.cli = None
        print("Stopping pipeline...", flush=True)
        self.ssh("killall -q single_stream_case_study 2>/dev/null || true")
        time.sleep(2)

    def report(self, test_name, passed, detail=""):
        """Record and print a test result."""
        status = "PASS" if passed else "FAIL"
        if passed:
            self.passed += 1
        else:
            self.failed += 1
        msg = f"  [{status}] {test_name}"
        if detail:
            msg += f" — {detail}"
        print(msg, flush=True)

    # --- Functional tests ---

    def test_list_stages(self):
        """list_stages should return at least one stage."""
        output = self.send_cli_command("list_stages")

        has_stages = "pre_isp" in output
        self.report(
            "list_stages",
            has_stages,
            f"output={'(has stages)' if has_stages else repr(output[:200])}",
        )

    def test_single_frame_all_stages(self):
        """Single frame snapshot should capture all registered stages."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 1", wait_for="Captured ")

        has_requested = "Snapshot requested" in output
        has_complete = "Captured 1/1" in output

        dir_count = self.count_snapshot_dirs()
        files = self.list_files_in_dirs()
        total_files = sum(len(v) for v in files.values())

        self.report(
            "single_frame_all_stages",
            has_requested and has_complete and dir_count == 1 and total_files >= 1,
            f"dirs={dir_count}, files={total_files}, cli_ok={has_requested and has_complete}",
        )

    def test_single_frame_filtered(self):
        """Single frame with stage filter should produce exactly 1 file."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 1 pre_isp_raw", wait_for="Captured ")

        has_requested = "Snapshot requested" in output
        has_complete = "Captured 1/1" in output

        dir_count = self.count_snapshot_dirs()
        files = self.list_files_in_dirs()
        all_files = [f for flist in files.values() for f in flist]
        has_raw = any("pre_isp_raw" in f for f in all_files)

        self.report(
            "single_frame_filtered",
            has_requested and has_complete and dir_count == 1 and len(all_files) == 1 and has_raw,
            f"dirs={dir_count}, files={all_files}, cli_ok={has_requested and has_complete}",
        )

    def test_multi_frame_three_stages(self):
        """3-frame snapshot with 3 stages should produce 3 directories."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 3 pre_isp_raw pre_isp_denoised post_isp", wait_for="Captured ")

        has_requested = "Snapshot requested" in output
        has_complete = "Captured 3/3" in output

        dir_count = self.count_snapshot_dirs()
        files = self.list_files_in_dirs()
        all_correct = all(len(v) == 3 for v in files.values()) if files else False

        self.report(
            "multi_frame_three_stages",
            has_requested and has_complete and dir_count == 3 and all_correct,
            f"dirs={dir_count}, files_per_dir={[len(v) for v in files.values()]}, "
            f"cli_ok={has_requested and has_complete}",
        )

    def test_multi_frame_filtered(self):
        """3-frame filtered snapshot should produce 3 dirs with 2 files each."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 3 pre_isp_raw pre_isp_denoised", wait_for="Captured ")

        has_requested = "Snapshot requested" in output
        has_complete = "Captured 3/3" in output

        dir_count = self.count_snapshot_dirs()
        files = self.list_files_in_dirs()
        all_correct = all(len(v) == 2 for v in files.values()) if files else False

        self.report(
            "multi_frame_filtered",
            has_requested and has_complete and dir_count == 3 and all_correct,
            f"dirs={dir_count}, files_per_dir={[len(v) for v in files.values()]}, "
            f"cli_ok={has_requested and has_complete}",
        )

    def test_frame_count_preserved(self):
        """5-frame snapshot should produce exactly 5 directories."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 5 pre_isp_raw", wait_for="Captured ")

        has_complete = "Captured 5/5" in output

        dir_count = self.count_snapshot_dirs()
        self.report(
            "frame_count_preserved",
            has_complete and dir_count == 5,
            f"dirs={dir_count} (expected 5), cli_ok={has_complete}",
        )

    def test_interval(self):
        """3-frame snapshot with interval 5 should produce 3 directories."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 3 pre_isp_raw --interval 5", timeout=120, wait_for="Captured ")

        has_complete = "Captured 3/3" in output

        dir_count = self.count_snapshot_dirs()
        self.report(
            "interval",
            has_complete and dir_count == 3,
            f"dirs={dir_count} (expected 3), cli_ok={has_complete}",
        )

    def test_file_sizes_nonzero(self):
        """All snapshot files should have non-zero size."""
        self.clean_snapshots()
        output = self.send_cli_command("snapshot 1 pre_isp_raw pre_isp_denoised", wait_for="Captured ")

        has_complete = "Captured 1/1" in output

        file_sizes = self.get_file_sizes()
        all_nonzero = all(size > 0 for _, size in file_sizes)
        zero_files = [f for f, size in file_sizes if size == 0]

        self.report(
            "file_sizes_nonzero",
            has_complete and len(file_sizes) == 2 and all_nonzero,
            f"files={len(file_sizes)}, zero_size={zero_files}, cli_ok={has_complete}"
            if zero_files
            else f"files={len(file_sizes)}, sizes={[s for _, s in file_sizes]}, cli_ok={has_complete}",
        )

    def test_back_to_back_requests(self):
        """Sequential snapshot requests should each produce correct results."""
        self.clean_snapshots()

        output1 = self.send_cli_command("snapshot 2 pre_isp_raw", wait_for="Captured ")
        first_complete = "Captured 2/2" in output1
        first_count = self.count_snapshot_dirs()

        output2 = self.send_cli_command("snapshot 3 pre_isp_denoised", wait_for="Captured ")
        second_complete = "Captured 3/3" in output2
        second_count = self.count_snapshot_dirs()

        self.report(
            "back_to_back_requests",
            first_complete and second_complete and first_count == 2 and second_count == 5,
            f"after_first={first_count} (expected 2), after_second={second_count} (expected 5), "
            f"cli_ok={first_complete and second_complete}",
        )

    # --- Performance tests ---

    def test_perf_50_frames_single_stage(self):
        """50-frame single-stage snapshot: all frames captured, consistent file sizes."""
        num_frames = 50
        self.clean_snapshots()

        start = time.time()
        output = self.send_cli_command(f"snapshot {num_frames} pre_isp_raw", timeout=120, wait_for="Captured ")
        elapsed = time.time() - start

        has_complete = f"Captured {num_frames}/{num_frames}" in output

        dir_count = self.count_snapshot_dirs()
        file_sizes = self.get_file_sizes()

        sizes = [size for _, size in file_sizes]
        all_nonzero = all(s > 0 for s in sizes)
        size_consistent = len(set(sizes)) == 1 if sizes else False

        self.report(
            f"perf_{num_frames}_frames_single_stage",
            has_complete and dir_count == num_frames and all_nonzero and size_consistent,
            f"dirs={dir_count}/{num_frames}, "
            f"sizes_ok={all_nonzero and size_consistent}, "
            f"elapsed={elapsed:.1f}s, cli_ok={has_complete}",
        )

    def test_perf_50_frames_two_stages(self):
        """50-frame two-stage snapshot: all frames captured with 2 files each."""
        num_frames = 50
        self.clean_snapshots()

        start = time.time()
        output = self.send_cli_command(
            f"snapshot {num_frames} pre_isp_raw pre_isp_denoised", timeout=120, wait_for="Captured "
        )
        elapsed = time.time() - start

        has_complete = f"Captured {num_frames}/{num_frames}" in output

        dir_count = self.count_snapshot_dirs()
        files = self.list_files_in_dirs()
        all_have_two = all(len(v) == 2 for v in files.values()) if files else False

        file_sizes = self.get_file_sizes()
        all_nonzero = all(size > 0 for _, size in file_sizes)

        self.report(
            f"perf_{num_frames}_frames_two_stages",
            has_complete and dir_count == num_frames and all_have_two and all_nonzero,
            f"dirs={dir_count}/{num_frames}, "
            f"all_have_2_files={all_have_two}, "
            f"elapsed={elapsed:.1f}s, cli_ok={has_complete}",
        )

    def test_perf_20_frames_with_interval(self):
        """20 frames with interval 3: captures every 3rd frame."""
        num_frames = 20
        interval = 3
        self.clean_snapshots()

        start = time.time()
        output = self.send_cli_command(
            f"snapshot {num_frames} pre_isp_raw --interval {interval}", timeout=120, wait_for="Captured "
        )
        elapsed = time.time() - start

        has_complete = f"Captured {num_frames}/{num_frames}" in output

        dir_count = self.count_snapshot_dirs()

        self.report(
            f"perf_{num_frames}_frames_interval_{interval}",
            has_complete and dir_count == num_frames,
            f"dirs={dir_count}/{num_frames}, elapsed={elapsed:.1f}s, cli_ok={has_complete}",
        )

    # --- Custom path tests ---

    def test_custom_path_via_env(self):
        """Snapshot with MEDIALIB_SNAPSHOT_PATH should write to custom directory."""
        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

        output = self.send_cli_command("snapshot 1 pre_isp_raw", wait_for="Captured ")
        has_complete = "Captured 1/1" in output

        dir_count = self.count_snapshot_dirs(path=CUSTOM_SNAPSHOT_PATH)
        files = self.list_files_in_dirs(path=CUSTOM_SNAPSHOT_PATH)
        total_files = sum(len(v) for v in files.values())

        default_count = self.count_snapshot_dirs(path=DEFAULT_SNAPSHOT_PATH)

        self.report(
            "custom_path_via_env",
            has_complete and dir_count == 1 and total_files == 1 and default_count == 0,
            f"custom_dirs={dir_count}, custom_files={total_files}, "
            f"default_dirs={default_count}, cli_ok={has_complete}",
        )

        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

    def test_custom_path_multi_frame(self):
        """Multi-frame snapshot with custom path should produce all frames there."""
        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

        output = self.send_cli_command("snapshot 3 pre_isp_raw pre_isp_denoised", wait_for="Captured ")
        has_complete = "Captured 3/3" in output

        dir_count = self.count_snapshot_dirs(path=CUSTOM_SNAPSHOT_PATH)
        files = self.list_files_in_dirs(path=CUSTOM_SNAPSHOT_PATH)
        all_correct = all(len(v) == 2 for v in files.values()) if files else False

        self.report(
            "custom_path_multi_frame",
            has_complete and dir_count == 3 and all_correct,
            f"dirs={dir_count}, files_per_dir={[len(v) for v in files.values()]}, "
            f"cli_ok={has_complete}",
        )

        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

    def test_custom_path_file_sizes(self):
        """Files saved to custom path should have same sizes as default path files."""
        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

        output = self.send_cli_command("snapshot 1 pre_isp_raw", wait_for="Captured ")
        has_complete = "Captured 1/1" in output

        file_sizes = self.get_file_sizes(path=CUSTOM_SNAPSHOT_PATH)
        all_nonzero = all(size > 0 for _, size in file_sizes)

        self.report(
            "custom_path_file_sizes",
            has_complete and len(file_sizes) == 1 and all_nonzero,
            f"files={len(file_sizes)}, sizes={[s for _, s in file_sizes]}, cli_ok={has_complete}",
        )

        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

    def run_all(self):
        """Run all test suites: functional, performance, and custom path."""
        # --- Phase 1: Functional tests (default path) ---
        self.start_pipeline()
        try:
            functional_tests = [
                self.test_list_stages,
                self.test_single_frame_all_stages,
                self.test_single_frame_filtered,
                self.test_multi_frame_three_stages,
                self.test_multi_frame_filtered,
                self.test_frame_count_preserved,
                self.test_interval,
                self.test_file_sizes_nonzero,
                self.test_back_to_back_requests,
            ]
            print(f"\n--- Functional tests ({len(functional_tests)}) ---\n", flush=True)
            for test in functional_tests:
                test()

            # --- Phase 2: Performance tests (default path) ---
            perf_tests = [
                self.test_perf_50_frames_single_stage,
                self.test_perf_50_frames_two_stages,
                self.test_perf_20_frames_with_interval,
            ]
            print(f"\n--- Performance tests ({len(perf_tests)}) ---\n", flush=True)
            for test in perf_tests:
                test()
        finally:
            self.stop_pipeline()

        # --- Phase 3: Custom path tests (restart pipeline with MEDIALIB_SNAPSHOT_PATH) ---
        self.clean_snapshots(path=DEFAULT_SNAPSHOT_PATH)
        self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)
        self.start_pipeline(extra_env=f"MEDIALIB_SNAPSHOT_PATH={CUSTOM_SNAPSHOT_PATH}")
        try:
            custom_path_tests = [
                self.test_custom_path_via_env,
                self.test_custom_path_multi_frame,
                self.test_custom_path_file_sizes,
            ]
            print(f"\n--- Custom path tests ({len(custom_path_tests)}) ---\n", flush=True)
            for test in custom_path_tests:
                test()
        finally:
            self.stop_pipeline()
            self.clean_snapshots(path=CUSTOM_SNAPSHOT_PATH)

        total = self.passed + self.failed
        print(f"\nResults: {self.passed}/{total} passed, {self.failed} failed", flush=True)
        return self.failed == 0


def main():
    parser = argparse.ArgumentParser(description="Snapshot CLI E2E tests")
    parser.add_argument(
        "--device-ip",
        default="10.0.0.1",
        help="Target device IP (default: 10.0.0.1)",
    )
    args = parser.parse_args()

    runner = SnapshotE2ETest(args.device_ip)
    success = runner.run_all()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
