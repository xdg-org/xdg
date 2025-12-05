#!/usr/bin/env python3
import subprocess
import statistics
import sys
import csv
import os

# --- CONFIG ---

BENCHMARK = "./tools/ray-benchmark"
MESH_PATH = "../dagmc_xdg_test.h5m"
VOLUME_ID = "2"
NUM_RAYS = "80000000"
ORIGIN = ["-o", "180", "250", "-27"]  # x y z as strings

# --- PARSING HELPERS ---

def parse_float_before_s(s: str) -> float:
    """
    Given a string like 'XDG initalisation Time = 1.25017s',
    pull out 1.25017 as float.
    """
    try:
        after_eq = s.split('=', 1)[1]
        number_str = after_eq.split('s', 1)[0].strip()
        return float(number_str)
    except Exception as e:
        raise ValueError(f"Failed to parse float from line: {s!r}") from e

def parse_throughput_line(s: str) -> float:
    """
    Given a string like 'Trace-only throughput        = 2.64065e+09 rays/s',
    pull out 2.64065e+09 as float.
    """
    try:
        after_eq = s.split('=', 1)[1]
        number_str = after_eq.split('rays', 1)[0].strip()
        return float(number_str)
    except Exception as e:
        raise ValueError(f"Failed to parse throughput from line: {s!r}") from e

def parse_benchmark_output(output: str):
    """
    Parse the benchmark stdout text and return a dict of metrics.
    Expected keys:
      - xdg_init
      - gen
      - gen_trace
      - end_to_end
      - wall_clock
      - trace_only
      - trace_only_throughput
    """
    metrics = {}

    for line in output.splitlines():
        line = line.strip()

        if line.startswith("XDG initalisation Time"):
            metrics["xdg_init"] = parse_float_before_s(line)

        elif line.startswith("Random ray generation"):
            metrics["gen"] = parse_float_before_s(line)

        elif line.startswith("Generation + tracing time"):
            metrics["gen_trace"] = parse_float_before_s(line)

        elif line.startswith("End-to-end throughput"):
            metrics["end_to_end"] = parse_throughput_line(line)

        elif line.startswith("Full wall-clock time"):
            metrics["wall_clock"] = parse_float_before_s(line)

        elif line.startswith("Ray Tracing Time (trace-only)"):
            metrics["trace_only"] = parse_float_before_s(line)

        elif line.startswith("Trace-only throughput"):
            metrics["trace_only_throughput"] = parse_throughput_line(line)

    required = [
        "xdg_init", "gen", "gen_trace", "end_to_end",
        "wall_clock", "trace_only", "trace_only_throughput"
    ]
    missing = [k for k in required if k not in metrics]
    if missing:
        raise RuntimeError(f"Missing metrics in output: {missing}")

    return metrics

# --- MAIN DRIVER ---

