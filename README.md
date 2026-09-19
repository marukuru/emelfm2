> [!NOTE]
> **AI assistance**
> Code changes were developed with assistance from OpenAI Codex using the **GPT-6 Astra** model.

emelFM2 is a file manager that implements the popular two-pane design. It features a simple Gtk interface, a flexible filetyping scheme, and capacity for executing commands without opening a terminal-emulator application. It's designed to be small, and as fast as possible. For users so inclined, it's extensively customizable.

emelFM2 works on Gtk from 2.6 to 3.22. Some aspects of the design of Gtk3 and/or Gtk3 themes are 'unfriendly' (not only to emelFM2).
#### LICENSE
emelFM2 is licensed under the [GNU General Public License V.3](./docs/GPL).
#### INTERNATIONALISATION
User-interface-string translations exist for
 * japanese
 * simplified chinese
 * french
 * german
 * russian
 * polish

Feel free to do another language. Start by asking on the mailing list if anybody else is already doing the same. The 'make' targets are i18n and [un]install_i18n.

The help documents [USAGE](./docs/USAGE) and [CONFIGURATION](./docs/CONFIGURATION) might well be translated at some future time, but they are not yet mature. Probably not worth translating yet.
#### BUILD, COMPILE
See files [CONSTRUCT](./docs/CONSTRUCT) and [Makefile.config](./Makefile.config).
#### INSTALL, UNINSTALL
See the [INSTALL](./docs/INSTALL) file.

Optional embedded terminals for GTK 2 and GTK 3 are available with explicit
`WITH_VTE=1`. The existing command panel remains the default. See
[terminal build, usage and validation instructions](docs/TERMINAL).

#### Changes in this fork
- Crash prevention and memory hardening in file lists, dialogs, and filesystem operations.
- Safer background directory reads, refresh handling, and child-process cleanup.
- More robust file I/O, atomic saves, and error handling for deletion and encryption.
- Configurable tray icons with desktop indicators, deferred questions, and optional notifications.
- Optional embedded VTE terminals for GTK 2 and GTK 3, supporting interactive tools such as `htop`.
- Dynamic file-pane column sizing, column header menus, and an equal panel sizes option.
- Custom icons, configurable regular-file colors, and extended command-output colors.
- Date-variable expansion when opening bookmarks.
- Updated compiler compatibility, Debian packaging, and automated DEB/AppImage releases.
- Regression tests for command parsing, wildcard expansion, tray behavior, and terminals.

Enable **general → miscellaneous → allow only one instance** to reuse the running
window for the same configuration directory. Further launches quietly restore and
focus it, including when it is hidden in the tray. This works without a session bus.

Enable **interface → miscellaneous → enable tabs** to group a pair of panes and
their navigation bars in each tab. **Ctrl+N** opens a tab with the current folders;
click a tab to switch or its **×** button to close it. Titles show both folder
names. Tabs retain navigation history, selection, filters, sorting and pane sizes
during the session. The command bar and terminal remain shared. Disabling tabs
keeps the current folder pair; only the current pair is restored on next startup.

GUI regression checks: `tests/run-single-instance.sh` and
`tests/run-pane-tabs.sh` (set `GTK_PACKAGE=gtk+-2.0` for GTK 2; set
`E2_TABS_VTE=1` when testing a build with embedded terminals).
