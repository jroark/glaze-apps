# Maps

Maps (`packages/maps`) shows a CF GPS receiver's position on offline tile
packs. glaze-os documents the card, the kernel driver and the pack format in
`docs/hardware/GPS.md`.

## Map view

- **Packs.** `.gzmap` packs are read from `/Maps` and `/Card/Maps`. The
  **Maps** button lists them. When a fix falls outside the open pack, Maps
  switches to a pack that covers it.
- **Tiles.** Tiles decode two per frame, middle out, into a 12-tile cache
  (128 KiB each). Until a tile is ready, the nearest coarser tile is drawn
  magnified. Past a pack's deepest zoom, tiles are magnified up to two more
  levels.
- **Moving the map.** Drag to pan. Double-tap, the **+** and **-** buttons, or
  Page Up and Page Down change the zoom. The arrow keys pan, and OK means
  **Locate**.
- **Following.** **Locate** follows the position again; panning stops
  following.
- **Position.** The fix is a blue dot. A ring shows HDOP × 6 m, and a heading
  line appears above 0.5 m/s. The dot turns grey when the fix is more than
  5 s old.
- **Overlays.** A scale bar, the pack's attribution, and status messages: no
  card, searching, acquiring.
- **GPS page.** It shows the receiver and baud rate, the fix type,
  satellites, position, altitude, speed, HDOP and UTC time, then a signal bar
  for each satellite (green when used in the fix).
- **Recording.** **Record** keeps a point whenever the position moves 3 m, up
  to 8000 points. It keeps the app running in the background. **Stop** writes
  GPX 1.1 to `/Documents/Tracks/YYYYMMDD-HHMMSS.gpx` (UTC).
- **Saved state.** The pack, zoom, centre and follow mode are saved in the
  bundle's `data/state`.

The package links glaze-os's `libgps.a` (`LDLIBS += -lgps`). It includes Mark
Adler's puff (zlib licence), changed to use GCC's setjmp builtins on the
device.

## Tests

```sh
make -C tests maps-ui-test        # native; screenshots in out/host/maps-*.bmp
./build.sh out/pkg/maps.gzp
mkdir -p ../glaze-os/out/input && cp tests/ui/t_maps.txt ../glaze-os/out/input/
(cd ../glaze-os && ./build.sh INPUT_SCRIPT=out/input/t_maps.txt)
python3 tests/run_maps.py         # be300 --cf-gps glaze-os/tests/gps/track.nmea
```

The native test uses glaze-os's synthetic `demo.gzmap`, made by
`tests/maptiles_test.py`. It checks:

- pack discovery and reading;
- tile drawing;
- following, pan, zoom limits and double-tap;
- a recorded track's GPX;
- the GPS and pack pages;
- the card being removed;
- saved state;
- refusing a corrupt pack.
