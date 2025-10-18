#include "syzygy/tbprobe.h"

#include "syzygy/tbcore.h"

namespace bby::syzygy {

void initialize() {
  bby_syzygy_touch_symbol();
}

}  // namespace bby::syzygy
