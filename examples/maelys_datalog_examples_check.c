#include "examples/domains/maelys_datalog_example_domains.h"
#include "include/maelys_datalog.h"

#include <assert.h>

int main(void) {
    assert(maelys_datalog_example_domains_install() == MAELYS_OK);
    assert(maelys_datalog_domain_registry_find("graph") != NULL);
    assert(maelys_datalog_domain_registry_find("decision") != NULL);
    return 0;
}
