#include "export_symbols.h"

int main(void)
{
  /* This fixture is linked, not run. Keep the generated list length in the
     executable so a changed list also changes optimized output. */
  return (int)(sizeof(symbols)/sizeof(symbols[0]));
}
