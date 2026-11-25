"""Loopback-only MQTT broker for simulator development and automated tests."""
import argparse
import asyncio

from amqtt.broker import Broker


def make_broker(port, bind="127.0.0.1"):
    return Broker({
        "listeners": {"default": {"type": "tcp", "bind": f"{bind}:{port}"}},
        "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}},
    })


async def main(port, bind="127.0.0.1"):
    broker = make_broker(port, bind=bind)
    await broker.start()
    try:
        await asyncio.Event().wait()
    finally:
        await broker.shutdown()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--bind", default="127.0.0.1")
    args = parser.parse_args()
    asyncio.run(main(args.port, bind=args.bind))

