VTE integration concept
=======================

This document records the concept and its implementation stages. The separately
requested removal of the startup-path row and live `user@folder` terminal titles
are the baseline for this proposal.

Implementation status:

- Stage 1: implemented. One notebook per pane, Command log and terminal tabs,
  tab-strip New terminal and View actions buttons, contextual menus and inline
  restart/close controls. Extra native log creation and detach are disabled in
  VTE builds. Existing log pages are consolidated with their text and formatting,
  including when per-main-tab outputs are combined. Running commands are retargeted.
- Stages 2 and 3: pending.

The main problem is the interaction model: terminals feel like a second
application embedded below the file manager. Each side has a terminal toolbar,
an outer notebook containing “Application output”, and potentially another
notebook inside that page. Some controls belong to logs, some to shells, and
some to the application as a whole. Selecting a tab should make these choices
obvious without requiring knowledge of those layers.

The recommended direction is **one tools area per directory pane**, containing
one flat row of tabs. Keep the two areas side by side. Each area contains a
permanent **Command log** tab, zero or more terminal tabs, and a **+** button.
The existing choice between shared outputs and outputs owned by main tabs remains.

```text
Main tabs:  [ Project A × ] [ Project B × ] [ + ]

Left directory pane                   Right directory pane
Files and folders                     Files and folders
──────────────────────────────────┬──────────────────────────────────
[Command log] [user@src ×] [+] [⋯]  │ [Command log] [user@build ×] [+] [⋯]
                                  │
Selected log or terminal           │ Selected log or terminal
                                  │
──────────────────────────────────┴──────────────────────────────────
Application status / background tasks
```

Here `×`, `+` and `⋯` represent icons with accessible names and tooltips. The
diagram shows placement, not an additional row of text buttons. The tools area
uses the application's fonts, spacing, borders, icons and focus styling;
terminal text retains its separate font and color preferences.

Ideas worth pursuing, in priority order:

| Priority | Idea | Benefit |
| --- | --- | --- |
| First | Flatten the nested notebooks into one tab strip per pane. | Logs and shells become peer views with predictable navigation. |
| First | Replace “Application output” with “Command log” in the VTE layout. | Explains why this view is different from an interactive shell. |
| First | Put New terminal in the tab strip; move secondary actions into a menu. | Removes two permanent rows of six buttons. |
| First | Use the same selected-tab and focused-pane appearance as the file lists. | Makes it clear which pane owns a terminal and where an action will apply. |
| First | Show only actions supported by the selected view. | Prevents log operations appearing to act on a terminal. |
| Next | Align the divider between tools areas with the divider between file panes. | Makes the left/right relationship visually immediate. |
| Next | Offer “Show terminal folder in pane” when a current local directory is known. | Connects shell navigation back to the file manager without automatic navigation. |
| Next | Offer “Insert selected paths” in the terminal menu and through a configurable shortcut. | Retains a useful bridge between file selection and shell commands. |
| Next | Add an unobtrusive unread-output indicator to inactive tabs. | Makes background activity discoverable without stealing focus. |
| Next | Add search to the selected log or terminal, where the backend supports it. | Makes long output useful without copying it into an editor. |
| Later | Offer explicit, optional shell integration for prompt, command and remote-location metadata. | Allows reliable busy/completed indicators and better remote titles. |
| Later | Offer restoration of terminal locations as a startup preference. | Restores useful context without pretending to restore running processes. |

Opening **+** creates a terminal in the associated file pane's current local
directory and focuses it. Navigating the file pane subsequently leaves the
shell's directory alone. Changing directory inside the shell updates its title,
but does not navigate the file pane. “Show terminal folder in pane” is an explicit
action, enabled only for a verified local directory; remote paths must never be
treated as local paths simply because their names match.

Terminal tabs retain `user@folder` titles and their own close buttons. Tooltips
can disclose the full current directory, remote host when reported, and session
state without adding a permanent path bar. Duplicate titles are acceptable;
tooltips distinguish their locations. If both user and folder match, an optional
user-assigned tab name could be a later addition. No implementation detail such
as a process ID belongs in the normal title.

Running shells need no permanent status row. Exited or failed terminals retain
their contents and show a compact message with **Restart** and **Close** actions.
Restart should clearly disclose which directory it will use. Prefer the last
known valid local directory, falling back to the startup directory with an
explanation; never silently reinterpret a remote path as local. This is a
proposed behavior change from the current restart-in-startup-directory behavior.

The command entry continues to launch native emelFM2 commands into the owning
pane's Command log. Selecting a terminal must not silently redirect that entry
into a shell. Interactive commands are entered directly in the terminal. Opening
a new terminal can briefly explain this distinction; ordinary use should not
require a tutorial or a mode selector. Background command output remains attached
to its originating log when the user changes panes or main tabs.

