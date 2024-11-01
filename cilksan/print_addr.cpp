#include "csan.h"
#include "cilksan_internal.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <signal.h>
#include <sstream>
#include <unistd.h>
#include <unordered_map>

extern bool is_running_under_rr;

std::ostream &outs = std::cerr;
std::ofstream outf;

// Mappings from CSI ID to associated program counter.
uintptr_t *call_pc = nullptr;
uintptr_t *spawn_pc = nullptr;
uintptr_t *loop_pc = nullptr;
uintptr_t *load_pc = nullptr;
uintptr_t *store_pc = nullptr;
uintptr_t *alloca_pc = nullptr;
uintptr_t *allocfn_pc = nullptr;
allocfn_prop_t *allocfn_prop = nullptr;
uintptr_t *free_pc = nullptr;

typedef enum {
  LOAD_ACC,
  STORE_ACC,
  CALL_LOAD_ACC,
  CALL_STORE_ACC,
  ALLOC_LOAD_ACC,
  ALLOC_STORE_ACC,
  FREE_ACC,
  REALLOC_ACC,
  STACK_FREE_ACC,
} ACC_TYPE;

// Helper function to get string describing a variable location from a
// obj_source_loc_t.
static std::string
get_obj_info_str(const obj_source_loc_t *obj_src_loc, const Decorator &d) {
  if (!obj_src_loc)
    return "<no information on variable>";

  std::ostringstream convert;
  // std::string type(obj_src_loc->type ?
  //                  obj_src_loc->type :
  //                  "<no variable type>");
  std::string variable(obj_src_loc->name ?
                       (std::string(d.Variable()) + obj_src_loc->name +
                        std::string(d.Default())) :
                       "<no variable name>");
  std::string filename(obj_src_loc->filename ?
                       (std::string(d.Filename()) + obj_src_loc->filename +
                        std::string(d.Default())) :
                       "<no filename>");
  int32_t line_no = obj_src_loc->line_number;

  // if (obj_src_loc->type)
  //   convert << type << " ";

  convert << variable
          << " (declared at " << filename;
  if (line_no >= 0)
    convert << d.Filename() << ":" << std::dec << line_no << d.Default();
  convert << ")";

  return convert.str();
}

// Helper function to get string describing source location from a
// csan_source_loc_t.
static std::string
get_src_info_str(const csan_source_loc_t *src_loc, const Decorator &d) {
  if (!src_loc)
    return "<no information on source location>";

  std::ostringstream convert;
  std::string file(src_loc->filename ?
                   (std::string(d.Filename()) + src_loc->filename +
                    std::string(d.Default())) :
                   "<no file name>");
  std::string funcname(src_loc->name ?
                       (std::string(d.Function()) + src_loc->name +
                        std::string(d.Default())) :
                       "<no function name>");
  int32_t line_no = src_loc->line_number;
  int32_t col_no = src_loc->column_number;

  convert << " " << funcname;
  convert << " " << file;
  if (line_no >= 0 && col_no >= 0) {
    convert << d.Filename();
    convert << ":" << std::dec << line_no;
    convert << ":" << std::dec << col_no;
    convert << d.Default();
  }
  return convert.str();
}

static std::string get_info_on_alloca(const csi_id_t alloca_id,
                                      const Decorator &d) {
  std::ostringstream convert;

  // Get source and object information.
  const csan_source_loc_t *src_loc = nullptr;
  const obj_source_loc_t *obj_src_loc = nullptr;
  // Even alloca_id's are stack allocations
  if (alloca_id % 2) {
    convert << d.RaceLoc() << " Heap object " << d.Default();
    src_loc = __csan_get_allocfn_source_loc(alloca_id / 2);
    obj_src_loc = __csan_get_allocfn_obj_source_loc(alloca_id / 2);
  } else {
    convert << d.RaceLoc() << "Stack object " << d.Default();
    src_loc = __csan_get_alloca_source_loc(alloca_id / 2);
    obj_src_loc = __csan_get_alloca_obj_source_loc(alloca_id / 2);
  }

  convert << get_obj_info_str(obj_src_loc, d) << "\n";

  uintptr_t pc = (alloca_id % 2) ? allocfn_pc[alloca_id / 2] :
                     alloca_pc[alloca_id / 2];

  convert << d.RaceLoc();
  if (alloca_id % 2) {
    convert << "      Call "
            << d.InstAddress() << std::hex << pc << d.Default();
    if (allocfn_prop[alloca_id / 2].allocfn_ty != uint8_t(-1))
      convert << " to " << d.Function()
              << __csan_get_allocfn_str(allocfn_prop[alloca_id / 2])
              << d.Default();
  } else {
    convert << "     Alloc "
            << d.InstAddress() << std::hex << pc << d.Default();
  }

  if (src_loc)
    convert << " in" << get_src_info_str(src_loc, d);

  return convert.str();
}

