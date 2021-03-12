# zsql

`zsql` is a fast frequent/recent directory jumper tool with its storage in a SQLite database.

## Install

You will need:

* sqlite 3.28 or greater
* utf8proc 2.6 or greater

No release tarballs have been published. Autotools is needed to set up the development environment.

Run `autoreconf --install` to set up the autotools scripts necessary to configure.

Run `./configure && make && sudo make install` to install. Afterwards, you will need to add `eval "$(z -S)"` to your `.bashrc` (or equivalent file), which will create the alias around the binary which changes directories.

## Other

Set `ZSQL_DEBUG=1` to debug scoring. For example on my machine,

```
/home/?/Documents/Programming/C/zsql $ ZSQL_DEBUG=1 z doc
13195.4946      /home/?/Documents
12200.3205      /home/?/Documents/Programming
11800.0800      /home/?/Documents/Programming/C
11433.5394      /home/?/Documents/Programming/C/zsql
/home/?/Documents $
```

The entry with the highest score is selected.