Clicking a terminal activates its owning pane while retaining that pane's file
selection. The existing return-to-file-list shortcut restores the last focused
entry. A visible focus border should distinguish keyboard focus from tab
selection. Keep terminal key handling intact: Ctrl+C, Ctrl+Z, Tab and application
keys belong to the terminal while it has focus. Keep main-tab Ctrl+Tab navigation
in the file-manager context; provide a configurable, explicit terminal-tab
shortcut without intercepting keys used by terminal applications by default.

Use one **⋯** menu for the selected view, with a small shared core:

| Action | Command log | Terminal |
| --- | --- | --- |
| Copy selection | Yes | Yes |
| Paste | No | Yes |
| Find | Existing log search | Add only with matching backend support |
| Clear | Clear the log | Omit initially; clearing a screen and deleting scrollback are different operations |
| Edit contents | Keep for logs | Never |
| Insert selected paths | No | Yes, using the existing quoting rules |
| Show folder in pane | No | Only when the current local folder is known |
| Restart / Close session | No | Yes; retain confirmation for live sessions |
| Hide / Expand tools area | Shared layout actions | Shared layout actions |

In the VTE layout, disable creation of extra native output tabs initially: each
pane has one Command log. This removes a second tab-management system. Preserve
text selection, copy, search, log clearing, command history and task output
routing. Keep the richer native-output layout available to builds without VTE.
Existing profiles with several log tabs need an explicit migration that preserves
their content; hiding extra pages is not an acceptable migration.

Also disable log detach/reattach in the first version of the unified VTE layout.
Detached windows complicate pane ownership, tab closure and focus restoration.
Do not introduce detachable terminal tabs until those rules can be consistent
for both kinds of view. Remove the permanent Focus terminal, Copy, Paste,
Insert paths and Restart toolbar buttons after their replacements are accessible
through direct interaction, the menu and shortcuts. Keep New terminal as the
single primary action in the tab strip.

The tools area should resize as a unit below the two file lists. Its vertical
divider follows the file-pane divider by default. Hide/show should remember each
pane's selected tab and the area's previous height. Maximizing the selected view
must provide a clear restore action and preserve focus. Keep the side-by-side
layout requested here; adapting it to stacked file panes is a separate decision.

When outputs belong to main tabs, changing main tabs keeps shells alive and
restores the selected tools tab for each pane. Closing a main tab confirms the
closure of its live terminals together. Turning per-main-tab ownership off must
preserve sessions and output. A future flatter layout must retain the existing
merge behavior or provide an equally explicit preservation path.

Do not equate “shell process exists” with “command is busy”: an idle prompt has
a live shell too. Add busy/completed badges only when shell integration can
report that state reliably. Unread-output markers can work independently, but
should be subtle and should not force the tools area open. Refresh indicators
for hidden tabs without moving the user's selection or focus.

Metadata has practical limits. VTE can receive directory information through
OSC 7 and shell titles through OSC 0/2; this is supplied by the shell rather than
inferred from the terminal's visible prompt. Modern VTE exposes these through
terminal properties. GTK2's legacy backend has fewer metadata facilities, so
capabilities must be checked per backend. See the [VTE property documentation](https://gnome.pages.gitlab.gnome.org/vte/gtk3/index.html).
Linux process information is a useful local fallback, but reading another user's
working directory is permission-controlled. See [proc_pid_cwd](https://man7.org/linux/man-pages/man5/proc_pid_cwd.5.html).
Show unknown information honestly. A displayed username is a convenience, not an
authentication indicator. Shell integration, if added later, should be opt-in and
must not overwrite the user's prompt or startup files.

Implement this concept in three reviewable stages:

1. Flatten the tab structure, add the tab-strip New terminal button, adopt
   context-sensitive menus, and remove redundant permanent buttons. Resolve
   preservation of existing log tabs before removing their controls.
2. Unify divider positioning, hide/expand behavior, pane focus and return-to-list
   behavior. Add the explicit terminal-folder-to-file-pane action where supported.
3. Add search, unread-output indicators and optional shell metadata support.
   Consider session-location restoration only after ownership and closure are
   predictable.

Acceptance should cover GTK2 and GTK3, VTE enabled and disabled, shared and
per-main-tab ownership, a narrow window, keyboard-only use, and both idle and
busy terminals. A user must be able to open a terminal for either pane, change
directories, switch main tabs, run a background native command, return to the
selected file, close a session and recover from a failed shell without losing
output or wondering where keyboard input will go. Native command logging and
terminal job control must continue to work throughout.