static std::string
get_info_on_mem_access(const csi_id_t acc_id, ACC_TYPE type, uint8_t endpoint,
                       const Decorator &d) {
  std::ostringstream convert;

  convert << d.Bold() << d.RaceLoc();
  switch (type) {
  case LOAD_ACC:
  case CALL_LOAD_ACC:
  case ALLOC_LOAD_ACC:
    convert << "   Read ";
    break;
  case STORE_ACC:
  case CALL_STORE_ACC:
  case ALLOC_STORE_ACC:
    convert << "  Write ";
    break;
  case FREE_ACC:
  case STACK_FREE_ACC:
    convert << "   Free ";
    break;
  case REALLOC_ACC:
    convert << "Realloc ";
    break;
  }
  convert << d.Default();

  // Get PC for this access.
  if (UNKNOWN_CSI_ID != acc_id) {
    convert << d.InstAddress();
    switch (type) {
    case LOAD_ACC:
      convert << std::hex << load_pc[acc_id];
      break;
    case STORE_ACC:
      convert << std::hex << store_pc[acc_id];
      break;
    case CALL_LOAD_ACC:
    case CALL_STORE_ACC:
      convert << std::hex << call_pc[acc_id];
      break;
    case ALLOC_LOAD_ACC:
    case ALLOC_STORE_ACC:
      convert << std::hex << allocfn_pc[acc_id];
      break;
    case FREE_ACC:
      convert << std::hex << free_pc[acc_id];
      break;
    case REALLOC_ACC:
      convert << std::hex << allocfn_pc[acc_id];
      break;
    case STACK_FREE_ACC:
      convert << std::hex << call_pc[acc_id];
      break;
    }
    convert << d.Default();
  }

  // Get source information.
  const csan_source_loc_t *src_loc = nullptr;
  if (UNKNOWN_CSI_ID != acc_id) {
    switch (type) {
    case LOAD_ACC:
      src_loc = __csan_get_load_source_loc(acc_id);
      break;
    case STORE_ACC:
      src_loc = __csan_get_store_source_loc(acc_id);
      break;
    case CALL_LOAD_ACC:
    case CALL_STORE_ACC:
      src_loc = __csan_get_call_source_loc(acc_id);
      break;
    case ALLOC_LOAD_ACC:
    case ALLOC_STORE_ACC:
      src_loc = __csan_get_allocfn_source_loc(acc_id);
      break;
    case FREE_ACC:
      src_loc = __csan_get_free_source_loc(acc_id);
      break;
    case REALLOC_ACC:
      src_loc = __csan_get_allocfn_source_loc(acc_id);
      break;
    case STACK_FREE_ACC:
      src_loc = __csan_get_call_source_loc(acc_id);
      break;
    }
  }

  convert << get_src_info_str(src_loc, d);

  // Get object information
  const obj_source_loc_t *obj_src_loc = nullptr;
  if (UNKNOWN_CSI_ID != acc_id) {
    switch (type) {
    case LOAD_ACC:
      obj_src_loc = __csan_get_load_obj_source_loc(acc_id);
      break;
    case STORE_ACC:
      obj_src_loc = __csan_get_store_obj_source_loc(acc_id);
      break;
    // TODO: Track objects modified by allocfn's, free's, and realloc's.
    default:
      break;
    }
  }
  if (obj_src_loc) {
    convert << "\n" << (endpoint == 0 ? "| " : "||")
            << "       `-to variable ";

    convert << get_obj_info_str(obj_src_loc, d);
  }

  return convert.str();
}

