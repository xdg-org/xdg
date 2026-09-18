#!/usr/bin/env python3
"""Run repeated XDG ray benchmarks for one backend on one node."""

import argparse
import csv
import os
import socket
import statistics
import subprocess
import sys
from pathlib import Path


# Defaults intended to be edited for a particular machine or overridden on the
# command line. Each repetition uses the same seed so backend timings are taken
# from identical ray workloads.
BENCHMARK = "./build/cubql_llvm_ada/tools/ray-benchmark"
MESH = "./atr.h5m"
VOLUME = 38
BACKEND = "EMBREE"
NUM_RAYS = 10_000_000
WARMUP_RAYS = 1_000_000
TRACE_REPETITIONS = 100
RUNS = 10
SEED = 12_345
SOURCE_RADIUS = 0.0
ORIGIN = None  # None selects the center of VOLUME; otherwise use (x, y, z).
MESH_LIBRARY = "MOAB"
OMP_NUM_THREADS = None
SYSTEM_LABEL = ""
OUTPUT = "ray_benchmark_results.csv"


BENCHMARK_COLUMNS = [
    "model",
    "mesh_library",
    "rt_library",
    "volume",
    "num_faces",
    "num_rays",
    "warmup_rays",
    "trace_repetitions",
    "total_ray_queries",
    "num_hits",
    "num_misses",
    "hit_fraction",
    "seed",
    "source_radius",
    "origin_x",
    "origin_y",
    "origin_z",
    "n_threads",
    "initialisation_time_s",
    "generation_time_s",
    "upload_time_s",
    "trace_time_s",
    "download_time_s",
    "generation_trace_time_s",
    "transfer_inclusive_time_s",
    "end_to_end_throughput_rays_per_s",
    "transfer_inclusive_throughput_rays_per_s",
    "trace_only_throughput_rays_per_s",
    "wall_time_s",
]

DRIVER_COLUMNS = [
    "run_index",
    "system_label",
    "hostname",
    *BENCHMARK_COLUMNS,
]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", default=BENCHMARK)
    parser.add_argument("--mesh", default=MESH)
    parser.add_argument("--volume", type=int, default=VOLUME)
    parser.add_argument(
        "-rt",
        "--rt-library",
        "--backend",
        dest="backend",
        default=BACKEND,
    )
    parser.add_argument("-n", "--num-rays", type=int, default=NUM_RAYS)
    parser.add_argument("--warmup-rays", type=int, default=WARMUP_RAYS)
    parser.add_argument(
        "--trace-repetitions", type=int, default=TRACE_REPETITIONS
    )
    parser.add_argument("-r", "--runs", type=int, default=RUNS)
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--source-radius", type=float, default=SOURCE_RADIUS)
    parser.add_argument("--origin", nargs=3, type=float, default=ORIGIN)
    parser.add_argument("--mesh-library", default=MESH_LIBRARY)
    parser.add_argument("--omp-threads", type=int, default=OMP_NUM_THREADS)
    parser.add_argument("--system-label", default=SYSTEM_LABEL)
    parser.add_argument("-o", "--output", default=OUTPUT)
    output_mode = parser.add_mutually_exclusive_group()
    output_mode.add_argument("--append", action="store_true")
    output_mode.add_argument("--overwrite", action="store_true")
    return parser.parse_args()


def validate_args(args):
    args.backend = args.backend.upper()
    if args.backend not in {"EMBREE", "GPRT", "CUBQL"}:
        raise SystemExit(f"Unsupported backend: {args.backend}")
    if args.runs < 1:
        raise SystemExit("--runs must be at least one.")
    if args.num_rays < 1:
        raise SystemExit("--num-rays must be at least one.")
    if args.warmup_rays < 0:
        raise SystemExit("--warmup-rays cannot be negative.")
    if args.trace_repetitions < 1:
        raise SystemExit("--trace-repetitions must be at least one.")
    if args.omp_threads is not None and args.omp_threads < 1:
        raise SystemExit("--omp-threads must be at least one.")

    benchmark = Path(args.benchmark)
    mesh = Path(args.mesh)
    if not benchmark.is_file():
        raise SystemExit(f"Benchmark executable not found: {benchmark}")
    if not os.access(benchmark, os.X_OK):
        raise SystemExit(f"Benchmark is not executable: {benchmark}")
    if not mesh.is_file():
        raise SystemExit(f"Mesh file not found: {mesh}")


