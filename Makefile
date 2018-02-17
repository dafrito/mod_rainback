PREFIX=/home/$(shell whoami)
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INCLUDEDIR=$(PREFIX)/include
USER=$(shell whoami)
UID=$(shell id -u `whoami`)
GID=$(shell id -g `whoami`)
PACKAGE_NAME=servermod
PACKAGE_VERSION=1.0
PACKAGE_RELEASE=21
PACKAGE_URL=rainback.com
build_cpu=x86_64

CXXFLAGS=-I $(HOME)/include -I/usr/include/httpd -I/usr/include/apr-1 -lapr-1 -laprutil-1 -fPIC -L$(HOME)/lib -lparsegraph_user -lparsegraph_List -lparsegraph_environment

all: libservermod.so
.PHONY: all

libservermod.so: servermod.c
	$(CC) -o$@ $(CXXFLAGS) -shared -g `pkg-config --cflags openssl apr-1 ncurses` -I"../src" $^

clean:
	rm -f libservermod.so
.PHONY: clean

rpm.sh: rpm.sh.in
	cp -f $< $@-wip
	sed -i -re 's/@PACKAGE_NAME@/$(PACKAGE_NAME)/g' $@-wip
	sed -i -re 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g' $@-wip
	sed -i -re 's/@PACKAGE_RELEASE@/$(PACKAGE_RELEASE)/g' $@-wip
	mv $@-wip $@
	chmod +x rpm.sh

$(PACKAGE_NAME).spec: rpm.spec.in
	cp -f $< $@-wip
	sed -i -re 's/@PACKAGE_NAME@/$(PACKAGE_NAME)/g' $@-wip
	sed -i -re 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g' $@-wip
	sed -i -re 's/@PACKAGE_RELEASE@/$(PACKAGE_RELEASE)/g' $@-wip
	sed -i -re 's/@PACKAGE_SUMMARY@/$(PACKAGE_SUMMARY)/g' $@-wip
	sed -i -re 's/@PACKAGE_DESCRIPTION@/$(PACKAGE_DESCRIPTION)/g' $@-wip
	sed -i -re 's/@PACKAGE_URL@/$(PACKAGE_URL)/g' $@-wip
	sed -i -re 's/@build_cpu@/$(build_cpu)/g' $@-wip
	mv $@-wip $@

$(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz: servermod.c Makefile
	tar --transform="s'^'$(PACKAGE_NAME)-$(PACKAGE_VERSION)/'g" -cz -f $@ $^

dist-gzip: $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz $(PACKAGE_NAME).spec
.PHONY: dist-gzip

rpm: rpm.sh $(PACKAGE_NAME).spec dist-gzip
	bash $<
.PHONY: rpm
