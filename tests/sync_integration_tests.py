import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time


def main():
    probe, worker, server_path = sys.argv[1:]
    specification = importlib.util.spec_from_file_location("pinyin_sync_server", server_path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)

    class Handler(module.SyncHandler):
        def do_GET(self):
            if self.path == "/redirect.tsv":
                self.send_response(302)
                self.send_header("Location", "/dictionary.tsv")
                self.send_header("Content-Length", "0")
                self.end_headers()
            else:
                super().do_GET()

    with tempfile.TemporaryDirectory(prefix="pinyin-http-sync-") as workspace:
        root = Path(workspace)
        server = module.SyncServer(("127.0.0.1", 0), root / "cloud", "test-token")
        server.RequestHandlerClass = Handler
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        background = None
        try:
            url = f"http://127.0.0.1:{server.server_port}"
            subprocess.run([probe, url, str(root)], check=True, timeout=60)
            user = root / "device-a.tsv"
            subprocess.run([worker, "--once", "--user", str(user)], check=True, timeout=30)
            status = Path(str(user) + ".sync.status")
            if "同步成功" not in status.read_text(encoding="utf-8"):
                raise AssertionError("one-shot worker did not persist a successful status")
            last_success = status.read_text(encoding="utf-8").splitlines()[1]
            original = user.read_bytes()
            server.token = "different-token"
            failed = subprocess.run([worker, "--once", "--user", str(user)], timeout=30)
            failure_status = status.read_text(encoding="utf-8").splitlines()
            if failed.returncode != 1 or "401" not in failure_status[2] or failure_status[1] != last_success or user.read_bytes() != original:
                raise AssertionError("failed worker lost local words or the last successful timestamp")
            server.token = "test-token"
            before = server.requests
            background = subprocess.Popen([worker, "--background", "--user", str(user)])
            deadline = time.monotonic() + 10
            while server.requests == before and time.monotonic() < deadline:
                time.sleep(0.05)
            if server.requests == before:
                raise AssertionError("background worker did not sync immediately")
            duplicate = subprocess.run([worker, "--background", "--user", str(user)], timeout=10)
            if duplicate.returncode != 1:
                raise AssertionError("a second background worker was permitted")
            time.sleep(0.5)
            settled = server.requests
            time.sleep(2.2)
            if server.requests != settled:
                raise AssertionError("worker ignored its ten-minute idle interval")
            settings = Path(str(user) + ".sync.settings")
            settings.write_text(settings.read_text(encoding="utf-8").replace("\n1\n", "\n0\n", 1), encoding="utf-8", newline="\n")
            background.wait(timeout=10)
            if background.returncode != 0:
                raise AssertionError("disabled background worker did not exit cleanly")
            print("PASS: one-shot status, single worker, immediate sync, idle scheduling and disable")
        finally:
            if background is not None and background.poll() is None:
                background.terminate()
                background.wait(timeout=10)
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    main()
