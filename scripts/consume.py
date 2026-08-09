import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "third_party"))

import pika
connection = pika.BlockingConnection(pika.ConnectionParameters('localhost'))
channel = connection.channel()

def callback(ch, method, properties, body):
    print(f"Received: {body.decode()}")

channel.basic_consume(queue='cdr', auto_ack=True, on_message_callback=callback)

try:
    channel.start_consuming()
except KeyboardInterrupt:
    print("\n\033[1;31mProgram Stopped\033[0m")
    channel.stop_consuming()
finally:
    if connection.is_open:
        connection.close()


