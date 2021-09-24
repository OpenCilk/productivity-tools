import logging
import subprocess
import sys
import os
import time
import csv

logger = logging.getLogger(__name__)

def print_stdout_stderr(out, err, bin_instrument, bin_args):
  print()
  print(">> STDOUT (" + bin_instrument + " " + " ".join(bin_args) + ")")
  print(str(out,"utf-8"), file=sys.stdout, end='')
  print("<< END STDOUT")
  print()
  print(">> STDERR (" + bin_instrument + " " + " ".join(bin_args) + ")")
  print(str(err,"utf-8"), file=sys.stderr, end='')
  print("<< END STDERR")
  print()
  return

# generate csv with parallelism numbers
def get_parallelism(bin_instrument, bin_args, out_csv):
  out,err = run_command("CILKSCALE_OUT=" + out_csv + " " + bin_instrument + " " + " ".join(bin_args))
  return out,err

def run_command(cmd, asyn = False):
  proc = subprocess.Popen([cmd], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  if not asyn:
    out,err=proc.communicate()
    return out,err
  else:
    return ""

def get_n_cpus():
  return len(get_cpu_ordering())

def benchmark_tmp_output(n):
  return ".out.bench." + str(n) + ".csv"

def run_on_p_workers(P, rcommand):
  cpu_ordering = get_cpu_ordering()
  cpu_online = cpu_ordering[:P]

  time.sleep(0.1)
  if sys.platform != "darwin":
    rcommand = "taskset -c " + ",".join([str(p) for (p,m) in cpu_online]) + " " + rcommand
  logger.info('CILK_NWORKERS=' + str(P) + ' ' + rcommand)
  bench_out_csv = benchmark_tmp_output(P)
  proc = subprocess.Popen(['CILK_NWORKERS=' + str(P) + ' ' + "CILKSCALE_OUT=" + bench_out_csv + " " + rcommand], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out,err=proc.communicate()
  err = str(err, "utf-8")

def get_cpu_ordering():
  if sys.platform == "darwin":
    # TODO: Replace with something that analyzes CPU configuration on Darwin
    out,err = run_command("sysctl -n hw.physicalcpu_max")
    return [(0, p) for p in range(0,int(str(out, 'utf-8')))]
  else:
    out,err = run_command("lscpu --parse")

  out = str(out, 'utf-8')

  avail_cpus = []
  for l in out.splitlines():
    if l.startswith('#'):
      continue
    items = l.strip().split(',')
    cpu_id = int(items[0])
    core_id = int(items[1])
    socket_id = int(items[2])
    avail_cpus.append((socket_id, core_id, cpu_id))

  avail_cpus = sorted(avail_cpus)
  ret = []
  added_cores = dict()
  for x in avail_cpus:
    if x[1] not in added_cores:
      added_cores[x[1]] = True
    else:
      continue
    ret.append((x[2], x[0]))
  return ret

def run(bin_instrument, bin_bench, bin_args, out_csv="out.csv", cpu_counts=None):
  # get parallelism
  out,err = get_parallelism(bin_instrument, bin_args, out_csv)

  # print execution output (stdout/stderr) as user feedback
  print_stdout_stderr(out, err, bin_instrument, bin_args)

  # get benchmark runtimes
  NCPUS = get_n_cpus()
  if cpu_counts is None:
    cpu_counts = range(1, NCPUS+1)
  else:
    cpu_counts = list(map(int, cpu_counts.split(",")))

  logger.info("Generating scalability data for " + str(NCPUS) + " cpus.")

  # this will be prepended with CILK_NWORKERS and CILKSCALE_OUT in run_on_p_workers
  # any tmp files will be destroyed
  run_command = bin_bench + " " + " ".join(bin_args)
  results = dict()
  last_CPU = NCPUS+1
  for i in range(1, NCPUS+1):
    if i in cpu_counts:
      try:
        results[i] = run_on_p_workers(i, run_command)
      except KeyboardInterrupt:
        logger.info("Benchmarking stopped early at " + str(i-1) + " cpus.")
        last_CPU = i
        break

  new_rows = []

  # read data from out_csv
  with open(out_csv, "r") as out_csv_file:
    rows = out_csv_file.readlines()
    new_rows = rows.copy()
    for i in range(len(new_rows)):
      new_rows[i] = new_rows[i].strip("\n")

    # join all the csv data
    for i in range(1, last_CPU):
      if i not in cpu_counts:
        continue
      with open(benchmark_tmp_output(i), "r") as csvfile:
        reader = csv.reader(csvfile, delimiter=',')
        row_num = 0
        for row in reader:
          if row_num == 0:
            col_header = str(i) + "c " + row[1].strip()
            new_rows[row_num] += "," + col_header
          else:
            # second col contains runtime
            time = row[1].strip()
            new_rows[row_num] += "," + time
          row_num += 1
      os.remove(benchmark_tmp_output(i))

    for i in range(len(new_rows)):
      new_rows[i] += "\n"

  # write the joined data to out_csv
  with open(out_csv, "w") as out_csv_file:
    out_csv_file.writelines(new_rows)

