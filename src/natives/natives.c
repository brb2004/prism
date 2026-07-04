#include "natives.h"

void registerGLNatives();
void registerInputNatives();
void registerMathNatives();
void registerCoreNatives();

void registerNatives() {
    registerGLNatives();
    registerInputNatives();
    registerMathNatives();
    registerCoreNatives();
}
