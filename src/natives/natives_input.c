 #include <stdio.h>
#include <string.h>
#include "vm.h"
#include "obj.h"
#include "memory.h"

static Value inputNative(int argCount, Value* args) {
    if (argCount > 0 && IS_STRING(args[0])) {
        printf("%s", AS_CSTRING(args[0]));
        fflush(stdout);
    }

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NIL_VAL;
    }

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    return OBJ_VAL(copyString(buffer, (int)len));
}
void registerInputNatives() {
    defineNative("input", inputNative);

}