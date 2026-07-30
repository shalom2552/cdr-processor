import argparse
import atexit
import os
import random
import time
from datetime import datetime, timedelta

# --- config ---------------------------------------------------------------
ROOT = os.path.dirname(os.path.dirname(__file__))
SEQ_FILE = os.path.join(os.path.dirname(__file__), ".seq")
RECORDS_DIR = os.path.join(ROOT, "records")

DIGITS = "0123456789"
USAGE_TYPES = ["MOC", "MTC", "SMS-MO", "SMS-MT", "D", "U", "B", "X"]

ROTATE_SECONDS = 600         # new file every 10 minutes
GEN_INTERVAL = 0.001             # seconds between records

RABBIT_URL = "amqp://guest:guest@localhost/"
RABBIT_QUEUE = "cdr"


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
    with open(path, "w") as fh:
        fh.write(str(len(records)) + "\n")
        fh.write("\n".join(records) + "\n")
    print(path)


def run_files():
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
    finally:
        write_file(records)


def run_print():
    while True:
        print(make_cdr(next_seq()))
        time.sleep(GEN_INTERVAL)


def run_rabbit():
    import pika

    conn = pika.BlockingConnection(pika.URLParameters(RABBIT_URL))
    channel = conn.channel()
    channel.queue_declare(queue=RABBIT_QUEUE, durable=True)
    try:
        while True:
            channel.basic_publish("", RABBIT_QUEUE, make_cdr(next_seq()).encode())
            time.sleep(GEN_INTERVAL)
    finally:
        conn.close()


# --- main -----------------------------------------------------------------
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-p", "--print", action="store_true", help="print records to the screen (default)")
    group.add_argument("-f", "--file", action="store_true", help="write records to rotating files")
    group.add_argument("-r", "--rabbit", action="store_true", help="stream records to RabbitMQ")
    args = parser.parse_args()

    try:
        if args.file:
            run_files()
        elif args.rabbit:
            run_rabbit()
        else:
            run_print()
    except KeyboardInterrupt:
        pass
