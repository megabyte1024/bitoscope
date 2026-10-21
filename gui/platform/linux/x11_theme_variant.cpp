/*
 * SPDX-License-Identifier: MIT
 */

#include "x11_theme_variant.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

#include <dlfcn.h>

#include <cstring>

namespace bitoscope::gui::linux_platform {

namespace {

// WHY: libX11 is loaded with dlopen as GLFW does: the program must start on a system without it.
struct Xlib {
  decltype(&XInternAtom) internAtom = nullptr;
  decltype(&XChangeProperty) changeProperty = nullptr;
  decltype(&XFlush) flush = nullptr;
};

const Xlib* xlib() {
  static const Xlib loaded = [] {
    Xlib x;
    if (void* library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL)) {
      x.internAtom = reinterpret_cast<decltype(&XInternAtom)>(dlsym(library, "XInternAtom"));
      x.changeProperty =
        reinterpret_cast<decltype(&XChangeProperty)>(dlsym(library, "XChangeProperty"));
      x.flush = reinterpret_cast<decltype(&XFlush)>(dlsym(library, "XFlush"));
    }
    return x;
  }();
  const bool complete =
    loaded.internAtom != nullptr && loaded.changeProperty != nullptr && loaded.flush != nullptr;
  return complete ? &loaded : nullptr;
}

}

void setX11ThemeVariant(GLFWwindow* window, bool dark) {
  const Xlib* x = xlib();
  Display* display = glfwGetX11Display();
  const Window handle = glfwGetX11Window(window);
  if (x == nullptr || display == nullptr || handle == None) {
    return;
  }
  const Atom variant = x->internAtom(display, "_GTK_THEME_VARIANT", False);
  const Atom utf8 = x->internAtom(display, "UTF8_STRING", False);
  const char* value = dark ? "dark" : "light";
  x->changeProperty(display, handle, variant, utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(value),
                    static_cast<int>(std::strlen(value)));
  x->flush(display);
}

std::uint64_t x11WindowId(GLFWwindow* window) {
  return static_cast<std::uint64_t>(glfwGetX11Window(window));
}

}
