PREFIX=/home/$(shell whoami)
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INCLUDEDIR=$(PREFIX)/include
USER=$(shell whoami)
UID=$(shell id -u `whoami`)
GID=$(shell id -g `whoami`)
PACKAGE_NAME=mod_rainback
PACKAGE_VERSION=1.3
PACKAGE_RELEASE=4
PACKAGE_URL=rainback.com
build_cpu=x86_64

CXXFLAGS=-I $(HOME)/include -I/usr/include/httpd -I/usr/include/apr-1 -lapr-1 -laprutil-1 -fPIC -L$(HOME)/lib -lparsegraph

all: mod_rainback.so
.PHONY: all

SOURCES=src/module.c src/route.c src/auth.c src/page.c src/generate.c src/homepage.c src/login.c src/logout.c src/killed.c src/signup.c src/profile.c src/account.c src/live_environment.c
#SOURCES=src/module.c src/route.c src/auth.c src/contact.c src/homepage.c src/user.c src/user_json.c src/import.c src/search.c src/environment.c src/payment.c src/profile.c src/subscribe.c
HEADERS=src/mod_rainback.h

mod_rainback.so: $(SOURCES) $(HEADERS)
	$(CC) -I src -o$@ $(CXXFLAGS) -shared -g `pkg-config --cflags openssl apr-1 ncurses` $(SOURCES) -L.. -lmarla

clean:
	rm -f mod_rainback.so $(PACKAGE_NAME).spec rpm.sh $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz
.PHONY: clean

check: mod_rainback.so runtest
	./runtest
.PHONY: check

runtest: src/runtest.c
	$(CC) -I src -o$@ $(CXXFLAGS) -shared -g `pkg-config --cflags openssl apr-1 ncurses` $^

rpm.sh: src/rpm.sh.in
	cp -f $< $@-wip
	sed -i -re 's/@PACKAGE_NAME@/$(PACKAGE_NAME)/g' $@-wip
	sed -i -re 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g' $@-wip
	sed -i -re 's/@PACKAGE_RELEASE@/$(PACKAGE_RELEASE)/g' $@-wip
	mv $@-wip $@
	chmod +x rpm.sh

$(PACKAGE_NAME).spec: src/rpm.spec.in
	cp -f $< $@-wip
	sed -i -re 's/@PACKAGE_NAME@/$(PACKAGE_NAME)/g' $@-wip
	sed -i -re 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g' $@-wip
	sed -i -re 's/@PACKAGE_RELEASE@/$(PACKAGE_RELEASE)/g' $@-wip
	sed -i -re 's/@PACKAGE_SUMMARY@/$(PACKAGE_SUMMARY)/g' $@-wip
	sed -i -re 's/@PACKAGE_DESCRIPTION@/$(PACKAGE_DESCRIPTION)/g' $@-wip
	sed -i -re 's/@PACKAGE_URL@/$(PACKAGE_URL)/g' $@-wip
	sed -i -re 's/@build_cpu@/$(build_cpu)/g' $@-wip
	mv $@-wip $@

$(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz: $(SOURCES) $(HEADERS) Makefile
	tar --transform="s'^'$(PACKAGE_NAME)-$(PACKAGE_VERSION)/'g" -cz -f $@ $^

dist-gzip: $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz $(PACKAGE_NAME).spec
.PHONY: dist-gzip

rpm: rpm.sh $(PACKAGE_NAME).spec dist-gzip
	bash $<
.PHONY: rpm
