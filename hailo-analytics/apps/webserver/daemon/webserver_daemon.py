import subprocess
import threading
import time
import logging
import json
import sys
import signal
import atexit
import os
from typing import List, Tuple, Dict, Optional


class Config:
    def __init__(
        self,
        port: int = 8001,
        keepalive_timeout: float = 10.0,
        check_interval: float = 2.0,
        grace_period: float = 5.0,
        child_cmd: List[str] = ['camera-viewer-server'],
    ):
        self.port = port
        self.keepalive_timeout = keepalive_timeout
        self.check_interval = check_interval
        self.grace_period = grace_period
        self.child_cmd = child_cmd


class State:
    def __init__(self):
        self._lock = threading.Lock()
        self.proc: Optional[subprocess.Popen] = None
        self.external_pid: Optional[int] = None
        self.using_external: bool = False
        self.last_ping: float = 0.0

    def update_last_ping(self):
        with self._lock:
            self.last_ping = time.time()

    def get(self) -> Tuple[Optional[subprocess.Popen], Optional[int], bool, float]:
        with self._lock:
            return self.proc, self.external_pid, self.using_external, self.last_ping

    def set_internal(self, proc: subprocess.Popen):
        with self._lock:
            self.proc = proc
            self.external_pid = None
            self.using_external = False
            self.last_ping = time.time()

    def set_external(self, pid: int):
        with self._lock:
            self.proc = None
            self.external_pid = pid
            self.using_external = True
            self.last_ping = time.time()

    def clear(self):
        with self._lock:
            self.proc = None
            self.external_pid = None
            self.using_external = False


class ChildProcessManager:
    def __init__(self, config: Config, state: State):
        self.config = config
        self.state = state

    def is_alive(self, pid: int) -> bool:
        try:
            os.kill(pid, 0)
            return True
        except Exception:
            return False

    def find_external_instances(self) -> List[int]:
        try:
            out = subprocess.check_output([
                'pidof', self.config.child_cmd[0]
            ], text=True).strip()
            return [int(p) for p in out.split()] if out else []
        except Exception:
            return []

    def forward_output(self, pipe, tag: str):
        for line in pipe:
            logging.info(f'[{tag}] {line.rstrip()}')
        pipe.close()

    def launch(self) -> None:
        # reject if already running
        proc, ext, using_ext, _ = self.state.get()
        if proc and proc.poll() is None:
            raise RuntimeError("Already running under daemon")
        if using_ext and ext and self.is_alive(ext):
            raise RuntimeError("Already running under daemon (external)")

        # reuse external if present
        ext_list = self.find_external_instances()
        if ext_list:
            pid = ext_list[0]
            logging.warning(f"Found existing process with PID {pid}; reusing it")
            self.state.set_external(pid)
            return

        # spawn new
        logging.info("Launching child...")
        p = subprocess.Popen(
            self.config.child_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        threading.Thread(
            target=self.forward_output, args=(p.stdout, 'STDOUT'), daemon=True
        ).start()
        threading.Thread(
            target=self.forward_output, args=(p.stderr, 'STDERR'), daemon=True
        ).start()
        self.state.set_internal(p)

    def stop(self) -> bool:
        proc, ext, using_ext, _ = self.state.get()
        if using_ext and ext:
            return self._terminate_pid(ext)
        if proc and proc.poll() is None:
            return self._terminate_proc(proc)
        return False

    def _terminate_pid(self, pid: int) -> bool:
        logging.info(f"Stopping external process PID {pid}...")
        try:
            os.kill(pid, signal.SIGTERM)
        except Exception:
            pass
        start = time.time()
        while time.time() - start < self.config.grace_period and self.is_alive(pid):
            time.sleep(1)
        if self.is_alive(pid):
            logging.warning(f"External did not exit; SIGKILL PID {pid}")
            try:
                os.kill(pid, signal.SIGKILL)
            except Exception:
                pass
        self.state.clear()
        return True

    def _terminate_proc(self, proc: subprocess.Popen) -> bool:
        logging.info("Stopping child...")
        proc.terminate()
        start = time.time()
        while time.time() - start < self.config.grace_period:
            if proc.poll() is not None:
                break
            time.sleep(1)
        if proc.poll() is None:
            logging.warning("Child did not exit; SIGKILL")
            proc.kill()
        self.state.clear()
        return True


class Monitor(threading.Thread):
    def __init__(self, config: Config, state: State, manager: ChildProcessManager):
        super().__init__(daemon=True)
        self.config = config
        self.state = state
        self.manager = manager

    def run(self):
        while True:
            time.sleep(self.config.check_interval)
            proc, ext, using_ext, last = self.state.get()
            if using_ext and ext and self.manager.is_alive(ext):
                if (time.time() - last) > self.config.keepalive_timeout:
                    logging.info(f"Heartbeat lost; killing external PID {ext}")
                    self.manager.stop()
            elif proc and proc.poll() is None:
                if (time.time() - last) > self.config.keepalive_timeout:
                    logging.info("Heartbeat lost; killing internal child")
                    self.manager.stop()


class RequestDispatcher:
    def __init__(self, config: Config, state: State, manager: ChildProcessManager):
        self.config = config
        self.state = state
        self.manager = manager

    def dispatch(self, method: str, path: str) -> Tuple[str, Dict[str, str], str]:
        if method == 'POST' and path == '/launch':
            try:
                self.manager.launch()
                return 'HTTP/1.1 204 No Content', {}, ''
            except RuntimeError as e:
                return 'HTTP/1.1 409 Conflict', {}, str(e)
            except Exception as e:
                return 'HTTP/1.1 500 Internal Server Error', {}, str(e)

        if method == 'POST' and path == '/ping':
            proc, ext, using_ext, _ = self.state.get()
            alive = (using_ext and ext and self.manager.is_alive(ext)) or (proc and proc.poll() is None)
            if not alive:
                return 'HTTP/1.1 409 Not running', {}, ''
            self.state.update_last_ping()
            return 'HTTP/1.1 204 No Content', {}, ''

        if method == 'POST' and path == '/stop':
            if self.manager.stop():
                return 'HTTP/1.1 204 No Content', {}, ''
            return 'HTTP/1.1 409 Not running', {}, ''

        if method == 'GET' and path == '/status':
            proc, ext, using_ext, last = self.state.get()
            if using_ext and ext and self.manager.is_alive(ext):
                running, pid = True, ext
            elif proc and proc.poll() is None:
                running, pid = True, proc.pid
            else:
                running, pid = False, None
            last_str = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(last)) if last else None
            body = json.dumps({'running': running, 'pid': pid, 'last_ping': last_str})
            headers = {'Content-Type': 'application/json', 'Content-Length': str(len(body))}
            return 'HTTP/1.1 200 OK', headers, body

        return 'HTTP/1.1 404 Not Found', {}, ''


