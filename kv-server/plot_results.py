#!/usr/bin/env python3
import argparse
import glob
import os
from typing import List

import pandas as pd
import matplotlib.pyplot as plt


def load_all_csv(csv_dir: str) -> pd.DataFrame:
    pattern = os.path.join(csv_dir, "*.csv")
    files: List[str] = glob.glob(pattern)
    if not files:
        raise SystemExit(f"No CSV files found in {csv_dir!r}")

    frames = []
    for path in files:
        try:
            df = pd.read_csv(path)
            df["source_file"] = os.path.basename(path)
            frames.append(df)
        except Exception as e:
            print(f"Warning: failed to read {path}: {e}")

    if not frames:
        raise SystemExit("No CSV files could be read successfully.")

    all_data = pd.concat(frames, ignore_index=True)

    # Drop runs with ok == 0 (failed experiments)
    if "ok" in all_data.columns:
        before = len(all_data)
        all_data = all_data[all_data["ok"] > 0].copy()
        after = len(all_data)
        if after < before:
            print(f"Filtered out {before - after} rows with ok == 0")

    return all_data


def plot_workload(df: pd.DataFrame, workload_name: str, output_dir: str) -> None:
    sub = df[df["workload"] == workload_name].copy()
    if sub.empty:
        print(f"No data for workload={workload_name!r}, skipping plots.")
        return

    if "clients" not in sub.columns or "thr_rps" not in sub.columns:
        raise SystemExit("CSV must have 'clients' and 'thr_rps' columns.")

    # Basic throughput + latency
    agg = sub.groupby("clients", as_index=False).agg(
        thr_rps=("thr_rps", "mean"),
        avg_ms=("avg_ms", "mean"),
        p95_ms=("p95_ms", "mean"),
    )
    agg = agg.sort_values("clients")

    print(f"\n=== {workload_name} ===")
    print(agg[["clients", "thr_rps", "avg_ms", "p95_ms"]])

    safe_name = workload_name.replace(" ", "_")

    # Throughput vs clients
    plt.figure()
    plt.plot(agg["clients"], agg["thr_rps"], marker="o")
    plt.xlabel("Number of clients")
    plt.ylabel("Throughput (requests/second)")
    plt.title(f"Throughput vs Clients – {workload_name}")
    plt.grid(True)
    plt.tight_layout()
    out_thr = os.path.join(output_dir, f"{safe_name}_throughput.png")
    plt.savefig(out_thr)
    plt.close()
    print(f"Saved {out_thr}")

    # Latency vs clients (avg + p95)
    plt.figure()
    plt.plot(agg["clients"], agg["avg_ms"], marker="o", label="Average latency")
    plt.plot(agg["clients"], agg["p95_ms"], marker="s", label="p95 latency")
    plt.xlabel("Number of clients")
    plt.ylabel("Latency (ms)")
    plt.title(f"Latency vs Clients – {workload_name}")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    out_lat = os.path.join(output_dir, f"{safe_name}_latency.png")
    plt.savefig(out_lat)
    plt.close()
    print(f"Saved {out_lat}")

    # CPU utilization vs clients (if present)
    if "cpu_utilization" in sub.columns:
        cpu_agg = sub.groupby("clients", as_index=False)["cpu_utilization"].mean()
        cpu_agg = cpu_agg.sort_values("clients")

        plt.figure()
        plt.plot(cpu_agg["clients"], cpu_agg["cpu_utilization"], marker="o")
        plt.xlabel("Number of clients")
        plt.ylabel("CPU utilization (%)")
        plt.title(f"CPU utilization vs Clients – {workload_name}")
        plt.grid(True)
        plt.tight_layout()
        out_cpu = os.path.join(output_dir, f"{safe_name}_cpu.png")
        plt.savefig(out_cpu)
        plt.close()
        print(f"Saved {out_cpu}")

    # Disk throughput vs clients (if present)
    if "disk_read_MBps" in sub.columns and "disk_write_MBps" in sub.columns:
        disk_agg = sub.groupby("clients", as_index=False)[
            ["disk_read_MBps", "disk_write_MBps"]
        ].mean()
        disk_agg = disk_agg.sort_values("clients")

        plt.figure()
        plt.plot(disk_agg["clients"], disk_agg["disk_read_MBps"], marker="o", label="Read MB/s")
        plt.plot(disk_agg["clients"], disk_agg["disk_write_MBps"], marker="s", label="Write MB/s")
        plt.xlabel("Number of clients")
        plt.ylabel("Disk throughput (MB/s)")
        plt.title(f"Disk throughput vs Clients – {workload_name}")
        plt.legend()
        plt.grid(True)
        plt.tight_layout()
        out_disk = os.path.join(output_dir, f"{safe_name}_disk.png")
        plt.savefig(out_disk)
        plt.close()
        print(f"Saved {out_disk}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot throughput, latency, CPU and disk vs clients for DECS KV-server workloads."
    )
    parser.add_argument(
        "--csv-dir",
        default="csv",
        help="Directory containing CSV result files (default: ./csv)",
    )
    parser.add_argument(
        "--out-dir",
        default="plots",
        help="Directory to store generated PNG plots (default: ./plots)",
    )
    args = parser.parse_args()

    csv_dir = os.path.abspath(args.csv_dir)
    out_dir = os.path.abspath(args.out_dir)

    os.makedirs(out_dir, exist_ok=True)

    data = load_all_csv(csv_dir)

    workloads = ["put-all", "get-popular", "get-all"]
    for w in workloads:
        plot_workload(data, w, out_dir)


if __name__ == "__main__":
    main()