def main():
    # Ask for backend
    backend_in = input("Choose backend (embree/gprt): ").strip().lower()
    if backend_in not in ("embree", "gprt"):
        print("Invalid backend, please choose 'embree' or 'gprt'.")
        sys.exit(1)

    base_backend = backend_in.upper()  # what we pass to -r: EMBREE or GPRT

    # If GPRT, ask for which variant
    if backend_in == "gprt":
        mode_in = input(
            "GPRT mode: [1] GPRT (FP64), [2] GPRT (FP32) + RT cores [1]: "
        ).strip()
        if mode_in == "2":
            variant = "fp32_rt"
            label = "GPRT (FP32) + RT cores"
        else:
            variant = "fp64"
            label = "GPRT (FP64)"
    else:
        # Embree is effectively FP64 for your purposes
        variant = "fp64"
        label = "Embree"

    runs_str = input("How many runs? ").strip()
    try:
        num_runs = int(runs_str)
        if num_runs <= 0:
            raise ValueError
    except ValueError:
        print("Number of runs must be a positive integer.")
        sys.exit(1)

    # Ask for CSV filename
    csv_filename = input("CSV output file [benchmarks.csv]: ").strip()
    if not csv_filename:
        csv_filename = "benchmarks.csv"

    mesh_name = os.path.basename(MESH_PATH)

    all_metrics = {
        "xdg_init": [],
        "gen": [],
        "gen_trace": [],
        "end_to_end": [],
        "wall_clock": [],
        "trace_only": [],
        "trace_only_throughput": [],
    }

    # CSV header: machine-friendly backend/variant, plus pretty label
    header = [
        "backend",              # EMBREE / GPRT
        "variant",              # fp64 / fp32_rt
        "label",                # Embree / GPRT (FP64) / GPRT (FP32) + RT cores
        "mesh_name",
        "volume_id",
        "num_rays",
        "run_index",
        "xdg_init",
        "gen",
        "gen_trace",
        "end_to_end",
        "wall_clock",
        "trace_only",
        "trace_only_throughput",
    ]

    # Decide whether to append or overwrite
    file_exists = os.path.exists(csv_filename)
    write_header = False
    file_mode = "w"
    append_mode = False

    if file_exists:
        choice = input(
            f"File '{csv_filename}' already exists. "
            "[o]verwrite, [a]ppend, or e[x]it? [a]: "
        ).strip().lower()

        if choice in ("x", "q"):
            print("Aborting, no benchmarks run.")
            sys.exit(0)
        elif choice in ("", "a"):
            file_mode = "a"
            write_header = False  # assume header already there
            append_mode = True
        elif choice == "o":
            file_mode = "w"
            write_header = True
            append_mode = False
        else:
            print("Unrecognized choice, aborting.")
            sys.exit(1)
    else:
        # new file: write header
        file_mode = "w"
        write_header = True
        append_mode = False

    csv_file = open(csv_filename, file_mode, newline="")

    # If appending, add a separation comment line so it's obvious this is a new batch
    if append_mode:
        csv_file.write(
            f"\n# --- New benchmark batch: "
            f"label={label}, backend={base_backend}, variant={variant}, "
            f"mesh={mesh_name}, volume={VOLUME_ID}, "
            f"rays={NUM_RAYS}, runs={num_runs} ---\n"
        )

    writer = csv.writer(csv_file)

    if write_header:
        writer.writerow(header)

    try:
        for i in range(1, num_runs + 1):
            print(f"\n=== Run {i}/{num_runs} ({label}) ===")

            cmd = [
                BENCHMARK,
                MESH_PATH,
                VOLUME_ID,
                "-r", base_backend,   # EMBREE or GPRT
                "-n", NUM_RAYS,
                *ORIGIN,
            ]

            print("Running:", " ".join(cmd))

            try:
                result = subprocess.run(
                    cmd,
                    check=True,
                    text=True,
                    capture_output=True,
                )
            except subprocess.CalledProcessError as e:
                print("Benchmark command failed!")
                print("STDOUT:\n", e.stdout)
                print("STDERR:\n", e.stderr)
                sys.exit(1)

            try:
                metrics = parse_benchmark_output(result.stdout)
            except Exception as e:
                print("Failed to parse benchmark output:", e)
                print("Raw output:\n", result.stdout)
                sys.exit(1)

            # store for averages
            for k in all_metrics.keys():
                all_metrics[k].append(metrics[k])

            # write CSV row
            writer.writerow([
                base_backend,          # backend
                variant,               # variant
                label,                 # label
                mesh_name,
                VOLUME_ID,
                NUM_RAYS,
                i,                     # run_index
                metrics["xdg_init"],
                metrics["gen"],
                metrics["gen_trace"],
                metrics["end_to_end"],
                metrics["wall_clock"],
                metrics["trace_only"],
                metrics["trace_only_throughput"],
            ])

            # per-run summary
            print(f"XDG init           : {metrics['xdg_init']:.6f} s")
            print(f"Generation         : {metrics['gen']:.6f} s")
            print(f"Gen + trace        : {metrics['gen_trace']:.6f} s")
            print(f"End-to-end         : {metrics['end_to_end']:.3e} rays/s")
            print(f"Wall-clock         : {metrics['wall_clock']:.6f} s")
            print(f"Trace-only         : {metrics['trace_only']:.6f} s")
            print(f"Trace-only thrpt   : {metrics['trace_only_throughput']:.3e} rays/s")

    finally:
        csv_file.close()

    # Averages
    print(
        "\n=== Averages over",
        num_runs,
        f"runs (label: {label}) ==="
    )

    def avg(key): return statistics.mean(all_metrics[key])

    print(f"Avg XDG init           : {avg('xdg_init'):.6f} s")
    print(f"Avg Generation         : {avg('gen'):.6f} s")
    print(f"Avg Gen + trace        : {avg('gen_trace'):.6f} s")
    print(f"Avg End-to-end         : {avg('end_to_end'):.3e} rays/s")
    print(f"Avg Wall-clock         : {avg('wall_clock'):.6f} s")
    print(f"Avg Trace-only         : {avg('trace_only'):.6f} s")
    print(f"Avg Trace-only thrpt   : {avg('trace_only_throughput'):.3e} rays/s")
    print(f"\nResults written to: {csv_filename}")

if __name__ == "__main__":
    main()