static std::string get_info_on_call(const CallID_t &call, const Decorator &d) {
  std::ostringstream convert;
  convert << d.RaceLoc();
  switch (call.getType()) {
  case CALL:
    convert << "  Call ";
    break;
  case SPAWN:
    convert << " Spawn ";
    break;
  case LOOP:
    convert << "Parfor ";
    break;
  }
  convert << d.Default();

  if (call.isUnknownID()) {
    convert << "<no information on source location>";
    return convert.str();
  }

  uintptr_t pc = (uintptr_t)nullptr;
  switch (call.getType()) {
  case CALL:
    pc = call_pc[call.getID()];
    break;
  case SPAWN:
    pc = spawn_pc[call.getID()];
    break;
  case LOOP:
    pc = loop_pc[call.getID()];
    break;
  }
  convert << d.InstAddress() << std::hex << pc << d.Default();

  const csan_source_loc_t *src_loc = nullptr;
  switch (call.getType()) {
  case CALL:
    src_loc = __csan_get_call_source_loc(call.getID());
    break;
  case SPAWN:
    src_loc = __csan_get_detach_source_loc(call.getID());
    break;
  case LOOP:
    src_loc = __csan_get_loop_source_loc(call.getID());
    break;
  }

  convert << get_src_info_str(src_loc, d);

  return convert.str();
}

int get_call_stack_divergence_pt(
    const std::unique_ptr<std::pair<CallID_t, uintptr_t>[]> &first_call_stack,
    int first_call_stack_size,
    const std::unique_ptr<std::pair<CallID_t, uintptr_t>[]> &second_call_stack,
    int second_call_stack_size) {
  int i;
  int end =
    (first_call_stack_size < second_call_stack_size) ?
    first_call_stack_size : second_call_stack_size;
  for (i = 0; i < end; ++i)
    // if (first_call_stack[i].first != second_call_stack[i].first)
    if (first_call_stack[i].second != second_call_stack[i].second)
      // TODO: For Loop entries in the call stack, use versioning to distinguish
      // different iterations of the loop.
      break;
  return i;
}

static std::unique_ptr<std::pair<CallID_t, uintptr_t>[]>
get_call_stack(const AccessLoc_t &instrAddr) {
  int stack_size = instrAddr.getCallStackSize();
  std::unique_ptr<std::pair<CallID_t, uintptr_t>[]>
      call_stack(new std::pair<CallID_t, uintptr_t>[stack_size]);
  {
    const call_stack_node_t *call_stack_node = instrAddr.getCallStack();
    for (int i = stack_size - 1;
         i >= 0;
         --i, call_stack_node = call_stack_node->getPrev()) {
      call_stack[i].first = call_stack_node->getCallID();
      call_stack[i].second = reinterpret_cast<uintptr_t>(call_stack_node);
    }
  }
  return call_stack;
}

bool CilkSanImpl_t::ColorizeReports() {
  char *e = getenv("CILKSAN_COLOR_REPORT");
  if (e) {
    if (0 == strcmp(e, "0"))
      return false;
    else if (0 == strcmp(e, "1"))
      return true;
  }
  return isatty(STDERR_FILENO) != 0;
}

bool CilkSanImpl_t::PauseOnRace() {
  char *e = getenv("CILKSAN_DEBUGGER");
  if (e && 0 == strcmp(e, "1"))
    return true;
  return false;
}

bool CilkSanImpl_t::RunningUnderRR() {
  // Check if we're running under RR
  char *e = getenv("RUNNING_UNDER_RR");
  if (e && 0 == strcmp(e, "1"))
    return true;
  return false;
}

