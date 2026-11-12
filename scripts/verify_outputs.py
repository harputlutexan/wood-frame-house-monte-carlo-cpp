"""Validate generated Monte Carlo summaries against the published sample results.

Checks structural consistency and key engineering results with floating-point
allowance across standard-library implementations and target platforms.
"""
import csv
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTUAL = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results.csv"
REFERENCE = ROOT / "sample_results" / "results.csv"


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def check():
    rows = read_rows(ACTUAL)
    reference = read_rows(REFERENCE)
    assert len(rows) == len(reference) == 144, "Expected 72 baseline and 72 post-mitigation rows"
    pairs = {(row["stage"], int(row["combination"])): row for row in rows}
    assert len(pairs) == 144, "Duplicate or missing combination/stage"
    baseline = [pairs["baseline", i] for i in range(1, 73)]
    post = [pairs["post_mitigation", i] for i in range(1, 73)]
    assert sum(int(r["feasible"]) for r in baseline) == 53, "Unexpected feasible count"
    assert sum(int(r["feasible"]) for r in post) == 53, "Unexpected post-mitigation count"
    for x, y in zip(baseline, post):
        assert {k: v for k, v in x.items() if k != "stage"} == {k: v for k, v in y.items() if k != "stage"}, "Expected identical metrics for mitigation NONE"
    for row in rows:
        for key in ("energy_margin", "expected_cost", "p90_cost", "budget_overrun",
                    "schedule_overrun", "expected_duration", "expected_action_count"):
            assert math.isfinite(float(row[key])), f"Non-finite value: {key}"
        assert 0 <= float(row["budget_overrun"]) <= 1
        assert 0 <= float(row["schedule_overrun"]) <= 1
    feasible = [r for r in baseline if r["feasible"] == "1"]
    selected = min(feasible, key=lambda r: (float(r["expected_cost"]), float(r["p90_cost"]), int(r["combination"])))
    assert int(selected["combination"]) == 33, "Unexpected nominal design"
    assert abs(float(selected["expected_cost"]) - 1209984.33) < 50, "Unexpected nominal expected cost"
    assert abs(float(selected["p90_cost"]) - 1254602.99) < 50, "Unexpected P90"
    assert abs(float(selected["energy_margin"]) - 0.03) < 1e-9
    # Compare per-design reference costs to catch accidental changes in the model.
    samples = {(r["stage"], r["combination"]): r for r in reference}
    for row in rows:
        previous = samples[row["stage"], row["combination"]]
        assert row["feasible"] == previous["feasible"], "Changed feasibility result"
        assert abs(float(row["expected_cost"]) - float(previous["expected_cost"])) < 50, "Cost drift"
        assert abs(float(row["p90_cost"]) - float(previous["p90_cost"])) < 50, "P90 drift"
    print("PASS: 72 designs x 3,000 scenarios, 53 feasible, mitigation NONE, final design #33.")


if __name__ == "__main__":
    check()
