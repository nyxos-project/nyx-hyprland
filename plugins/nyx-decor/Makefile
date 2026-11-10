# nyx-decor, NyxOS's window decoration plugin. Built against the Hyprland it runs in
# (hyprland-nyx; its headers come with the hyprland package's dev output), and
# rebuilt with it: the plugin refuses to load into another Hyprland build.

CXXFLAGS ?= -O2
PKGS = hyprland pixman-1 libdrm libinput libudev wayland-server xkbcommon cairo pangocairo hyprgraphics
SRC = main.cpp HeaderBar.cpp CornerButton.cpp AppIcon.cpp
PREFIX ?= /usr/local

all: nyx-decor.so

nyx-decor.so: $(SRC) globals.hpp HeaderBar.hpp CornerButton.hpp AppIcon.hpp
	$(CXX) -shared -fPIC --no-gnu-unique -std=c++2c $(CXXFLAGS) $(SRC) -o $@ `pkg-config --cflags $(PKGS)` $(EXTRA_CFLAGS)

install: nyx-decor.so
	install -Dm755 nyx-decor.so $(DESTDIR)$(PREFIX)/lib/nyx-decor.so

clean:
	rm -f nyx-decor.so

.PHONY: all install clean
