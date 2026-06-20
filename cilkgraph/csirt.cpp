#include <cilk/cilk_api.h>
#include <csi/csi.h>

#define CSIRT_API __attribute__((visibility("default")))


EXTERN_C

typedef struct {
  int64_t num_entries;
  csi_id_t *id_base;
  const source_loc_t *entries;
} unit_fed_table_t;

typedef struct {
  int64_t num_entries;
  const sizeinfo_t *entries;
} unit_sizeinfo_table_t;

typedef void (*__csi_init_callsite_to_functions)();

CSIRT_API
void __csirt_unit_init(const char *const name,
                       unit_fed_table_t *unit_fed_tables,
                       unit_sizeinfo_table_t *unit_sizeinfo_tables,
                       __csi_init_callsite_to_functions callsite_to_func_init) {
}
EXTERN_C_END
