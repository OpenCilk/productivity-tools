import logging
import csv
import re

can_plot = True
try:
  import matplotlib
except ImportError:
  can_plot = False

logger = logging.getLogger(__name__)

# upper bound P-worker runtime for program with work T1 and parallelism PAR
def bound_runtime(T1, PAR, P):
  # We scale the span term by 1.7 based on the span coefficient in the
  # Cilkview paper.
  Tp = T1/P + (1.0-1.0/P)*1.7*T1/PAR
  return Tp

def sanitize_rows_to_plot(out_csv, rows_to_plot):
  # if any row numbers are invalid, set them to the last row by default;
  # "all" is a special case for plotting all rows in the CSV
  num_rows = 0
  with open(out_csv, "r") as out_csv_file:
    rows = csv.reader(out_csv_file, delimiter=",")
    num_rows = len(list(rows))

    if rows_to_plot == "all":
      return set(range(1, num_rows))

    new_rows_to_plot = set()
    for r in rows_to_plot:
      if r in range(1, num_rows):
        new_rows_to_plot.add(r)
      else:
        new_rows_to_plot.add(num_rows-1)

  return new_rows_to_plot

def get_row_data(out_csv, rows_to_plot, max_cpus=0):
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
        header = row
        # get num cpus (extracts num_cpus from, for example, "32c time (seconds)" )
        if re.match(r"\d+c", row[-1].split()[0]):
          num_cpus = int(row[-1].split()[0][:-1])
        else:
          num_cpus = 0

        # find parallelism col and start of benchmark cols
        for i in range(len(row)):
          if row[i] == "burdened_parallelism":
            par_col = i
          elif bench_col_start == 0 and re.match(r"(\d+)c", row[i]):
            min_count = int(re.match(r"(\d+)c", row[i]).group(1))
            if min_count != 1:
              logger.warning("Estimating 1-core running time from " + str(min_count) + " core running time")
            # subtract 1 because we will add 1-indexed cpu counts to this value
            bench_col_start = i-1
        if max_cpus == 0:
          max_cpus = num_cpus

      elif row_num in rows_to_plot:
        # collect data from this row of the csv file
        tag = row[0]
        parallelism = float(row[par_col])
        if num_cpus == 0:
          single_core_runtime = float("nan")
        else:
          single_core_runtime = float(row[bench_col_start+1]) * min_count

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

        bench_col = bench_col_start + 1
        for i in range(1, max_cpus+1):
          data["num_workers"].append(i)

          if i > num_cpus or bench_col >= len(row) or i != int(re.match(r"(\d+)c", header[bench_col]).group(1)):
            data["obs_runtime"].append(float("nan"))
            data["obs_speedup"].append(float("nan"))
          else:
            data["obs_runtime"].append(float(row[bench_col]))
            if 0.0 == float(row[bench_col]):
              data["obs_speedup"].append(float("nan"))
            else:
              data["obs_speedup"].append(single_core_runtime/float(row[bench_col]))
            bench_col += 1
          data["perf_lin_runtime"].append(single_core_runtime/i)
          greedy_runtime_bound = bound_runtime(single_core_runtime, parallelism, i)
          data["greedy_runtime"].append(greedy_runtime_bound)
          data["span_runtime"].append(single_core_runtime/parallelism)

          data["perf_lin_speedup"].append(1.0*i)
          if 0.0 == greedy_runtime_bound:
            data["greedy_speedup"].append(float("nan"))
          else:
            data["greedy_speedup"].append(single_core_runtime/(bound_runtime(single_core_runtime, parallelism, i)))
          data["span_speedup"].append(parallelism)

        all_data.append((tag, data))

      row_num += 1

  return all_data

# by default, plots the last row (i.e. overall execution)
def plot(out_csv="out.csv", out_plot="plot.pdf", rows_to_plot=[0], cpus=[]):

  rows_to_plot = sanitize_rows_to_plot(out_csv, rows_to_plot)
  all_data = get_row_data(out_csv, rows_to_plot, len(cpus))

  num_plots = len(all_data)

  clr_face_ax = [0.97] * 3;    # off-white
  grid_ax = {"b": True, "linestyle": ":"}

  logger.info("Generating plot (" + str(num_plots) + " subplots)")
  # matplotlib.use() must be called before importing matplotlib.pyplot.
  matplotlib.use('PDF')
  import matplotlib.pyplot as plt
  fig, axs = plt.subplots(nrows=num_plots, ncols=2, figsize=(12,6*num_plots), squeeze=False)

  for r in range(num_plots):
    tag, data = all_data[r]

    if tag == "":
      tag = "(No tag)"

    # legend shared between subplots.
    axs[r,0].plot(data["num_workers"], data["obs_runtime"], "mo", label="Observed", linestyle='None', markersize = 5)
    axs[r,0].plot(data["num_workers"], data["perf_lin_runtime"], "g", label="Perfect linear speedup")
    axs[r,0].plot(data["num_workers"], data["greedy_runtime"], "c", label="Burdened-dag bound")
    axs[r,0].plot(data["num_workers"], data["span_runtime"], "y", label="Span bound = " + "{:.5f} s".format(data["span_runtime"][0]))

    if cpus:
      num_workers = len(cpus)
    elif data["num_workers"]:
      num_workers = data["num_workers"][-1]
    else:
      continue

    axs[r,0].set_xlabel("Number of workers")
    axs[r,0].set_ylabel("Runtime (s)")
    axs[r,0].set_title(tag + " execution time")
    axs[r,0].set(xlim=[0,num_workers])
    axs[r,0].set_aspect(1.0/axs[r,0].get_data_ratio())
    axs[r,0].set_facecolor(clr_face_ax)
    axs[r,0].grid(**grid_ax)

    axs[r,0].legend(loc="upper right")


    axs[r,1].plot(data["num_workers"], data["obs_speedup"], "mo", label="Observed", linestyle='None', markersize = 5)
    axs[r,1].plot(data["num_workers"], data["perf_lin_speedup"], "g", label="Perfect linear speedup")
    axs[r,1].plot(data["num_workers"], data["greedy_speedup"], "c", label="Burdened-dag bound")
    axs[r,1].plot(data["num_workers"], data["span_speedup"], "y", label="Parallelism = " +
                  "{:.3f}".format(data["span_speedup"][0]))

    axs[r,1].set_xlabel("Number of workers")
    axs[r,1].set_ylabel("Speedup")
    axs[r,1].set_title(tag + " speedup")
    axs[r,1].set(xlim=[0,num_workers], ylim=[0,num_workers])
    axs[r,1].set_aspect(1.0/axs[r,1].get_data_ratio())
    axs[r,1].set_facecolor(clr_face_ax)
    axs[r,1].grid(**grid_ax)

    axs[r,1].legend(loc="upper left")

    # Add vertical lines to delimit sockets
    added_socket = []
    first_socket = True
    for cpu_id, socket_id in cpus:
      if len(added_socket) == 0 or socket_id != added_socket[-1]:
        added_socket.append(socket_id)
        if first_socket:
          first_socket = False
        else:
          axs[r,0].axvline(x=cpu_id, ls=':', c="gray")
          axs[r,1].axvline(x=cpu_id, ls=':', c="gray")

  plt.savefig(out_plot)