// static void print_race_info(const RaceInfo_t& race) {
void RaceInfo_t::print(const AccessLoc_t &first_inst,
                       const AccessLoc_t &second_inst,
                       const AccessLoc_t &alloc_inst,
                       const Decorator &d) const {
  outs << d.Bold() << d.Error() << "Race detected on location "
    // << (is_on_stack(race.addr) ? "stack address " : "address ")
            << std::hex << addr << d.Default() << std::dec << "\n";

  std::string first_acc_info, second_acc_info;
  ACC_TYPE first_acc_type, second_acc_type;
  switch(type) {
  case RW_RACE:
    switch(first_inst.getType()) {
    case MAType_t::FNRW:
      first_acc_type = CALL_LOAD_ACC;
      break;
    case MAType_t::ALLOC:
      first_acc_type = ALLOC_LOAD_ACC;
      break;
    default:
      first_acc_type = LOAD_ACC;
      break;
    }
    switch (second_inst.getType()) {
    case MAType_t::FNRW:
      second_acc_type = CALL_STORE_ACC;
      break;
    case MAType_t::ALLOC:
      second_acc_type = ALLOC_STORE_ACC;
      break;
    case MAType_t::FREE:
      second_acc_type = FREE_ACC;
      break;
    case MAType_t::REALLOC:
      second_acc_type = REALLOC_ACC;
      break;
    case MAType_t::STACK_FREE:
      second_acc_type = STACK_FREE_ACC;
      break;
    default:
      second_acc_type = STORE_ACC;
      break;
    }
    break;
  case WW_RACE:
    switch (first_inst.getType()) {
    case MAType_t::FNRW:
      first_acc_type = CALL_STORE_ACC;
      break;
    case MAType_t::ALLOC:
      first_acc_type = ALLOC_STORE_ACC;
      break;
    case MAType_t::FREE:
      first_acc_type = FREE_ACC;
      break;
    case MAType_t::REALLOC:
      first_acc_type = REALLOC_ACC;
      break;
    case MAType_t::STACK_FREE:
      first_acc_type = STACK_FREE_ACC;
      break;
    default:
      first_acc_type = STORE_ACC;
      break;
    }
    switch (second_inst.getType()) {
    case MAType_t::FNRW:
      second_acc_type = CALL_STORE_ACC;
      break;
    case MAType_t::ALLOC:
      second_acc_type = ALLOC_STORE_ACC;
      break;
    case MAType_t::FREE:
      second_acc_type = FREE_ACC;
      break;
    case MAType_t::REALLOC:
      second_acc_type = REALLOC_ACC;
      break;
    case MAType_t::STACK_FREE:
      second_acc_type = STACK_FREE_ACC;
      break;
    default:
      second_acc_type = STORE_ACC;
      break;
    }
    break;
  case WR_RACE:
    switch (first_inst.getType()) {
    case MAType_t::FNRW:
      first_acc_type = CALL_STORE_ACC;
      break;
    case MAType_t::ALLOC:
      first_acc_type = ALLOC_STORE_ACC;
      break;
    case MAType_t::FREE:
      first_acc_type = FREE_ACC;
      break;
    case MAType_t::REALLOC:
      first_acc_type = REALLOC_ACC;
      break;
    case MAType_t::STACK_FREE:
      first_acc_type = STACK_FREE_ACC;
      break;
    default:
      first_acc_type = STORE_ACC;
      break;
    }
    switch (second_inst.getType()) {
    case MAType_t::FNRW:
      second_acc_type = CALL_LOAD_ACC;
      break;
    case MAType_t::ALLOC:
      second_acc_type = ALLOC_LOAD_ACC;
      break;
    default:
      second_acc_type = LOAD_ACC;
      break;
    }
    break;
  }
  first_acc_info =
      get_info_on_mem_access(first_inst.getID(), first_acc_type, 0, d);
  second_acc_info =
      get_info_on_mem_access(second_inst.getID(), second_acc_type, 1, d);

  // Extract the two call stacks
  int first_call_stack_size = first_inst.getCallStackSize();
  int second_call_stack_size = second_inst.getCallStackSize();
  auto first_call_stack = get_call_stack(first_inst);
  auto second_call_stack = get_call_stack(second_inst);

  // Determine where the two call stacks diverge
  int divergence = get_call_stack_divergence_pt(
      first_call_stack,
      first_call_stack_size,
      second_call_stack,
      second_call_stack_size);

  // Print the two accesses involved in the race
  outs << d.Bold() << "*  " << d.Default() << first_acc_info << "\n";
  for (int i = first_call_stack_size - 1; i >= divergence; --i)
    outs << "+   " << get_info_on_call(first_call_stack[i].first, d) << "\n";
  outs << "|" << d.Bold() << "* " << d.Default() << second_acc_info << "\n";
  for (int i = second_call_stack_size - 1; i >= divergence; --i)
    outs << "|+  " << get_info_on_call(second_call_stack[i].first, d) << "\n";

  // Print the common calling context
  if (divergence > 0) {
    outs << "\\| Common calling context\n";
    for (int i = divergence - 1; i >= 0; --i)
      outs << " +  " << get_info_on_call(first_call_stack[i].first, d) << "\n";
  }

  // Print the allocation
  if (alloc_inst.isValid()) {
    outs << "   Allocation context\n";
    const csi_id_t alloca_id = alloc_inst.getID();
    outs << "    " << get_info_on_alloca(alloca_id, d) << "\n";

    auto alloc_call_stack = get_call_stack(alloc_inst);
    for (int i = alloc_inst.getCallStackSize() - 1; i >= 0; --i)
      outs << "    " << get_info_on_call(alloc_call_stack[i].first, d) << "\n";
  }

  outs << "\n";
}

