import argparse
import logging
import sys
from runner import run, get_cpu_ordering
from plotter import plot, can_plot

logger = logging.getLogger(sys.argv[0])

def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("--cilkscale", "-c", help="binary compiled with -fcilktool=cilkscale", required=True)
  ap.add_argument("--cilkscale-benchmark", "-b", help="binary compiled with -fcilktool=cilkscale-benchmark", required=True)
  ap.add_argument("--cpu-counts", "-cpus", help="comma-separated list of cpu counts to use for benchmarking")
  ap.add_argument("--output-csv", "-ocsv", help="csv file for output data", default="out.csv")
  ap.add_argument("--output-plot", "-oplot", help="plot file dest", default="plot.pdf")
  ap.add_argument("--rows-to-plot", "-rplot", help="comma-separated list of rows to generate plots for (i.e. 0,1,2); or `all` to plot all rows", default="all")
  ap.add_argument("--args", "-a", nargs="*", help="binary arguments", default="")

  args = ap.parse_args()
  print(args)

  cpu_counts = args.cpu_counts

  out_csv = args.output_csv
  out_plot = args.output_plot

  bin_instrument = args.cilkscale
  bin_bench = args.cilkscale_benchmark
  bin_args = args.args

  logging.basicConfig(level=logging.INFO)
  if not can_plot:
    logger.warning("matplotlib required to generate plot.")

  # generate data and save to out_csv (defaults to out.csv)
  run(bin_instrument, bin_bench, bin_args, out_csv, cpu_counts)

  cpus = get_cpu_ordering()
  if can_plot and cpus:
    # generate plot
    # (out_plot defaults to plot.pdf)
    # (rows defaults to just the last row in the csv)
    rows_to_plot = args.rows_to_plot
    if rows_to_plot != "all":
      rows_to_plot = list(map(int, rows_to_plot.split(",")))
    plot(out_csv, out_plot, rows_to_plot, cpus)

if __name__ == '__main__':
  main()

