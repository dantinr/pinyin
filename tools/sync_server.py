import argparse
import hashlib
import hmac
import os
from pathlib import Path
import re
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


MAXIMUM_BYTES = 32 * 1024 * 1024
HEADER = b"# private-pinyin sync v1: date<TAB>text<TAB>pinyin<TAB>selection_count"


class SyncServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, directory, token=""):
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.token = token
        self.lock = threading.Lock()
        self.requests = 0
        super().__init__(address, SyncHandler)


class SyncHandler(BaseHTTPRequestHandler):
    server_version = "PrivatePinyinSync/1"

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, format_string, *arguments):
        pass

    def respond(self, code, body=b"", etag=None):
        self.send_response(code)
        self.send_header("Content-Type", "text/tab-separated-values; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if etag:
            self.send_header("ETag", etag)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def target(self):
        with self.server.lock:
            self.server.requests += 1
        if self.server.token and not hmac.compare_digest(
            self.headers.get("Authorization", ""), "Bearer " + self.server.token
        ):
            self.respond(401)
            return None
        if not re.fullmatch(r"/[A-Za-z0-9_-]+\.tsv", self.path):
            self.respond(404)
            return None
        return self.server.directory / self.path[1:]

    @staticmethod
    def etag(body):
        return '"' + hashlib.sha256(body).hexdigest() + '"'

    def do_GET(self):
        target = self.target()
        if target is None:
            return
        with self.server.lock:
            if not target.exists():
                self.respond(404)
                return
            if target.stat().st_size > MAXIMUM_BYTES:
                self.respond(413)
                return
            body = target.read_bytes()
        self.respond(200, body, self.etag(body))

    def do_PUT(self):
        target = self.target()
        if target is None:
            return
        try:
            size = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            self.respond(400)
            return
        if size < 0 or size > MAXIMUM_BYTES:
            self.respond(413)
            return
        body = self.rfile.read(size)
        try:
            valid = len(body) == size and body.decode("utf-8-sig").splitlines()[0] == HEADER.decode()
        except (UnicodeError, IndexError):
            valid = False
        if not valid:
            self.respond(400)
            return
        with self.server.lock:
            exists = target.exists()
            current = self.etag(target.read_bytes()) if exists else None
            match = self.headers.get("If-Match")
            create = self.headers.get("If-None-Match")
            if not match and create != "*":
                self.respond(428)
                return
            if (create == "*" and exists) or (match and match != current):
                self.respond(412)
                return
            descriptor, temporary = tempfile.mkstemp(dir=self.server.directory, prefix=".sync-")
            try:
                with os.fdopen(descriptor, "wb") as output:
                    output.write(body)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(temporary, target)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        self.respond(204 if exists else 201, etag=self.etag(body))


def main():
    parser = argparse.ArgumentParser(description="Private Pinyin dictionary sync server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--directory", type=Path, default=Path("sync-data"))
    parser.add_argument("--token-env", default="PRIVATE_PINYIN_SYNC_TOKEN")
    arguments = parser.parse_args()
    token = os.environ.get(arguments.token_env, "")
    if arguments.host not in ("127.0.0.1", "localhost") and not token:
        parser.error("set the token environment variable before listening on a non-loopback address")
    server = SyncServer((arguments.host, arguments.port), arguments.directory, token)
    print(f"Sync server listening on {arguments.host}:{server.server_port}; press Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
