# emelFM2 development handoff

This file is a portable starting point for a developer or coding assistant continuing work in this repository. Repository files and the current checkout are authoritative; the branch and worktree notes below describe the checkout when this file was prepared and can become stale.

## Project overview

emelFM2 is a C file manager with two GTK file panes, an internal command line, plugins, and an internal viewer/editor. GTK 2 and GTK 3 builds are supported. This fork also contains newer interface features and regression tests. The project uses a hand-written `Makefile` and `Makefile.config`; it does not use Autotools or CMake.

Start with `README.md`, `docs/CONSTRUCT`, `docs/INSTALL`, and `Makefile.config`. For feature-specific behavior, consult `docs/USAGE`, `docs/ACTIONS`, `docs/CONFIGURATION`, and, as applicable, `docs/TERMINAL` or `docs/MODERN_UI`.

## Source map

- `src/e2_fileview.c`, `src/e2_pane.c`, `src/e2_tabs.c`: file lists, pane behavior, navigation, and tabs.
- `src/command/`: command parsing/execution, quoting, aliases, and completion.
- `src/dialogs/`: configuration, internal viewer/editor, and other dialogs.
- `src/utils/`: shared widgets and utilities, including menus and command-line input.
- `plugins/`: optional plugins. `src/utils/e2_menu.c` assembles shared menus.
- `tests/`: shell-driven and C regression tests; UI tests generally create an isolated profile and run under Xvfb/D-Bus.
- `docs/`: user documentation and build notes.
- `po/`: gettext catalogs for German, French, Japanese, Polish, Russian, and Simplified Chinese.

Follow nearby code for ownership, filename encoding, GTK compatibility, locking, and error handling. Use `_()` for user-visible strings and preserve both GTK 2 and GTK 3 paths when editing shared UI code. Update user docs when a user-visible option or behavior changes.

## Build

Choose one GTK generation explicitly. For example, a GTK 2 build with VTE disabled:

```sh
make -j2 WITH_GTK2=1 WITH_GTK3=0 WITH_VTE=0
```

For GTK 3, use `WITH_GTK2=0 WITH_GTK3=1`. Pass the same options to later make targets such as `install`. Common feature switches in `Makefile.config` include `WITH_VTE`, `WITH_MODERN_UI`, `NEW_COMMAND`, `WITH_LATEST`, and `I18N`. VTE is opt-in; modern UI support is compiled in by default but remains a user preference. Check the config comments before changing less common options.

The viewer's GTK 2 syntax highlighting requires `libsource-highlight-dev` and a C++ compiler; GTK 3 uses `libgtksourceview-4-dev`. The CI workflow `.github/workflows/validate.yml` lists the Ubuntu packages used for GTK builds and UI tests. Other core build dependencies include a C compiler, `pkg-config`, gettext, GTK development headers, Pango, X11, and libmagic development files.

Generated build headers and object files are shared across configurations. Do not build GTK 2 and GTK 3 variants concurrently in one checkout or trust old objects after changing flags. Use a separate clean checkout/worktree per configuration, or follow `tests/run-build-configuration.sh` and its configuration checks. That script defaults to a GTK 2, VTE-off build and runs the CI regression suite; its matrix is controlled by `TEST_GTK`, `TEST_VTE`, `TEST_COMMAND`, and `TEST_MODERN_UI`. The ignored `.build-config` file is only a build stamp, not a source of truth.

## Regression tests

Run the narrowest relevant runner after behavior changes, with `GTK_PACKAGE` matching the build where supported. Useful mappings for recently added features:

- Active-pane indicator and action search: `tests/run-action-search.sh`; pane tabs and focus behavior: `tests/run-pane-tabs.sh`.
- Command-line Tab completion/Escape navigation: `tests/run-command-line-keys.sh`.
- Copy-name/full-path plugin menu and Bash path output: `tests/run-clipnames-menu.sh`.
- Bash ANSI-C path quoting and parsing: `tests/run-command-quoting.sh` and `tests/run-command-quoting-ui.sh`.
- Large-file warning and viewer behavior: `tests/run-viewer-ui.sh`.

For example, `GTK_PACKAGE=gtk+-2.0 tests/run-viewer-ui.sh` selects GTK 2 for a UI runner. Use `gtk+-3.0` for GTK 3 builds. Some runners set or assume a GTK package themselves, so check the script before relying on its default. UI tests may require Xvfb, `xauth`, and `dbus-x11`; the CI workflow installs these. `tests/run-build-configuration.sh` is the broader build-and-regression entry point.

## Recently completed feature work

These changes are already present in the history at the recorded handoff commit. Check `git log` before repeating or reverting them.

- Pane focus indicator and Ctrl+Shift+A action search, including switching panes from results and routing arrow keys to the newly active pane (`tests/run-action-search.sh`).
- Command-line Tab completion remains in the input; Escape toggles Tab navigation between completion and GUI focus (`tests/run-command-line-keys.sh`).
- Copy full paths and one-path-per-line entries appear in the file/folder Plugins context submenu when the clip-names plugin is loaded and enabled in Menu. Bash-quoted variants support filenames containing shell syntax and arbitrary filename bytes (`tests/run-clipnames-menu.sh`).
- The command input/Commandlog parser accepts the Bash `$'…'` syntax emitted by those variants and keeps quoted contents literal through command handling (`tests/run-command-quoting.sh`, `tests/run-command-quoting-ui.sh`).
- The internal viewer warns before opening or refreshing files larger than a configurable threshold, defaulting to 128 MiB (`tests/run-viewer-ui.sh`).

Relevant implementation is mainly in `plugins/e2p_clipnames.c`, `src/command/e2_command_quoting.[ch]`, `src/command/e2_command.c`, `src/utils/e2_command_line.c`, `src/utils/e2_menu.c`, `src/dialogs/e2_view_dialog.c`, and `src/e2_pane.c`.

## Worktree and change handling

At the time this handoff was written, the checkout was on `master` at `4b3ade0` (`Parse Bash ANSI-C quoted paths in the command input`, 2026-10-08). The worktree also had uncommitted changes in `po/de.po`, `po/fr.po`, `po/ja.po`, `po/pl.po`, `po/ru.po`, and `po/zh_CN.po`. They include refreshed source references and newly extracted strings, with some new entries untranslated or fuzzy. Preserve them unless translation work is requested. Always inspect `git status`, `git diff`, branch, and recent history on the machine where work resumes; this snapshot may no longer match.

Keep changes focused on the user's request, preserve unrelated edits, and stage only files belonging to the requested change. Do not run `make i18n` or regenerate catalogs as a side effect of unrelated work. When the user asks for separate commits, keep each requested change in its own commit. Do not commit `UIUX.md` or files under `UIUX-mockups/` for UI implementation work unless the user explicitly asks for those files.

Builds and tests can create binaries, generated headers, caches, or local configuration. Review status after validation and do not remove or reset artifacts or edits whose ownership is unclear. Do not commit generated build outputs.
