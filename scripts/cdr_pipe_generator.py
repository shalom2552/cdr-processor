import argparse
import atexit
import os
import random
import sys
import time
import tomllib
from datetime import datetime, timedelta

DIM, CYAN, GREEN, RESET = "\033[2m", "\033[36m", "\033[32m", "\033[0m"


def log(color, tag, msg):
    print(f"\r{DIM}{datetime.now():%H:%M:%S}{RESET} {color}{tag:<7}{RESET} {msg}", file=sys.stderr)

# --- config ---------------------------------------------------------------
ROOT = os.path.dirname(os.path.dirname(__file__))
SEQ_FILE = os.path.join(os.path.dirname(__file__), ".seq")
CONFIG_FILE = os.path.join(ROOT, "config.toml")

with open(CONFIG_FILE, "rb") as fh:
    CFG = tomllib.load(fh)

SOURCE = CFG.get("source", {})
MODE = SOURCE.get("mode", "")
FORMAT = SOURCE.get("format", "pipe")
if FORMAT != "pipe":
    raise SystemExit(f"this generator writes pipe records, config asks for '{FORMAT}'")
RECORDS_DIR = os.path.join(ROOT, SOURCE.get("file", {}).get("dir", "records"))
ROTATE_SECONDS = SOURCE.get("file", {}).get("rotate_seconds", 600)
RABBIT_URL = SOURCE.get("rabbit", {}).get("url", "amqp://guest:guest@localhost/")
RABBIT_QUEUE = SOURCE.get("rabbit", {}).get("queue", "cdr")
GEN_INTERVAL = CFG.get("generator", {}).get("gen_interval", 0.001)

DIGITS = "0123456789"
USAGE_TYPES = ["MOC", "MTC", "SMS-MO", "SMS-MT", "D", "U", "B", "X"]


# --- sequence -------------------------------------------------------------
seq = int(open(SEQ_FILE).read()) if os.path.exists(SEQ_FILE) else 0
atexit.register(lambda: open(SEQ_FILE, "w").write(str(seq)))


def next_seq():
    global seq
    seq += 1
    return seq


# --- generation -----------------------------------------------------------
def make_cdr(seq):
    rnd = lambda n: "".join(random.choices(DIGITS, k=n))        # n digits, may lead with 0
    num = lambda n: random.choice(DIGITS[1:]) + rnd(n - 1)      # n digits, no leading zero
    usage = random.choice(USAGE_TYPES)
    when = datetime.now() - timedelta(seconds=random.randint(0, 2592000))

    # only successful voice calls and data sessions have a duration
    duration = str(random.randint(1, 3600)) if usage in ("MOC", "MTC", "D") else "0"

    rx = tx = sp_imsi = sp_msisdn = ""
    if usage == "D":
        rx, tx = str(random.randint(0, 10_000_000)), str(random.randint(0, 10_000_000))
    else:
        sp_imsi = num(15)
        sp_msisdn = num(random.randint(11, 15))

    return "|".join([
        str(seq),
        num(15),                                    # subscriber IMSI
        f"{rnd(2)}-{rnd(6)}-{rnd(6)}-{rnd(1)}",     # IMEI
        usage,
        num(random.randint(11, 15)),                # subscriber MSISDN
        when.strftime("%d/%m/%Y"),
        when.strftime("%H:%M:%S"),
        duration,
        rx,
        tx,
        sp_imsi,
        sp_msisdn,
    ])


# --- sinks ----------------------------------------------------------------
def write_file(records):
    if not records:
        return
    os.makedirs(RECORDS_DIR, exist_ok=True)
    path = os.path.join(RECORDS_DIR, datetime.now().strftime("%Y%m%d_%H%M%S") + ".cdr")
    with open(path + ".tmp", "w") as fh:
        fh.write(f"CDR|{FORMAT}|{len(records)}\n")
        fh.write("\n".join(records) + "\n")
    os.replace(path + ".tmp", path)
    log(GREEN, "saved", f"{os.path.relpath(path, ROOT)}  {len(records)} records")


def run_files():
    log(CYAN, "start", f"file mode -> {os.path.relpath(RECORDS_DIR, ROOT)}/, rotate every {ROTATE_SECONDS}s")
    records = []
    start = time.time()
    try:
        while True:
            records.append(make_cdr(next_seq()))
            if time.time() - start >= ROTATE_SECONDS:
                write_file(records)
                records = []
                start = time.time()
            time.sleep(GEN_INTERVAL)
    except KeyboardInterrupt:
        pass
    finally:
        write_file(records)


def run_print():
    log(CYAN, "start", "print mode")
    while True:
        print(make_cdr(next_seq()))
        time.sleep(GEN_INTERVAL)


def run_rabbit():
    import pika

    log(CYAN, "start", f"rabbit mode -> {RABBIT_URL} queue={RABBIT_QUEUE}")
    conn = pika.BlockingConnection(pika.URLParameters(RABBIT_URL))
    channel = conn.channel()
    channel.queue_declare(queue=RABBIT_QUEUE, durable=True)
    log(GREEN, "ready", "connected, publishing")
    try:
        while True:
            channel.basic_publish("", RABBIT_QUEUE, make_cdr(next_seq()).encode())
            time.sleep(GEN_INTERVAL)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            if conn.is_open:
                conn.close()
        except Exception:
            pass


# --- main -----------------------------------------------------------------
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-p", "--print", action="store_true", help="print records to the screen")
    group.add_argument("-f", "--file", action="store_true", help="write records to rotating files")
    group.add_argument("-r", "--rabbit", action="store_true", help="stream records to RabbitMQ")
    args = parser.parse_args()

    # CLI flag wins, else config [source].mode, else print
    mode = "print" if args.print else "file" if args.file else "rabbit" if args.rabbit else MODE
    run = {"file": run_files, "rabbit": run_rabbit}.get(mode, run_print)

    log(CYAN, "config", f"gen_interval={GEN_INTERVAL}s  seq={seq}")

    first = seq
    try:
        run()
    except KeyboardInterrupt:
        pass
    finally:
        log(CYAN, "stop", f"{seq - first} records generated")
