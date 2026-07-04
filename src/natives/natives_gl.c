#include <stdio.h>
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include "vm.h"
#include "obj.h"
#include "memory.h"

extern GLFWwindow* window;
extern void initOpenGL();
#define ENSURE_WINDOW() do { if (window == NULL) initOpenGL(); } while(0)

static Value windowShouldCloseNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    ENSURE_WINDOW();
    return BOOL_VAL(glfwWindowShouldClose(window));
}

static Value pollEventsNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    ENSURE_WINDOW();
    glfwPollEvents();
    return NIL_VAL;
}

static Value swapBuffersNative(int argCount, Value* args) {
    (void)argCount; (void)args;
    ENSURE_WINDOW();
    glfwSwapBuffers(window);
    return NIL_VAL;
}

static Value clearColorNative(int argCount, Value* args) {
    (void)argCount;
    ENSURE_WINDOW();
    float r = (float)AS_NUMBER(args[0]);
    float g = (float)AS_NUMBER(args[1]);
    float b = (float)AS_NUMBER(args[2]);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return NIL_VAL;
}

void registerGLNatives() {
    defineNative("windowShouldClose", windowShouldCloseNative);
    defineNative("pollEvents",        pollEventsNative);
    defineNative("swapBuffers",       swapBuffersNative);
    defineNative("clearColor",        clearColorNative);
}