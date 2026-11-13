"""Run the MicroPython station logic on a PC without sensor hardware."""
import ast
from pathlib import Path
import unittest


class Clock:
    now = 0

    def ticks_ms(self):
        return self.now

    @staticmethod
    def ticks_diff(a, b):
        return a - b


class Pin:
    def __init__(self):
        self.level = 0

    def value(self, level):
        self.level = level


class Sensor:
    def __init__(self, bus):
        self.pending = None

    def is_data_ready(self):
        return self.pending is not None

    def read_distance_mm(self):
        result = self.pending
        self.pending = None
        return result


class ClassificationTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        source = Path(__file__).resolve().parents[1] / "main_micropython.py"
        tree = ast.parse(source.read_text(encoding="utf-8"))
        names = {"BELT_CLEAR_MIN_MM", "DEBOUNCE_COUNT", "CLEAR_COUNT", "station1", "station2"}
        # Execute the real class and station settings, skipping hardware setup/main loop.
        nodes = [node for node in tree.body if
                 isinstance(node, ast.ClassDef) and node.name == "Station" or
                 isinstance(node, ast.Assign) and any(
                     isinstance(target, ast.Name) and target.id in names
                     for target in node.targets)]
        namespace = dict(time=self.clock, VL53L0X=Sensor, i2c0=None, i2c1=None,
                         sol1_pin=Pin(), sol2_pin=Pin(), print=lambda *args: None)
        exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), namespace)
        self.stations = [namespace["station1"], namespace["station2"]]

    def sample(self, station, distance, count=1):
        for _ in range(count):
            self.clock.now += 40
            station.sensor.pending = distance
            station.update()

    def test_bare_belt_and_small_parts_pass(self):
        for station in self.stations:
            for distance in (80, 82, 85, 87, 90):
                self.sample(station, distance, 10)
            self.assertEqual(station.total_sorted, 0)
            self.assertEqual(station.sol_pin.level, 0)

    def test_only_own_size_and_inclusive_boundaries_trigger(self):
        for index, matching, other in ((0, (65, 70, 75), (50, 55, 60, 64, 76)),
                                       (1, (50, 55, 60), (49, 61, 65, 70, 75))):
            for distance in other + matching:
                self.setUp()
                station = self.stations[index]
                self.sample(station, distance, 2)
                expected = int(distance in matching)
                self.assertEqual(station.total_sorted, expected, (index, distance))
                self.assertEqual(station.sol_pin.level, expected, (index, distance))

    def test_noise_requires_two_consecutive_matching_samples(self):
        for station, distance in zip(self.stations, (70, 55)):
            for value in (distance, 90, distance, 0, distance, 8190, distance):
                self.sample(station, value)
            self.assertEqual(station.sol_pin.level, 0)
            self.sample(station, distance)
            self.assertEqual(station.total_sorted, 1)
            self.assertEqual(station.sol_pin.level, 1)

    def test_both_release_at_500_ms_and_do_not_repeat_on_same_part(self):
        for station, distance in zip(self.stations, (70, 55)):
            self.sample(station, distance, 2)
            self.clock.now = station.state_start_ms + 499
            station.update()
            self.assertEqual(station.sol_pin.level, 1)
            self.clock.now += 1
            station.update()
            self.assertEqual(station.sol_pin.level, 0)
            self.sample(station, distance, 30)
            self.assertEqual(station.total_sorted, 1)
            # Neither an invalid read nor a single belt read rearms the station.
            for value in (0, 8190, 90, distance, distance):
                self.sample(station, value)
            self.assertEqual(station.total_sorted, 1)
            self.sample(station, 85, 2)
            self.sample(station, distance, 2)
            self.assertEqual(station.total_sorted, 2)

    def test_stations_can_fire_together(self):
        for _ in range(2):
            self.clock.now += 40
            for station, distance in zip(self.stations, (70, 55)):
                station.sensor.pending = distance
                station.update()
        self.assertEqual([station.sol_pin.level for station in self.stations], [1, 1])
        self.clock.now += 500
        for station in self.stations:
            station.update()
        self.assertEqual([station.sol_pin.level for station in self.stations], [0, 0])


if __name__ == "__main__":
    unittest.main()
