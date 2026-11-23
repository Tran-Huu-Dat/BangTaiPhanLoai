"""Loopback-only MQTT broker for simulator development and automated tests."""
import argparse
import asyncio

from amqtt.broker import Broker


def make_broker(port):
    return Broker({
        "listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}},
        "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}},
    })


async def main(port):
    broker = make_broker(port)
    await broker.start()
    try:
        await asyncio.Event().wait()
    finally:
        await broker.shutdown()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=1883)
    asyncio.run(main(parser.parse_args().port))

