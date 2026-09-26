# Development

Initialize `libs/onewili` with `git submodule update --init --recursive`.
The driver is included as source; no firmware monorepo is needed. GoogleTest
is fetched at a fixed commit only when BUILD_TESTING is enabled.

```sh
cmake -S . -B build-tests -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release --parallel 2
ctest --test-dir build-tests -C Release --output-on-failure
python3 -m unittest discover -s tests -p test_tools.py
```

On Windows, select an installed Visual Studio or Ninja compiler generator.
Driver hardware and app execution require CM0 Linux. Linux/macOS host builds
also test the actual Unix socket adapter against a fake bridge with fragmented
replies, busy refusal, disconnect, absent socket, and orderly close.

For on-device changes, compile on the CM0 and run the read-only examples, then
install and run their launchers from `/home/apps`. Test connection close/reopen
and MAIN USB coexistence when changing transport behavior. Do not test GPIO
outputs or transmit on a bus without confirming the attached circuit.

Generated API changes belong upstream in OneWili's generator. Update its
submodule only after the upstream commit is published. New dependencies must
be pinned and accessible to customers. Keep testing records clear about the
specific firmware/image combination tested.

For a source release, commit the changes and run
`python3 tools/package.py dist/wilicm0bsp-source.tar.gz`. It reads the committed
tree and its pinned submodule, omits Git metadata, and normalizes timestamps.
Verify the archive includes `libs/onewili` before attaching it to a release.
