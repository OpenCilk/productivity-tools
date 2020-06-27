import csv
import matplotlib
import matplotlib.pyplot as plt

# upper bound P-worker runtime for program with work T1 and parallelism PAR
def bound_runtime(T1, PAR, P):
  # We scale the span term by 1.7 based on the span coefficient in the
  # Cilkview paper.
  Tp = T1/P + (1.0-1.0/P)*1.7*T1/PAR
  return Tp

def sanitize_rows_to_plot(out_csv, rows_to_plot):
  # if any row numbers are invalid, set them to the last row by default
  num_rows = 0
  with open(out_csv, "r") as out_csv_file:
    rows = csv.reader(out_csv_file, delimiter=",")
    num_rows = len(list(rows))

    new_rows_to_plot = set()
    for r in rows_to_plot:
      if r in range(1, num_rows):
        new_rows_to_plot.add(r)
      else:
        new_rows_to_plot.add(num_rows-1)

  return new_rows_to_plot

def get_row_data(out_csv, rows_to_plot):
  # list of (tag, data)
  all_data = []

  with open(out_csv, "r") as out_csv_file:
    rows = csv.reader(out_csv_file, delimiter=",")

    row_num = 0
    num_cpus = 0

    par_col = 0
    bench_col_start = 0

    for row in rows:
      if row_num == 0:
        # get num cpus (extracts num_cpus from, for example, "32c time (seconds)" )
        num_cpus = int(row[-1].split()[0][:-1])
        # find parallelism col and start of benchmark cols
        for i in range(len(row)):
          if row[i] == "burdened_parallelism":
            par_col = i
          elif row[i].startswith("1c"):
            # subtract 1 because we will add 1-indexed cpu counts to this value
            bench_col_start = i-1

      elif row_num in rows_to_plot:
        # collect data from this row of the csv file
        tag = row[0]
        parallelism = float(row[par_col])
        single_core_runtime = float(row[bench_col_start+1])

        data = {}
        data["num_workers"] = []
        data["obs_runtime"] = []
        data["perf_lin_runtime"] = []
        data["greedy_runtime"] = []
        data["span_runtime"] = []
        data["obs_speedup"] = []
        data["perf_lin_speedup"] = []
        data["greedy_speedup"] = []
        data["span_speedup"] = []

        for i in range(1, num_cpus+1):
          data["num_workers"].append(i)

          data["obs_runtime"].append(float(row[bench_col_start+i]))
          data["perf_lin_runtime"].append(single_core_runtime/i)
          data["greedy_runtime"].append(bound_runtime(single_core_runtime, parallelism, i))
          data["span_runtime"].append(single_core_runtime/parallelism)

          data["obs_speedup"].append(single_core_runtime/float(row[bench_col_start+i]))
          data["perf_lin_speedup"].append(1.0*i)
          data["greedy_speedup"].append(single_core_runtime/(bound_runtime(single_core_runtime, parallelism, i)))
          data["span_speedup"].append(parallelism)

        all_data.append((tag, data))

      row_num += 1

  return all_data

# by default, plots the last row (i.e. overall execution)
def plot(out_csv="out.csv", out_plot="plot.pdf", rows_to_plot=[0]):

  rows_to_plot = sanitize_rows_to_plot(out_csv, rows_to_plot)
  all_data = get_row_data(out_csv, rows_to_plot)

  num_plots = len(all_data)

  print("Generate plot")
  matplotlib.use('PDF')
  fig, axs = plt.subplots(nrows=num_plots, ncols=2, figsize=(12,6*num_plots))

  for r in range(num_plots):
    tag, data = all_data[r]

    # legend shared between subplots.
    axs[r,0].plot(data["num_workers"], data["obs_runtime"], "mo", label="Observed", linestyle='None', markersize = 5)
    axs[r,0].plot(data["num_workers"], data["perf_lin_runtime"], "g", label="Perfect linear speedup")
    axs[r,0].plot(data["num_workers"], data["greedy_runtime"], "c", label="Burdened-dag bound")
    axs[r,0].plot(data["num_workers"], data["span_runtime"], "y", label="Span bound")

    num_workers = data["num_workers"][-1]

    axs[r,0].set_xlabel("Num workers")
    axs[r,0].set_ylabel("Runtime")
    axs[r,0].set_title(tag + " execution time")
    axs[r,0].set_aspect(1.0/axs[r,0].get_data_ratio())


    axs[r,1].plot(data["num_workers"], data["obs_speedup"], "mo", label="Observed", linestyle='None', markersize = 5)
    axs[r,1].plot(data["num_workers"], data["perf_lin_speedup"], "g", label="Perfect linear speedup")
    axs[r,1].plot(data["num_workers"], data["greedy_speedup"], "c", label="Burdened-dag bound")
    axs[r,1].plot(data["num_workers"], data["span_speedup"], "y", label="Span bound")

    axs[r,1].set_xlabel("Num workers")
    axs[r,1].set_ylabel("Speedup")
    axs[r,1].set_title(tag + " speedup")
    axs[r,1].set(xlim=[0,num_workers], ylim=[0,num_workers])

    axs[r,1].legend(loc="upper left")

  plt.savefig(out_plot)

