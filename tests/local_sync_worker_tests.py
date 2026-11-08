"""Exercise the actual offline worker with an isolated dictionary and no startup edits."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time


def main():
    worker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="private-pinyin-local-worker-") as directory:
        root = Path(directory)
        user = root / "words.user.tsv"
        enabled = Path(str(user) + ".local-sync.enabled")
        user.write_text("定时同步词\tding shi tong bu ci\t5\n", encoding="utf-8")
        original = user.read_bytes()
        environment = dict(os.environ, USERPROFILE=str(root / "profile"), LOCALAPPDATA=str(root / "appdata"))
        subprocess.run([worker, "--local-once", "--user", user], env=environment, check=True, timeout=20)
        assert user.read_bytes() == original, "disabled one-shot changed personal words"
        enabled.write_text("v1\n", encoding="ascii", newline="\n")
        subprocess.run([worker, "--local-once", "--user", user], env=environment, check=True, timeout=20)
        assert "定时同步词\tding shi tong bu ci\t12000" in (root / "main.tsv").read_text(encoding="utf-8")
        assert "定时同步词" not in user.read_text(encoding="utf-8"), "one-shot failed to clear personal queue"
        assert any(path.read_bytes() == original for path in (root / "backups").glob("personal-before-main-sync.*.tsv"))
        user.write_text("后续新词\thou xu xin ci\t1\n", encoding="utf-8")
        pending = user.read_bytes()
        subprocess.run([worker, "--local-once", "--user", user], env=environment, check=True, timeout=20)
        assert user.read_bytes() == pending, "one-shot ignored the ten-minute interval"
        process = subprocess.Popen([worker, "--local-background", "--user", user], env=environment)
        try:
            time.sleep(0.8)
            assert process.poll() is None, "local background worker exited early"
            duplicate = subprocess.run([worker, "--local-background", "--user", user], env=environment, timeout=10)
            assert duplicate.returncode == 1, "second local worker was allowed"
            assert user.read_bytes() == pending, "background worker ran before ten minutes"
            enabled.unlink()
            process.wait(timeout=10)
            assert process.returncode == 0, "disabled local worker did not exit"
            assert not Path(str(user) + ".sync.settings").exists(), "offline mode created cloud settings"
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    print("PASS: offline one-shot, backup, clearing, interval, singleton and background disable")


if __name__ == "__main__":
    main()