static void open_outf(void) {
  const char *envstr = getenv("CILKSAN_OUT");
  if (envstr)
    outf.open(envstr);
  else if (is_running_under_rr)
    outf.open("cilksan_races.out");
}

// Log the race detected
void CilkSanImpl_t::report_race(
    const AccessLoc_t &first_inst, const AccessLoc_t &second_inst,
    const AccessLoc_t &alloc_inst, uintptr_t addr,
    enum RaceType_t race_type) {
  static int last_race_count = 0;
  bool found = false;
  // TODO: Make the key computation consistent with is_equivalent_race().
  uint64_t key = first_inst < second_inst ?
                              first_inst.getID() : second_inst.getID();
  RaceInfo_t race(first_inst, second_inst, alloc_inst, addr, race_type);

  std::pair<RaceMap_t::iterator, RaceMap_t::iterator> range;
  range = races_found.equal_range(key);
  while (range.first != range.second) {
    const RaceInfo_t &in_map = range.first->second;
    if (race.is_equivalent_race(in_map)) {
      found = true;
      break;
    }
    range.first++;
  }
  if (found) { // increment the dup count
    duplicated_races++;
  } else {
    // have to get the info before user program exits
    if (is_running_under_rr) {
      // Open outf if it is not open already.
      if (!outf.is_open())
        open_outf();

      outf << "race " << std::hex << addr << std::dec << " "
           << first_inst.getID() << " " << second_inst.getID() << "\n";
      if (!last_race_count) {
        outs << "Cilksan detected " << get_num_races_found()
             << " racing pairs.";
        last_race_count = get_num_races_found();
      } else if (get_num_races_found() >= 17 * last_race_count / 16) {
        outs << "\rCilksan detected " << get_num_races_found()
             << " racing pairs.";
        last_race_count = get_num_races_found();
      }
    } else
      race.print(first_inst, second_inst, alloc_inst, Decorator(color_report));
    races_found.insert(std::make_pair(key, race));
    if (PauseOnRace())
      // Raise a SIGTRAP to let the user examine the state of the program at
      // this point within the debugger.
      raise(SIGTRAP);
  }
}

void CilkSanImpl_t::report_race(
    const AccessLoc_t &first_inst, const AccessLoc_t &second_inst,
    uintptr_t addr, enum RaceType_t race_type) {
  report_race(first_inst, second_inst, AccessLoc_t(), addr, race_type);
}

int CilkSanImpl_t::get_num_races_found() {
  return races_found.size();
}

void CilkSanImpl_t::print_race_report() {
  outs << "\n";
  outs << "Cilksan detected " << get_num_races_found() << " distinct races.\n";
  if (!is_running_under_rr) {
    outs << "Cilksan suppressed " << duplicated_races
         << " duplicate race reports.\n";
    outs << "\n";
  }
}
