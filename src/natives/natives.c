#include "natives.h"

void registerGLNatives();
void registerInputNatives();

void registerNatives() {
    registerGLNatives();
    registerInputNatives();
}