class NcServer:
    def __init__(self, config: Config, dispatcher: RequestDispatcher):
        self.config = config
        self.dispatcher = dispatcher

    def serve_forever(self):
        logging.info(f"Listening on port {self.config.port} via BusyBox nc")
        while True:
            p = subprocess.Popen([
                'nc', '-l', '-p', str(self.config.port)
            ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            request = p.stdout.readline().strip()
            if not request:
                p.stdin.close()
                p.wait()
                continue
            method, path, _ = request.split(' ', 2)
            # skip headers
            while True:
                h = p.stdout.readline()
                if not h or h in ('\r\n', '\n'):
                    break

            # Prepare CORS headers
            cors_headers = {
                'Access-Control-Allow-Origin': '*',
                'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
                'Access-Control-Allow-Headers': 'Content-Type',
            }

            # Handle preflight
            if method == 'OPTIONS':
                status = 'HTTP/1.1 204 No Content'
                headers = {**cors_headers, 'Content-Length': '0'}
                body = ''
            else:
                status, headers, body = self.dispatcher.dispatch(method, path)
                # Merge CORS headers into all responses
                headers = {**cors_headers, **headers}

            # Build and send response
            resp = status + '\r'
            for k, v in headers.items():
                resp += f"{k}: {v}\r"
            resp += '\r' + body
            p.stdin.write(resp)
            p.stdin.close()
            p.wait()


def main():
    logging.basicConfig(level=logging.INFO, format='[%(asctime)s] %(message)s')
    config = Config()
    state = State()
    manager = ChildProcessManager(config, state)
    monitor = Monitor(config, state, manager)
    dispatcher = RequestDispatcher(config, state, manager)
    server = NcServer(config, dispatcher)

    def cleanup(signum=None, frame=None):
        logging.info("Daemon exiting; stopping child if needed.")
        manager.stop()
        sys.exit(0)

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, cleanup)
    atexit.register(manager.stop)

    monitor.start()
    server.serve_forever()


if __name__ == '__main__':
    main()
