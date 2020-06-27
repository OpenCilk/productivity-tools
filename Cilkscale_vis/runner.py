import subprocess
import sys
import os
import time
import csv

# generate csv with parallelism numbers
def get_parallelism(bin_instrument, bin_args, out_csv):
  out,err = run_command("CILKSCALE_OUT=" + out_csv + " " + bin_instrument + " " + " ".join(bin_args))
  return

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
  rcommand = "taskset -c " + ",".join([str(p) for (p,m) in cpu_online]) + " " + rcommand
  print(rcommand)
  bench_out_csv = benchmark_tmp_output(P)
  proc = subprocess.Popen(['CILK_NWORKERS=' + str(P) + ' ' + "CILKSCALE_OUT=" + bench_out_csv + " " + rcommand], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out,err=proc.communicate()
  err = str(err, "utf-8")

def get_cpu_ordering():
  out,err = run_command("lscpu --parse")

  out = str(out, 'utf-8')

  avail_cpus = []
  for l in out.splitlines():
    if l.startswith('#'):
      continue
    items = l.strip().split(',')
    cpu_id = int(items[0])
    node_id = int(items[1])
    socket_id = int(items[2])
    avail_cpus.append((socket_id, node_id, cpu_id))

  avail_cpus = sorted(avail_cpus)
  ret = []
  added_nodes = dict()
  for x in avail_cpus:
    if x[1] not in added_nodes:
      added_nodes[x[1]] = True
    else:
      continue
    ret.append((x[2], x[0]))
  return ret

def run(bin_instrument, bin_bench, bin_args, out_csv="out.csv"):
  # get parallelism
  get_parallelism(bin_instrument, bin_args, out_csv)

  # get benchmark runtimes
  NCPUS = get_n_cpus()

  print("Generating scalability data for " + str(NCPUS) + " cpus.")

  # this will be prepended with CILK_NWORKERS and CILKSCALE_OUT in run_on_p_workers
  # any tmp files will be destroyed
  run_command = bin_bench + " " + " ".join(bin_args)
  results = dict()
  for i in range(1, NCPUS+1):
    results[i] = run_on_p_workers(i, run_command)

  new_rows = []

  # read data from out_csv
  with open(out_csv, "r") as out_csv_file:
    rows = out_csv_file.readlines()
    new_rows = rows.copy()
    for i in range(len(new_rows)):
      new_rows[i] = new_rows[i].strip("\n")

    # join all the csv data
    for i in range(1, NCPUS+1):
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

