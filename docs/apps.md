# Linux application contract

Install apps under **`/home/apps/<name>/`**, with an executable `run.sh` entry
point. The FreeWili 2 **Linux > Apps** browser starts at `/home/apps/`.
It shows folders, `.py`, `.sh`, and executable files. Enter your app folder
and launch `run.sh` to use the bundled Python dependencies.

The launcher runs the selected file as the Linux console user, with the file's
parent as working directory. Standard input is disconnected. Standard output
and error go to a unique log under `~/.local/state/freewili/apps/`. The launch
dialog reports a process ID and log name; successful spawning does not prove
the app completed. Check its log. Avoid `input()` and terminal-only prompts.

The Python installer bundles the pinned OneWili Python sources, CM0 adapter,
and `result` dependency into the app's `.lib/`. Its launcher resolves paths
relative to itself. A source checkout, virtualenv activation, root login, or
PC is not needed at launch. Use short names; the firmware browser omits paths
longer than 126 encoded bytes and limits each listing to 512 entries.

Keep app-owned data under `~/.local/share/<app-name>/` and configuration under
`~/.config/<app-name>/`. Create them as needed. Treat the app folder as code.
MAIN SD paths used by OneWili file transfers are separate from Linux paths.

## Transfer from a development computer

Use `python tools/fw.py install hello_python --apps-dir dist/apps` to stage a
self-contained app after running `setup`. Copy `dist/apps/hello_python` into
`/home/apps/` using FreeWili GUI's Linux file tools or configured SSH/SCP.
On CM0, run `chmod +x /home/apps/hello_python/run.sh`; copied native binaries
also need the executable bit. A Windows/macOS native executable cannot run
on CM0: build native apps on CM0 or use a matching Linux ARM64 toolchain/sysroot.

Installation refuses to overwrite existing apps. Stop an app before replacing
its folder, preserve its user data, and deliberately remove or rename the old
code folder. Each installed Python app keeps the dependency version it shipped
with; updating the BSP checkout alone does not update installed apps.

## Standalone repositories

Pin this BSP as a submodule. A Python app can use the same setup/install pattern;
for C++, add the BSP with `add_subdirectory`, set `BUILD_TESTING=OFF` for consumer
builds, and link `wilicm0::wilicm0`. Keep your app target separate from BSP code.
Add an `AGENTS.md` linking to the pinned BSP's guide, documenting power/peripheral
ownership, your launch command, and tests. Publish a relocatable app folder with
your release, including dependency licenses and the target architecture.
