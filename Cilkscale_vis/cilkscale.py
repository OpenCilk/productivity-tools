import argparse
from runner import run
from plotter import plot

def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("--cilkscale", "-c", help="binary compiled with -fcilktool=cilkscale", required=True)
  ap.add_argument("--cilkscale-benchmark", "-b", help="binary compiled with -fcilktool=cilkscale-benchmark", required=True)
  ap.add_argument("--output-csv", "-ocsv", help="csv file for output data")
  ap.add_argument("--output-plot", "-oplot", help="plot file dest")
  ap.add_argument("--rows-to-plot", "-rplot", help="comma-separated list of rows to generate plots for (i.e. 1,2,3)")
  ap.add_argument("--args", "-a", nargs="*", help="binary arguments")

  args = ap.parse_args()
  print(args)

  out_csv = args.output_csv or "out.csv"
  out_plot = args.output_plot or "plot.pdf"

  bin_instrument = args.cilkscale
  bin_bench = args.cilkscale_benchmark
  bin_args = args.args

  # generate data and save to out_csv (defaults to out.csv)
  run(bin_instrument, bin_bench, bin_args, out_csv)

  # generate plot
  # (out_plot defaults to plot.pdf)
  # (rows defaults to just the last row in the csv)
  rows_to_plot = list(map(int, args.rows_to_plot.split(",")))
  plot(out_csv, out_plot, rows_to_plot)

if __name__ == '__main__':
  main()

