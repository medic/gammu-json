`gammu-json`
============

A simple command-line interface to the important portions of `libgammu`,
supporting JSON and UTF-8. Usable now; still under active development.

Examples
--------


Summary
-------

A simple command-line utility to send, retrieve, and delete SMS messages using
`libgammu` and a supported phone and/or GSM/CDMA modem. In all cases, input is
accepted as UTF-8 encoded program arguments, and output is provided as UTF-8
encoded JSON.

Multi-part messages (sometimes referred to as "concatenated SMS") are
supported.

In send mode, messages are automatically broken up in to multiple message parts
when size limits require it. User-data headers (UDHs) are generated
automatically to allow for message reassembly.

When sending messages, the utility automatically detects whether the message
can be sent using the default GSM alphabet (described in GSM 03.38). If so,
message parts may be up to 160 characters in length. If any character in the
string cannot be represented in the default GSM alphabet, the *entire message*
will be encoded as UCS-2, reducing the number of characters available per
message (or message part). This is a limitation of the underlying SMS protocol;
it cannot be fixed without making changes to the protocol itself.

In receive mode, message parts are each returned as separate messages; however,
every part of a multipart message will have the `udh`, `segment`, and
`total_segments` attributes set to meaningful values. The `udh` value is
a unique 8-bit or 16-bit integer, generated by the sender, that differentiates
sets of concatenated messages from one-another -- that is, messages with the
same `udh` value should be linked together.

This utility does not perform message reassembly in receive mode; reassembly
requires some form of persistent data storage, since each message part could be
delayed indefinitely. Architecturally speaking, message reassembly will need to
be performed at a higher level.

Building
--------

Building `gammu-json` from source can typically be accomplished by running
`make` and `make install`. The included `Makefile` uses `pkg-config` to locate
`libgammu`. If you'd like to link with a `libgammu` that isn't under `/usr`,
try defining the `PREFIX` variable when you run `make`, like this:

``make PREFIX=/srv/software/libgammu``

or, alternatively, set the `PKG_CONFIG_PATH` environment variable to point
directly to your preferred `$prefix/lib/pkgconfig` directory.

Setup
-----

Currently, `gammu-json` requires that (a) you have a valid `/etc/gammurc` file
(or symbolic link), and that (b) that file provides a working phone/modem
configuration in the `gammu` section. Here's a simple example:

``[gammu]
Connection = at
Device = /dev/ttyUSB0``

Future modifications will remove this requirement, and allow you to provide
device and configuration information as program arguments. For now, though,
this is the simplest way to get started.

Licensing
---------

This software is released under the GNU General Public License (GPL) v3.

Executing and/or parsing the output of an unmodified `gammu-json` from your
closed-source program *does not* create a derivative work of `gammu-json`, nor
does it require you to release whatever source code it is that you're afraid of
other people seeing. Your secret's safe with us.

However: if you modify the `gammu-json` software itself, and distribute a
compiled version containing your modifications to others (either as software,
as a software package, or as software preloaded on a piece of hardware), you
will be required to offer those modifications (in their original human-readable
source code form) to whomever obtains the modified software.

Before sending hate mail, please note that Gammu itself (including `libgammu`)
is licensed under the GPL v2 (or later, presumably at your option).