def benchmark_command(args):
    command = [
        args.benchmark,
        args.mesh,
        str(args.volume),
        "--mesh-library",
        args.mesh_library,
        "--rt-library",
        args.backend,
        "--num-rays",
        str(args.num_rays),
        "--warmup-rays",
        str(args.warmup_rays),
        "--trace-repetitions",
        str(args.trace_repetitions),
        "--seed",
        str(args.seed),
        "--source-radius",
        str(args.source_radius),
        "--format",
        "csv",
    ]
    if args.origin is None:
        command.append("--volume-center")
    else:
        command.extend(["--origin", *map(str, args.origin)])
    return command


def parse_benchmark_csv(stdout):
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    for index, line in enumerate(lines):
        if line.startswith("model,") and index + 1 < len(lines):
            row = next(csv.DictReader([line, lines[index + 1]]))
            missing = [column for column in BENCHMARK_COLUMNS if column not in row]
            if missing:
                raise RuntimeError(
                    "ray-benchmark CSV is missing columns: " + ", ".join(missing)
                )
            return row
    raise RuntimeError("Could not find ray-benchmark CSV output.")


def run_benchmark(args, run_index, environment):
    command = benchmark_command(args)
    print(f"[{args.backend}] repetition {run_index}/{args.runs}", flush=True)
    try:
        result = subprocess.run(
            command,
            check=True,
            text=True,
            capture_output=True,
            env=environment,
        )
    except subprocess.CalledProcessError as error:
        print(error.stdout, file=sys.stderr)
        print(error.stderr, file=sys.stderr)
        raise SystemExit(
            f"{args.backend} repetition {run_index} failed with exit code "
            f"{error.returncode}."
        ) from error

    row = parse_benchmark_csv(result.stdout)
    row.update(
        run_index=str(run_index),
        system_label=args.system_label,
        hostname=socket.gethostname(),
    )
    print(
        "  trace: "
        f"{float(row['trace_time_s']):.6g} s, "
        f"{float(row['trace_only_throughput_rays_per_s']):.6e} rays/s",
        flush=True,
    )
    return row


def prepare_output(path, append, overwrite):
    output = Path(path)
    if output.exists() and not append and not overwrite:
        raise SystemExit(
            f"Output file already exists: {output}. "
            "Use --append or --overwrite."
        )

    if append and output.exists() and output.stat().st_size:
        with output.open(newline="") as stream:
            header = next(csv.reader(stream))
        if header != DRIVER_COLUMNS:
            raise SystemExit(f"Existing CSV schema does not match: {output}")
        return

    with output.open("w", newline="") as stream:
        csv.DictWriter(stream, fieldnames=DRIVER_COLUMNS).writeheader()


def append_row(path, row):
    with Path(path).open("a", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=DRIVER_COLUMNS, extrasaction="ignore"
        )
        writer.writerow(row)


def print_summary(rows, backend):
    times = [float(row["trace_time_s"]) for row in rows]
    rates = [float(row["trace_only_throughput_rays_per_s"]) for row in rows]
    cv = statistics.stdev(rates) / statistics.mean(rates) if len(rates) > 1 else 0.0

    print(f"\n{backend} trace-only summary over {len(rows)} repetitions")
    print(f"Median trace time : {statistics.median(times):.6g} s")
    print(f"Median throughput : {statistics.median(rates):.6e} rays/s")
    print(f"Throughput CV     : {cv:.2%}")


def main():
    args = parse_args()
    validate_args(args)
    prepare_output(args.output, args.append, args.overwrite)

    environment = os.environ.copy()
    if args.omp_threads is not None:
        environment["OMP_NUM_THREADS"] = str(args.omp_threads)

    rows = []
    for run_index in range(1, args.runs + 1):
        row = run_benchmark(args, run_index, environment)
        rows.append(row)
        append_row(args.output, row)

    print_summary(rows, args.backend)
    print(f"\nRaw results written to {args.output}")


if __name__ == "__main__":
    main()
