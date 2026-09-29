/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
static size_t live;
static void observe(size_t n) {live=n;}
static void at_exit_check(void) {
    /* Destructor priorities are independent of atexit registration: this
     * checks explicit dlclose, not an assumed ordering of exit handlers. */
    assert(live==0);
}
int main(int argc,char **argv) {
    assert(argc==2);assert(!atexit(at_exit_check));
    for(unsigned i=0;i<3;++i) {
        void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        if(!lib) {fprintf(stderr,"%s\n",dlerror());return 1;}
        int (*exercise)(void (*)(size_t))=(int (*)(void (*)(size_t)))dlsym(lib,"exercise_recycle");
        assert(exercise && !exercise(observe) && live==1);
        assert(!dlclose(lib));assert(live==0);
    }
    puts("session recycling: dlopen/dlclose frees the idle block on all three unloads PASS");
}
