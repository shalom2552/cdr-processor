"""Works out why no broker answered, by asking docker what is actually on the machine, so
a failed run ends with the command that fixes it instead of a connection traceback."""

from __future__ import annotations

import shutil
import subprocess

DOCKER_RUN = "docker run -d --name rabbitmq -p 5672:5672 -p 15672:15672 rabbitmq:3-management"
_PS_FORMAT = "{{.Names}}\t{{.Image}}\t{{.State}}"


def _containers() -> list[tuple[str, str, str]] | None:
    """Every container as (name, image, state), or None when docker cannot answer."""
    try:
        ps = subprocess.run(["docker", "ps", "-a", "--format", _PS_FORMAT],
                            capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    if ps.returncode != 0:
        return None
    rows = []
    for line in ps.stdout.splitlines():
        name, image, state = (line.split("\t") + ["", ""])[:3]
        rows.append((name, image, state))
    return rows


def _docker_trouble() -> list[str] | None:
    """Hints for when the docker command itself is the problem, None when it works."""
    if not shutil.which("docker"):
        return ["install docker, then: " + DOCKER_RUN,
                "or install a local broker and start it: sudo systemctl start rabbitmq"]
    try:
        ps = subprocess.run(["docker", "info"], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return [DOCKER_RUN]
    if ps.returncode == 0:
        return None
    if "permission denied" in ps.stderr.lower():
        return ["sudo usermod -aG docker $USER   (then log back in)", "or: sudo " + DOCKER_RUN]
    return ["sudo systemctl start docker", "then: " + DOCKER_RUN]


def broker_hints(url: str) -> list[str]:
    trouble = _docker_trouble()
    if trouble is not None:
        return trouble

    for name, image, state in _containers() or []:
        if "rabbitmq" not in f"{name} {image}".lower():
            continue
        if state == "running":                  # up, so the url or the ports are wrong
            return [f"container '{name}' is running, check the url in config.toml: {url}",
                    f"docker port {name}"]
        return [f"docker start {name}"]

    return [DOCKER_RUN]
