# zsql

`zsql` is a fast frequent/recent directory jumper tool with its storage in a SQLite database.

## Install

You will need:

* sqlite 3.28 or greater
* utf8proc 2.6 or greater

No release tarballs have been published. Autotools is needed to set up the development environment.

Run `autoreconf --install` to set up the autotools scripts necessary to configure.

Run `./configure && make && sudo make install` to install.
